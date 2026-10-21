#include "auth/offline_auth.h"

#include "engine/addresses.hpp"

#include "network/backend/frontend_backend.h"
#include "auth/offline_identity.h"
#include "auth/identity_store.h"

#include <mutex>

#include "engine/safemem.h"

#include "common/console.h"

#include "dedicated/dedicated.h"

#include "engine/game.h"

#include "network/online_services.h"
#include "auth/profile/local_stats.h"
#include "auth/profile/local_progression.h"
#include "auth/profile/local_playercard.h"
#include "auth/profile/local_settings.h"
#include "client/weapons/weapon_loadouts.h"
#include <bcrypt.h>

#include <atomic>
#include <array>
#include <charconv>

#include <intrin.h>
namespace offlineauth
{
	namespace
	{
		constexpr uintptr_t kUser = 0xe5c0730;
		constexpr uintptr_t kAuth = 0x4622910;
		constexpr uintptr_t kPlatformClient = 0xf05ac10;
		constexpr uintptr_t kPlatformXuid = 0xf05ace8;
		constexpr uintptr_t kDevName = 0xf05ad00;
		constexpr uintptr_t kContentFinished = 0xe371231;
		constexpr uintptr_t kLastBnetError = 0xf05ad24;
		constexpr uintptr_t kOfflineStatsFetched = 0xd317db4;
		constexpr uintptr_t kOnlineStatsFetched = 0xd305a34;
		constexpr uintptr_t kGetStatsDDLContext = 0x15c7940;
		constexpr uintptr_t kStatsInit = 0x12a4190;
		constexpr uintptr_t kProfileLogin = 0x15d7bb0;
		constexpr uintptr_t kDDLLoadAsset = 0x2051180;
		constexpr uintptr_t kGamerSettings = 0xef28ae0;
		constexpr uintptr_t kLuiVM = 0x11868880;
		constexpr uintptr_t kDemoRegistration = 0x259c448;
		constexpr uintptr_t kStockDemo = 0x19b96a0;
		constexpr uintptr_t kStockTrue = 0x1a039e0;
		uintptr_t g_base = 0;
		Identity g_identity;
		IdentityStore g_identityStore;
		std::mutex g_identityLock;
		bool g_prepared = false;
		int g_statsSource = 1; // OFFLINE; configured online clients select ONLINE=0.
		std::atomic<bool> g_enabled{ false }, g_ready{ false };
		std::atomic<unsigned long long> g_beginCalls{ 0 }, g_dwCalls{ 0 }, g_statsCalls{ 0 };
		std::atomic<unsigned> g_repairs{ 0 }, g_errorCalls{ 0 };
		using BeginFn = void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
		using SignedFn = bool (*)(int);
		using StatusFn = int (*)(int);
		using PlatformIdFn = bool (*)(int, uint64_t*);
		using PlatformNameFn = bool (*)(int, char*, size_t);
		using ErrorFn = const char* (*)(const uint32_t*);
		using ConnectFn = bool (*)(uintptr_t);
		// DW_Frame ignores the return register of Replay's controller logon thunk.
		using DWConnectFn = void (*)(int);
		std::atomic<ConnectFn> g_connect{ nullptr };
		std::atomic<DWConnectFn> g_dwConnect{ nullptr };
		std::atomic<unsigned> g_connectSkipped{ 0 };
		std::atomic<unsigned> g_dwConnectSkipped{ 0 };
		std::atomic<BeginFn> g_begin{ nullptr };
		std::atomic<SignedFn> g_signed{ nullptr };
		std::atomic<StatusFn> g_status{ nullptr }, g_stats{ nullptr };
		std::atomic<PlatformIdFn> g_platformId{ nullptr };
		std::atomic<PlatformNameFn> g_platformName{ nullptr };
		std::atomic<ErrorFn> g_error{ nullptr };

		bool Writable(uintptr_t address, size_t size)
		{
			size_t available = 0;
			return safemem::RegionReadable(reinterpret_cast<void*>(address), available) && available >= size &&
				safemem::RegionWritable(reinterpret_cast<void*>(address));
		}

		template <class T>
		T Read(uintptr_t rva)
		{
			T result{};
			safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &result, sizeof(result));
			return result;
		}

