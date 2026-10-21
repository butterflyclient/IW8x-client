#include "diagnostics/night/night_loot.h"
#include "diagnostics/night/night_loot_bindings.h"
#include "diagnostics/night/night_files.h"
#include "common/console.h"
#include <windows.h>
#include <cstring>
// Temporary no-loot testing policy requested by the user, following the 1.44
// Caldera approach. This is not complete retail loot support or error suppression.

namespace night_loot
{
	namespace
	{
		namespace binding = night_loot_binding;
		uintptr_t imageBase{};
		using InitFn = void (*)();
		using RowsFn = void (*)(void*);
		std::atomic<InitFn> originalInit{};
		std::atomic<RowsFn> originalRows{};
		bool installed{};
		constexpr char World[] = "maps/mp/mp_kstenod.d3dbsp";
		constexpr char EscapeWorld[] = "maps/mp/mp_escape2.d3dbsp";
		static_assert(sizeof(World) == sizeof(EscapeWorld));

		template <class Fn>
		Fn native(uintptr_t rva)
		{
			return reinterpret_cast<Fn>(imageBase + rva);
		}

		bool copy(const void* src, void* dst, size_t bytes)
		{
			const DWORD error = GetLastError();
			SIZE_T copied{};
			const bool ok = src && ReadProcessMemory(GetCurrentProcess(), src, dst, bytes, &copied) && copied == bytes;
			SetLastError(error);
			return ok;
		}

		bool active()
		{
			if (!night_files::Enabled()) return false;
			const void* map{};
			const char* name{};
			std::array<char, sizeof(World)> text{};
			return copy(reinterpret_cast<void*>(imageBase + binding::MapEnts), &map, sizeof(map)) && map &&
				copy(map, &name, sizeof(name)) && name && copy(name, text.data(), text.size()) &&
				((!std::memcmp(text.data(), World, sizeof(World)) && night_files::MapEnabled("mp_kstenod")) ||
					(!std::memcmp(text.data(), EscapeWorld, sizeof(EscapeWorld)) && night_files::MapEnabled("mp_escape2")));
		}

		bool string(const char* src, std::array<char, 256>& dst)
		{
			// Small exact reads avoid VirtualQuery of the large map allocation and
			// never read beyond a terminator at the end of a readable page.
			if (!src) return false;
			for (size_t i = 0; i < dst.size(); ++i)
			{
				if (!copy(src + i, &dst[i], 1)) return false;
				if (!dst[i]) return true;
			}
			return false;
		}

		void initialize()
		{
			if (!active())
			{
				originalInit.load(std::memory_order_acquire)();
				return;
			}
			// Independently executed original Replay 130D210 empty branches. The
			// surrounding native scriptable initializer still owns allocation/lifetime.
			*reinterpret_cast<std::uint64_t*>(imageBase + 0xD721238) = 0;
			*reinterpret_cast<std::uint16_t*>(imageBase + 0xD721240) = 0;
			*reinterpret_cast<std::uint8_t*>(imageBase + 0xD721456) = 0;
			*reinterpret_cast<std::uint32_t*>(imageBase + 0xD752B08) = 0;
			LOG_INFO("NightLoot", "temporary no-loot policy: selected native empty-loot initialization for admitted map");
		}

		bool loot_table(const char* requested)
		{
			std::array<char, 256> name{};
			if (!string(requested, name)) return false;
			if (!name[0] || !std::strcmp(name.data(), "mp/loot_item_defs.csv")) return true;
			const void* dvar{};
			const char* value{};
			std::array<char, 256> configured{};
			return copy(reinterpret_cast<void*>(imageBase + binding::LootDvar), &dvar, sizeof(dvar)) && dvar &&
				copy(static_cast<const char*>(dvar) + 0x28, &value, sizeof(value)) && string(value, configured) && configured[0] &&
				!std::strcmp(name.data(), configured.data());
		}

		void rows(void* context)
		{
			// br_pickups::initarrays must still build its arrays/callbacks and reach
			// initscriptablemanagement. Only its selected loot-definition table has
			// zero rows under this policy; missing purchase/quest tables still fail.
			// Do not change client-VM behavior when Night is merely preloaded.
			if (context == reinterpret_cast<void*>(imageBase + binding::ServerContext) && active() &&
				native<unsigned (*)(void*)>(binding::NumParams.rva)(context) >= 1)
			{
				const auto* name = native<const char* (*)(void*, unsigned)>(binding::GetString.rva)(context, 0);
				if (loot_table(name))
				{
					native<void (*)(void*, int)>(binding::AddInt.rva)(context, 0);
					LOG_INFO("NightLoot", "temporary no-loot policy: initialized GSC loot-definition table with zero rows");
					return;
				}
			}
			originalRows.load(std::memory_order_acquire)(context);
		}
	}

	bool UseEscapeHUDArt()
	{
		if (!installed || !active()) return false;
		const void* map{};
		const char* name{};
		std::array<char, sizeof(EscapeWorld)> text{};
		return copy(reinterpret_cast<void*>(imageBase + binding::MapEnts), &map, sizeof(map)) && map && copy(map, &name, sizeof(name)) &&
			copy(name, text.data(), text.size()) && !std::memcmp(text.data(), EscapeWorld, sizeof(EscapeWorld));
	}

	bool UseDefaultEscapeHUDTable()
	{
		if (!UseEscapeHUDArt()) return false;
		const void* dvar{};
		const char* value{};
		char first{};
		return copy(reinterpret_cast<void*>(imageBase + binding::LootDvar), &dvar, sizeof(dvar)) && dvar &&
			copy(static_cast<const char*>(dvar) + 0x28, &value, sizeof(value)) && copy(value, &first, 1) && !first;
	}

	hook::Status Install(uintptr_t base)
	{
		if (!night_files::Enabled()) return hook::Status::Installed;
		if (installed) return imageBase == base ? hook::Status::Installed : hook::Status::Failed;
		for (const auto& entry : { binding::Initialize, binding::Rows, binding::NumParams, binding::GetString, binding::AddInt })
		{
			std::array<std::uint8_t, 16> observed{};
			if (!copy(reinterpret_cast<void*>(base + entry.rva), observed.data(), observed.size()) || observed != entry.bytes)
				return hook::Status::Failed;
		}
		imageBase = base;
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(base + binding::Initialize.rva), &initialize, binding::Initialize.bytes.data(),
			binding::Initialize.bytes.size(), originalInit);
		batch.Add(
			reinterpret_cast<void*>(base + binding::Rows.rva), &rows, binding::Rows.bytes.data(), binding::Rows.bytes.size(), originalRows);
		const auto status = batch.Commit();
		installed = status == hook::Status::Installed;
		if (installed)
			LOG_INFO("NightLoot", "installed map-scoped temporary no-loot policy; other maps and non-loot tables retain native behavior");
		return status;
	}
}
