#include "auth/profile/local_playercard.h"

#include "engine/addresses.hpp"

#include "auth/profile/local_stats.h"
#include "auth/offline_auth.h"

#include "engine/game.h"
#include "engine/safemem.h"

#include "common/console.h"

#include <array>
#include <atomic>
namespace local_playercard
{
	namespace
	{
		uintptr_t g_base = 0;
		uint64_t g_xuid = 0;
		bool g_profileReady = false;
		std::atomic<bool> g_pending{ false };
		ULONGLONG g_nextCheck = 0;
		int g_cacheIndex = -1;
		using SaveFn = int64_t (*)(int);
		std::atomic<SaveFn> g_save{ nullptr };

		template <class F>
		F Native(uintptr_t rva)
		{
			return reinterpret_cast<F>(g_base + rva);
		}

		template <class T>
		T Read(uintptr_t rva)
		{
			T value{};
			safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &value, sizeof(value));
			return value;
		}

		bool Refresh()
		{
			const auto mode = Native<uint8_t (*)()>(game::kComGameModeGetActiveGameModeRVA)();
			if (!Native<game::Sys_IsMainThread_t>(game::kSysIsMainThreadRVA)() || !g_profileReady || !offlineauth::IsReady() ||
				(mode != game::kGameModeMP && mode != 0))
				return false;
			const uintptr_t definition = Read<uintptr_t>(0x12c46248);
			int ddlSize = 0;
			if (!definition || !safemem::ReadBytes(reinterpret_cast<void*>(definition + 36), &ddlSize, 4) || ddlSize <= 0 || ddlSize > 255)
				return false;
			auto lookup = Native<int (*)(uint64_t)>(0x1b96540);
			if (lookup(g_xuid) < 0)
			{
				// Stock edit initialization clears the card when this entry is absent.
				// Seed using native DDL before building the card from persisted stats.
				Native<void (*)(int, uint64_t, const void*)>(0x1b996b0)(0, g_xuid, nullptr);
			}
			auto* uploadState = reinterpret_cast<int*>(g_base + 0x12be4970);
			const int previous = *uploadState;
			// 2 is native idle/ready. This local backend has no DW upload task; finish
			// locally after native packing instead of leaving state 0 waiting for DW.
			*uploadState = 2;
			const bool updated = Native<bool (*)(int)>(0x1b992f0)(0);
			*uploadState = updated ? 2 : previous;
			if (!updated) return false;
			g_cacheIndex = lookup(g_xuid);
			if (g_cacheIndex < 0 || g_cacheIndex >= 1024) return false;
			*reinterpret_cast<uint8_t*>(g_base + 0x12c45bdb) = 0;
			g_pending = false;
			local_stats::SaveNow();
			LOG_INFO("Playercard", "committed local card: emblem=%d background=%d cache=%d (native DDL)", Read<int>(0x12be4980 + 68),
				Read<int>(0x12be4980 + 72), g_cacheIndex);
			return true;
		}

		int64_t SaveChanges(int controller)
		{
			const auto result = g_save.load(std::memory_order_acquire)(controller);
			if (controller == 0)
			{
				g_pending = true;
				// Complete before another menu reloads the old cache.
				Refresh();
			}
			return result;
		}
	}

	void Prepare(uint64_t xuid)
	{
		g_xuid = xuid;
	}

	void OnNameChanged()
	{
		g_pending = true;
		Refresh();
	}

	void MarkDirty()
	{
		g_pending = true;
		g_nextCheck = 0;
	}

	hook::Status Install(uintptr_t base)
	{
		if (g_save.load(std::memory_order_acquire)) return hook::Status::Installed;
		if (!g_xuid) return hook::Status::Failed;
		g_base = base;

		struct Binding
		{
			uintptr_t rva;
			std::array<uint8_t, 16> bytes;
		};

		const Binding bindings[] = {
			{ 0x1b96540, { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x33 } },
			{ 0x1b996b0, { 0x48, 0x89, 0x54, 0x24, 0x10, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48 } },
			{ 0x1b992f0, { 0x40, 0x53, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x60, 0x48, 0x8b, 0x05, 0xf8, 0xb0, 0xeb, 0x03 } },
			{ 0x1b972e0, { 0x48, 0x8d, 0x05, 0x99, 0xd6, 0x04, 0x11, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } }
		};
		for (const auto& binding : bindings)
		{
			std::array<uint8_t, 16> bytes{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + binding.rva), bytes.data(), bytes.size()) || bytes != binding.bytes)
				return hook::Status::NotReady;
		}
		const uint8_t save[] = { 0x48, 0x63, 0xc1, 0x48, 0x8d, 0x0d, 0x51, 0xe2, 0x0a, 0x11, 0xc6, 0x04, 0x08, 0x01 };
		const auto status = hook::Install(reinterpret_cast<void*>(base + 0x1b97980), SaveChanges, save, sizeof(save), g_save);
		if (status == hook::Status::Installed) LOG_INFO("Playercard", "verified Replay local save/cache adapter installed");
		return status;
	}

	void Pump()
	{
		if (!local_stats::RemoteReady()) return;
		if (!g_save.load(std::memory_order_acquire)) return;
		g_profileReady = true; // caller has completed stats/profile restoration
		if (GetTickCount64() < g_nextCheck) return;
		g_nextCheck = GetTickCount64() + 2000;
		const bool cached = g_cacheIndex >= 0 && g_cacheIndex < 1024 && Read<uint64_t>(0x12befa40 + 280 * g_cacheIndex + 8) == g_xuid &&
			Read<uint8_t>(0x12befa40 + 280 * g_cacheIndex) != 0;
		if (!cached || g_pending.load()) Refresh();
	}
}