		template <class T>
		void Write(uintptr_t rva, T value)
		{
			// All ranges are checked together before the first write.
			memcpy(reinterpret_cast<void*>(g_base + rva), &value, sizeof(value));
		}

		bool UsesFrontendProvider(int controller)
		{
			return controller == 0 && g_enabled.load(std::memory_order_acquire) && g_statsSource == 0;
		}

		bool Ready(int controller)
		{
			return controller == 0 && g_ready.load(std::memory_order_acquire) && (g_statsSource != 0 || frontend_backend::Authenticated());
		}

		bool CompleteLocalAuth(uintptr_t object)
		{
			if (g_statsSource == 0 && !frontend_backend::Authenticated()) return false;
			std::lock_guard<std::mutex> lock(g_identityLock);
			if (object != g_base + kAuth) return false;
			const std::pair<uintptr_t, size_t> regions[] = { { kAuth, 0x318 }, { kUser, sizeof(UserRecord) }, { kPlatformXuid, 8 },
				{ kDevName, 36 }, { kContentFinished, 1 }, { kLastBnetError, 4 } };
			for (auto [rva, size] : regions)
				if (!Writable(g_base + rva, size)) return false;
			const int previous = Read<int>(kAuth);
			const uint32_t previousError = Read<uint32_t>(kLastBnetError);
			const bool first = !g_ready.load(std::memory_order_relaxed);
			const bool changed = first || previous != 2 || Read<int>(kUser) != 2 || Read<uint64_t>(kUser + 0x90) != g_identity.xuid ||
				Read<uint64_t>(kUser + 0xb8) != g_identity.xuid || Read<uint64_t>(kPlatformXuid) != g_identity.xuid ||
				Read<uint8_t>(kAuth + 0x2d0) != 1 ||
				memcmp(reinterpret_cast<void*>(g_base + kDevName), g_identity.name, sizeof(g_identity.name)) != 0;
			if (!changed && previousError == 0) return true;

			g_ready.store(false, std::memory_order_release);
			PopulateUser(*reinterpret_cast<UserRecord*>(g_base + kUser), g_identity);
			Write<uint64_t>(kPlatformXuid, g_identity.xuid);
			memcpy(reinterpret_cast<void*>(g_base + kDevName), g_identity.name, sizeof(g_identity.name));
			// kUser+0xb8 is also the platform/presence ID. Do not write xuid/6 here:
			// zeroproxy's subsequent assign_xuid overwrites that earlier write too.
			const uintptr_t field = kAuth + 0x2f4;
			// Preserve the game's initialized per-field key and shift.
			const uint32_t key = Read<uint32_t>(field + 8);
			const uint8_t shift = Read<uint8_t>(field + 4) & 31u;
			const bool owns = g_statsSource != 0 || frontend_backend::Premium();
			Write<uint32_t>(field, TransformOwnership(uint32_t{ owns } << shift, g_base + field, key));
			Write<uint8_t>(field + 4, shift);
			Write<uint8_t>(kAuth + 0x2d0, 1);
			Write<uint32_t>(kAuth + 0x1c8, 0); // auth failure result
			Write<uint8_t>(kAuth + 0x314, 0); // disconnected-after-auth flag
			Write<uint32_t>(kLastBnetError, 0);
			Write<uint8_t>(kContentFinished, 1); // byte in Replay, never a DWORD
			Write<int32_t>(kAuth, 2);
			g_ready.store(true, std::memory_order_release);
			if (g_repairs.fetch_add(1) < 8)
				LOG_INFO("Auth", "%s local controller 0: bnet=%d->2 prior_error=%08X xuid=%llu name='%s' ownership=1",
					first ? "initialized" : "resynchronized", previous, previousError, static_cast<unsigned long long>(g_identity.xuid),
					g_identity.name);
			return true;
		}

		bool ConnectPlatformClient(uintptr_t object)
		{
			// Live frame 1665AC0 calls platform update 1662F00 BEFORE BeginAuth.
			// Its 1662C40 connection attempt creates the external Battle.net
			// client and calls Connect at 3E4FA0. An absent launcher can block
			// this frame for ~30 seconds, so BeginAuth cannot run until timeout.
			// Offline mode needs no external client. Return a real disconnected
			// result; stock retry bookkeeping and the following local auth run.
			if (g_enabled.load(std::memory_order_acquire) && object == g_base + kPlatformClient)
			{
				if (g_connectSkipped.fetch_add(1) == 0)
					LOG_INFO("Auth", "offline startup: skipped external Battle.net client connection before BeginAuth");
				return false;
			}
			return g_connect.load(std::memory_order_acquire)(object);
		}

		void Begin(uintptr_t object, uintptr_t a2, uintptr_t a3, uintptr_t a4)
		{
			g_beginCalls.fetch_add(1, std::memory_order_relaxed);
			if (g_statsSource == 0) frontend_backend::Pump();
			// The online provider completes only after the dedicated service verifies
			// this saved identity. A pending HTTP request must not launch retail SSO.
			if (g_enabled.load() && g_statsSource == 0 && !frontend_backend::Authenticated()) return;
			if (g_enabled.load(std::memory_order_acquire))
			{
				if (!CompleteLocalAuth(object))
				{
					g_ready.store(false, std::memory_order_release);
					if (g_repairs.fetch_add(1) < 8) LOG_ERR("Auth", "BeginAuth object/ranges do not match Replay; forwarding stock call");
				}
			}
			// With state=2, stock BeginAuth takes its existing no-work branch.
			// Preserve its calling convention and never feed it invented SSO tokens.
			g_begin.load(std::memory_order_acquire)(object, a2, a3, a4);
		}

		bool Signed(int controller)
		{
			++g_dwCalls;
			if (UsesFrontendProvider(controller)) return Ready(controller);
			return Ready(controller) ? true : g_signed.load(std::memory_order_acquire)(controller);
		}

		int Status(int controller)
		{
			if (UsesFrontendProvider(controller)) return Ready(controller) ? 2 : 0;
			return Ready(controller) ? 2 : g_status.load(std::memory_order_acquire)(controller);
		}

		void ConnectDemonware(int controller)
		{
			// DW_Frame (14F2CC0) independently calls this thunk when Status is 0.
			// A backend lease can expire after a frame stall or a network outage even
			// after BeginAuth has completed. Starting the retail logon HSM here would
			// pump bdLogin without a Battle.net token (observed null read at 212B167).
			// Keep the provider disconnected until its HTTP authentication recovers;
			// the retail reconnect must never run for this configured controller.
			if (UsesFrontendProvider(controller))
			{
				if (g_dwConnectSkipped.fetch_add(1, std::memory_order_relaxed) == 0)
					LOG_INFO("Auth", "retail DW reconnect deferred; controller 0 waits for backend authentication");
				return;
			}
			g_dwConnect.load(std::memory_order_acquire)(controller);
		}

		int Stats(int controller)
		{
			++g_statsCalls;
			return Ready(controller) ? g_statsSource : g_stats.load(std::memory_order_acquire)(controller);
		}

		bool PlatformId(int controller, uint64_t* out)
		{
			if (!Ready(controller)) return g_platformId.load(std::memory_order_acquire)(controller, out);
			return safemem::WriteBytes(out, &g_identity.xuid, sizeof(g_identity.xuid));
		}

		bool PlatformName(int controller, char* out, size_t size)
		{
			if (!Ready(controller)) return g_platformName.load(std::memory_order_acquire)(controller, out, size);
			if (!size || size > 4096 || !Writable(reinterpret_cast<uintptr_t>(out), size)) return false;
			std::lock_guard<std::mutex> lock(g_identityLock);
			strncpy_s(out, size, g_identity.name, _TRUNCATE);
			return true;
		}

		const char* Error(const uint32_t* code)
		{
			if (g_errorCalls.fetch_add(1) < 8)
			{
				uint32_t value = 0;
				safemem::ReadBytes(code, &value, sizeof(value));
				LOG_ERR("Auth", "native Blizzard error BLZBNTBGS%08X; local_ready=%d", value, int(g_ready.load()));
				void* frames[12]{};
				const USHORT count = RtlCaptureStackBackTrace(1, 12, frames, nullptr);
				for (USHORT i = 0; i < count; ++i)
				{
					const uintptr_t pc = reinterpret_cast<uintptr_t>(frames[i]);
					if (pc >= g_base && pc < g_base + 0x1324b000)
						LOG_ERR("Auth", "  frame %u game RVA 0x%llX", i, static_cast<unsigned long long>(pc - g_base));
				}
				LogSnapshot();
			}
			return g_error.load(std::memory_order_acquire)(code);
		}

		template <size_t N>
		bool Matches(uintptr_t rva, const uint8_t (&bytes)[N])
		{
			std::array<uint8_t, N> actual{};
			return safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), actual.data(), N) && memcmp(actual.data(), bytes, N) == 0;
		}

		hook::Status UseStockOfflineBootCallback()
		{
			// The forced demo callback routes the boot flow to Local Play. A configured
			// online client must retain Replay's original callback instead.
			if (!dedicated::Enabled() && online_services::Configured())
			{
				if (Read<uintptr_t>(kDemoRegistration) != g_base + kStockDemo) return hook::Status::NotReady;
				LOG_INFO("Auth", "online frontend: retaining stock IsDemoBuild callback");
				return hook::Status::Installed;
			}
			const uint8_t trueCode[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0xba, 1, 0, 0, 0, 0x48, 0x8b, 0xd9 };
			if (!Matches(kStockTrue, trueCode)) return hook::Status::NotReady;
			auto** slot = reinterpret_cast<void**>(g_base + kDemoRegistration);
			void* expected = reinterpret_cast<void*>(g_base + kStockDemo);
			void* replacement = reinterpret_cast<void*>(g_base + kStockTrue);
			const auto current = Read<uintptr_t>(kDemoRegistration);
			if (current == reinterpret_cast<uintptr_t>(replacement)) return hook::Status::Installed;
			if (current != reinterpret_cast<uintptr_t>(expected)) return hook::Status::NotReady;
			DWORD old = 0;
			if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return hook::Status::Failed;
			void* prior = InterlockedCompareExchangePointer(slot, replacement, expected);
			DWORD ignored = 0;
			const bool restored = VirtualProtect(slot, sizeof(void*), old, &ignored) != FALSE;
			if (prior != expected || !restored) return hook::Status::Failed;
			LOG_INFO("Auth", "offline boot: Engine.IsDemoBuild uses stock true callback; Local Play already allowed in Replay");
			return hook::Status::Installed;
		}
	}

	Identity GetIdentity()
	{
		std::lock_guard lock(g_identityLock);
		return g_identity;
	}

	bool Prepare(HMODULE self)
	{
		wchar_t path[MAX_PATH]{};
		const DWORD len = GetModuleFileNameW(self, path, MAX_PATH);
		if (!len || len >= MAX_PATH) return false;
		wchar_t* slash = wcsrchr(path, L'\\');
		if (!slash) return false;
		const bool server = dedicated::Enabled();
		*slash = 0;
		if (!g_identityStore.Load(path, server, g_identity))
		{
			LOG_ERR("Auth", "could not load/create persistent identity; existing files preserved in players");
			return false;
		}
		if (!server)
		{
			// Replay Com_WriteConfiguration (12B59B0) waits on F529B0's cloud
			// readiness gate. This standalone identity has no cloud sync; use the
			// game's explicit opt-out so archived FOV/controller dvars save normally.
			if (!SetEnvironmentVariableA("CODPC_NOCLOUD_SYNC", "1")) return false;
			local_stats::Prepare(self, g_identity.xuid);
			local_playercard::Prepare(g_identity.xuid);
			LOG_INFO("Auth", "native local configuration saving enabled (cloud sync opt-out)");
		}
		g_prepared = true;
		LOG_INFO("Auth", "persistent local identity loaded; xuid=%llu name='%s' file=%ls (local MP compatibility)",
			static_cast<unsigned long long>(g_identity.xuid), g_identity.name,
			server ? L"players/server_identity.bin" : L"players/identity.bin");
		return true;
	}

	bool HandleCommand(std::string_view command)
	{
		const auto split = command.find_first_of(" \t");
		if (command.substr(0, split) != "name") return false;
		std::string_view name = split == std::string_view::npos ? std::string_view{} : command.substr(split + 1);
		while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
			name.remove_prefix(1);
		while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
			name.remove_suffix(1);
		if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
		{
			name.remove_prefix(1);
			name.remove_suffix(1);
		}
		if (name.empty())
		{
			std::lock_guard<std::mutex> lock(g_identityLock);
			OP_INFO("Current name: %s. Usage: name \"Player 1\"", g_identity.name);
			return true;
		}
		if (!IdentityStore::ValidName(name))
		{
			OP_ERR("Name must be 1-35 printable ASCII characters, without quotes, semicolons or backslashes");
			return true;
		}
		{
			std::lock_guard<std::mutex> lock(g_identityLock);
			if (!g_identityStore.Rename(g_identity, name))
			{
				OP_ERR("Name could not be saved; current identity was kept");
				return true;
			}
		}
		// Auth and platform-name getters share the same identity. The XUID never
		// changes here, so all existing DDL/playercard saves remain associated.
		CompleteLocalAuth(g_base + kAuth);
		local_playercard::OnNameChanged();
		OP_INFO("Name saved: %.*s. Rejoin to refresh an existing server's player list.", int(name.size()), name.data());
		return true;
	}

	hook::Status Install(uintptr_t base)
	{
		if (!g_prepared) return hook::Status::Failed;
		if (g_enabled.load()) return hook::Status::Installed;
		g_base = base;
		g_statsSource = !dedicated::Enabled() && online_services::Configured() ? 0 : 1;
		// Independent anchors prove the data addresses and v1 record stride.
		const uint8_t getter[] = { 0x48, 0x8d, 0x05, 0x89, 0x26, 0xfc, 0x02, 0xc3 };
		const uint8_t userGetter[] = { 0x48, 0x63, 0xc1, 0x48, 0x69, 0xc8, 0xe8, 0, 0, 0, 0x48, 0x8d, 0x05, 0x8f, 0xad, 0xf5, 0x0c };
		const uint8_t xuidGetter[] = { 0x48, 0x8b, 0x05, 0xd1, 0x50, 0x9f, 0x0d, 0x48, 0x89, 0x01, 0x48, 0x8b, 0xc1, 0xc3 };
		const uint8_t clientGetter[] = { 0x48, 0x8d, 0x05, 0x49, 0x7c, 0x9f, 0x0d, 0xc3 };
		if (!Matches(0x1660280, getter) || !Matches(0x1665990, userGetter) || !Matches(0x1665c10, xuidGetter) ||
			!Matches(0x1662fc0, clientGetter))
			return hook::Status::NotReady;

		// Complete instruction boundaries verified in the MD5-matching Replay image.
		const uint8_t begin[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x50, 0x48, 0x8b, 0x05, 0x4b, 0x44, 0x3f, 0x04 };
		const uint8_t signedIn[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0xd9 };
		const uint8_t status[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x63, 0xc1, 0xba, 0x21, 0, 0, 0 };
		const uint8_t stats[] = { 0x48, 0x83, 0xec, 0x28, 0x48, 0x8b, 0x05, 0x65, 0x3a, 0xbb, 0x0d };
		const uint8_t platformId[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda };
		const uint8_t platformName[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20 };
		const uint8_t error[] = { 0x40, 0x53, 0x48, 0x81, 0xec, 0xb0, 0, 0, 0, 0x48, 0x8b, 0x05, 0xf8, 0xf9, 0x3e, 0x04 };
		const uint8_t connect[] = { 0x48, 0x89, 0x5c, 0x24, 8, 0x48, 0x89, 0x7c, 0x24, 0x10, 0x55, 0x48, 0x8b, 0xec };
		const uint8_t dwConnect[] = { 0x48, 0x63, 0xc1, 0x48, 0x69, 0xc8, 0x30, 0x21, 0, 0, 0x48, 0x8d, 0x05, 0xbf, 0x6f, 0xca, 0x0d };
		const uint8_t statsInit[] = { 0x44, 0x89, 0x4c, 0x24, 0x20 };
		const uint8_t profileLogin[] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x81, 0xec, 0x80, 0, 0,
			0 };
		const uint8_t ddlLoad[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9 };
		if (!Matches(game::kComGameModeGetActiveGameModeRVA, game::kComGameModeGetActiveGameModePrologue) ||
			!Matches(kStatsInit, statsInit) || !Matches(kProfileLogin, profileLogin) || !Matches(kDDLLoadAsset, ddlLoad))
			return hook::Status::NotReady;
		const uint8_t contextGetter[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 };
		if (!Matches(kGetStatsDDLContext, contextGetter) || !Matches(game::kSysIsMainThreadRVA, game::kSysIsMainThreadPrologue))
			return hook::Status::NotReady;
		// Later profile adapters can defer initialization. Do not attempt to
		// recreate the already committed authentication batch on that retry.
		static bool authHooksInstalled = false;
		if (!authHooksInstalled)
		{
			hook::Batch batch;
			batch.Add(reinterpret_cast<void*>(base + 0x165ffa0), Begin, begin, sizeof(begin), g_begin);
			batch.Add(reinterpret_cast<void*>(base + 0x1528490), Signed, signedIn, sizeof(signedIn), g_signed);
			batch.Add(reinterpret_cast<void*>(base + 0x17ec930), Status, status, sizeof(status), g_status);
			batch.Add(reinterpret_cast<void*>(base + 0x12a1eb0), Stats, stats, sizeof(stats), g_stats);
			batch.Add(reinterpret_cast<void*>(base + 0x1666030), PlatformId, platformId, sizeof(platformId), g_platformId);
			batch.Add(reinterpret_cast<void*>(base + 0x1665c80), PlatformName, platformName, sizeof(platformName), g_platformName);
			batch.Add(reinterpret_cast<void*>(base + 0x16649f0), Error, error, sizeof(error), g_error);
			batch.Add(reinterpret_cast<void*>(base + 0x1662c40), ConnectPlatformClient, connect, sizeof(connect), g_connect);
			batch.Add(reinterpret_cast<void*>(base + 0x17ec990), ConnectDemonware, dwConnect, sizeof(dwConnect), g_dwConnect);
			const auto authHooks = batch.Commit();
			if (authHooks != hook::Status::Installed) return authHooks;
			authHooksInstalled = true;
		}
		if (!dedicated::Enabled())
		{
			const auto card = local_playercard::Install(base);
			if (card != hook::Status::Installed) return card;
			const auto settings = local_settings::Install(base);
			if (settings != hook::Status::Installed) return settings;
		}
		const auto boot = UseStockOfflineBootCallback();
		if (boot != hook::Status::Installed) return boot;
		g_enabled.store(true, std::memory_order_release);
		LOG_INFO("Auth",
			"9 verified Replay hooks installed; external client wait disabled; backend controller retains provider across reconnects; waiting for BeginAuth RVA 0x165ffa0");
		return hook::Status::Installed;
	}

	void LogSnapshot()
	{
		if (!g_enabled.load(std::memory_order_acquire)) return;
		const uint32_t encoded = Read<uint32_t>(kAuth + 0x2f4);
		const uint32_t key = Read<uint32_t>(kAuth + 0x2fc);
		const uint8_t shift = Read<uint8_t>(kAuth + 0x2f8) & 31;
		const bool owns = (TransformOwnership(encoded, g_base + kAuth + 0x2f4, key) >> shift) == 1;
		LOG_INFO("Auth",
			"snapshot: ready=%d begin=%llu dw_queries=%llu stats_queries=%llu bnet=%d user=%d identity_match=%d finished=%u owns=%d content=%u offline_stats_fetched=%u online_stats_fetched=%u stats_source=%d error=%08X",
			int(g_ready.load()), g_beginCalls.load(), g_dwCalls.load(), g_statsCalls.load(), Read<int>(kAuth), Read<int>(kUser),
			int(Read<uint64_t>(kUser + 0x90) == g_identity.xuid && Read<uint64_t>(kUser + 0xb8) == g_identity.xuid &&
				Read<uint64_t>(kPlatformXuid) == g_identity.xuid),
			Read<uint8_t>(kAuth + 0x2d0), int(owns), Read<uint8_t>(kContentFinished), Read<uint8_t>(kOfflineStatsFetched),
			Read<uint8_t>(kOnlineStatsFetched), g_statsSource, Read<uint32_t>(kLastBnetError));
	}

	bool IsReady(int controller)
	{
		return Ready(controller);
	}

	bool InitializeProfile()
	{
		if (dedicated::Enabled() || !g_enabled.load(std::memory_order_acquire) || !Ready(0)) return false;
		if (!reinterpret_cast<game::Sys_IsMainThread_t>(g_base + game::kSysIsMainThreadRVA)()) return false;
		const auto activeMode = reinterpret_cast<uint8_t (*)()>(g_base + game::kComGameModeGetActiveGameModeRVA)();
		// The stock boot fence fetches the shared online profile before a mode is
		// selected. All six DDL assets below must exist before initialization; do
		// not deadlock boot by requiring entry into MP first.
		if (activeMode != game::kGameModeMP && activeMode != 0) return false;
		static bool complete = false; // Only called on the client's main thread.
		static bool initializing = false;
		const uintptr_t fetched = g_statsSource == 0 ? kOnlineStatsFetched : kOfflineStatsFetched;
		static ULONGLONG lastAttempt = 0;
		if (complete && Read<uint8_t>(fetched) && Read<uint8_t>(kGamerSettings)) return true;
		if (initializing) return false; // Native init/profile events can reenter LUI.
		const auto now = GetTickCount64();
		if (now - lastAttempt < 1000 || !Read<uintptr_t>(kLuiVM)) return false;
		lastAttempt = now;
		initializing = true;

		struct InitGuard
		{
			bool& value;

			~InitGuard()
			{
				value = false;
			}
		} guard{ initializing };

		const auto load = reinterpret_cast<const void* (*)(const char*)>(g_base + kDDLLoadAsset);
		for (const char* asset : { "ddl/mp/playerdata.ddl", "ddl/mp/cpdata.ddl", "ddl/mp/commondata.ddl", "ddl/mp/rankedloadouts.ddl",
				 "ddl/mp/privateloadouts.ddl", "ddl/mp/nongamedata.ddl" })
		{
			const auto file = load(asset);
			uintptr_t definition = 0;
			if (!file || !safemem::ReadBytes(static_cast<const uint8_t*>(file) + 8, &definition, sizeof(definition)) || !definition)
				return false;
		}
		if (!weapon_loadouts::Prepare(g_base)) return false;
		// Replay StatsInit also uses the active-source getter internally. Its source
		// argument, our Stats hook, and the fetched flag must refer to the same bank.
		// Publish fetched before native reset so buffer getters return storage.
		// Initialize all six MP schema groups, including private loadouts; cpdata.ddl
		// is part of the native MP stats schema, not an activation of co-op gameplay.
		if (!Read<uint8_t>(fetched))
		{
			if (!Writable(g_base + fetched, 3)) return false;
			Write<uint8_t>(fetched, 1);
			Write<uint8_t>(fetched + 1, 0);
			Write<uint8_t>(fetched + 2, 0);
			LOG_INFO("Auth", "initializing MP DDL stats: LiveStorage_StatsInit(0, true, false, source=%d), RVA 0x12a4190", g_statsSource);
			reinterpret_cast<void (*)(int, bool, bool, int)>(g_base + kStatsInit)(0, true, false, g_statsSource);
		}
		// The engine creates/repairs contexts with its own obfuscation callbacks.
		// A fetched flag alone is not enough to let loadout UI consume player data.
		for (int group = 0; group < 6; ++group)
		{
			alignas(16) std::array<uint8_t, 128> context{};
			if (!reinterpret_cast<bool (*)(void*, int, int, int)>(g_base + kGetStatsDDLContext)(context.data(), 0, g_statsSource, group))
			{
				LOG_ERR("Auth", "MP stats DDL context unavailable: source=%d group=%d", g_statsSource, group);
				return false;
			}
		}
		if (!local_stats::Initialize(g_base, g_statsSource)) return false;
		if (g_statsSource == 0) local_progression::Pump();
		if (!Read<uint8_t>(kGamerSettings))
		{
			LOG_INFO("Auth", "GamerProfile_LogInProfile(0), Replay RVA 0x15d7bb0");
			reinterpret_cast<void (*)(int)>(g_base + kProfileLogin)(0);
		}
		complete = Read<uint8_t>(kGamerSettings) && Read<uint8_t>(fetched);
		if (complete) LOG_INFO("Auth", "MP profile ready: source=%d stats_fetched=1 ddl_contexts=6 profile_logged_in=1", g_statsSource);
		return complete;
	}
}
