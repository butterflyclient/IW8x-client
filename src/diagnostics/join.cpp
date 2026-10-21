#include "diagnostics/join.h"

#include "engine/addresses.hpp"

#include "common/console.h"

#include "dedicated/dedicated.h"

#include "engine/game.h"

#include "auth/offline_auth.h"
#include "network/online_services.h"
#include "network/match_connection.h"

#include "engine/safemem.h"
#include "client/console/game_console.hpp"

#include "client/frontend/frontend_materials.h"
#include "diagnostics/island/engine_log_budget.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <intrin.h>

#include <mutex>
namespace joindiag
{
	namespace
	{
		// Verified in the Replay IDB, base 7FF7A95E0000. See ida-reports/join_diagnostics.md.
		constexpr uintptr_t kPrint = 0x12b0660, kError = 0x12ab1c0;
		constexpr uintptr_t kServices = 0x15274c0, kJoinFailed = 0x16079d0;
		constexpr uintptr_t kJoinInfo = 0xc7f1870;
		constexpr uintptr_t kResponseNames = 0x459f1d0;
		constexpr uintptr_t kUser = 0xe5c0730, kAuth = 0x4622910;
		constexpr uint8_t kPrintBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x30 };
		constexpr uint8_t kErrorBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x30 };
		constexpr uint8_t kJoinBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20 };
		using PrintFn = uintptr_t (*)(unsigned, const char*, int);
		using ErrorFn = void (*)(int, const char*, va_list);
		using ServicesFn = bool (*)(int, const char**, const char**);
		using FailedFn = void (*)(void*, const char*, uint8_t);
		std::atomic<PrintFn> g_print{ nullptr };
		std::atomic<ErrorFn> g_error{ nullptr };
		std::atomic<ServicesFn> g_services{ nullptr };
		std::atomic<FailedFn> g_failed{ nullptr };
		using CleanupFn = void* (*)(void*, void*);
		using ErrorMessageFn = uintptr_t (*)(const char*);
		using RecoverFrontendFn = uintptr_t (*)();
		std::atomic<CleanupFn> g_cleanup{ nullptr };
		std::atomic<ErrorMessageFn> g_errorMessage{ nullptr };
		std::atomic<RecoverFrontendFn> g_recoverFrontend{ nullptr };
		thread_local bool g_retryCleanup = false;
		std::atomic<bool> g_installed{ false };
		uintptr_t g_base = 0;
		const char* g_side = "client";
		thread_local bool g_inLog = false;
		std::atomic<unsigned> g_attempt{ 0 };
		std::atomic<unsigned> g_transportFailures{ 0 };
		ULONGLONG g_started = 0, g_lastPoll = 0, g_lastProgress = 0;
		bool g_watching = false;
		int g_lastState = -1, g_lastClient = -1, g_lastResponse = -1;
		uintptr_t g_lastReason = 0;

		template <class T>
		T Read(uintptr_t address, T fallback = {})
		{
			T value = fallback;
			safemem::ReadBytes(reinterpret_cast<const void*>(address), &value, sizeof(value));
			return value;
		}

		uintptr_t CallerRva(const void* caller)
		{
			const auto address = reinterpret_cast<uintptr_t>(caller);
			return address >= g_base && address < g_base + 0x1324b000 ? address - g_base : 0;
		}

		void Clean(char* text)
		{
			// One physical log line per event, even for remote names/error text.
			for (char* p = text; *p; ++p)
				if (static_cast<unsigned char>(*p) < 32 || *p == 127) *p = ' ';
		}

		void CopyText(const char* source, char* out, size_t size)
		{
			safemem::ReadString(source, out, size);
			Clean(out);
		}

		void PointerText(const char** source, char* out, size_t size)
		{
			CopyText(Read<const char*>(reinterpret_cast<uintptr_t>(source)), out, size);
		}

		// Deduplicate identical engine messages, with a bounded rate for busy servers.
		// Explicit join failures and Com_Error bypass this filter.
		bool Admit(const char* text, unsigned channel, int flags)
		{
			struct Slot
			{
				uint64_t hash = 0;
				ULONGLONG tick = 0;
			};

			static Slot slots[128]{};
			static std::mutex lock;
			static ULONGLONG window = 0;
			static unsigned count = 0, dropped = 0;
			uint64_t hash = 14695981039346656037ull ^ channel ^ (uint64_t(flags) << 32);
			for (const char* p = text; *p; ++p)
				hash = (hash ^ uint8_t(*p)) * 1099511628211ull;
			const auto now = GetTickCount64();
			std::lock_guard<std::mutex> guard(lock);
			if (now - window >= 1000)
			{
				if (dropped) LOG_WARN("EngineNet", "suppressed %u repeated/burst lines in previous window", dropped);
				window = now;
				count = 0;
				dropped = 0;
			}
			auto& slot = slots[hash % 128];
			if ((slot.hash == hash && now - slot.tick < 1000) || count >= 120)
			{
				++dropped;
				return false;
			}
			slot = { hash, now };
			++count;
			return true;
		}

		uintptr_t Print(unsigned channel, const char* text, int flags)
		{
			const DWORD saved = GetLastError();
				// Island's converted technique table currently lacks some RTV
				// variants. The engine may issue hundreds of identical diagnostics
				// per second. Normal logging *and native logging* previously ran
				// on every emission, including costly closed-console processing.
				// Admit a representative once per second and summarize the rest.
				// This is intentionally restricted to the exact warning on Island;
				// other render warnings and all actual errors are left unchanged.
				if (detail::island_pipeline_format_warning(channel, flags, text))
				{
					static detail::engine_log_budget budget;
					static std::atomic<ULONGLONG> next_map_check{ 0 };
					static std::atomic<bool> cached_island{ false };
					const auto now = GetTickCount64();
					auto due = next_map_check.load(std::memory_order_relaxed);
					if (now >= due && next_map_check.compare_exchange_strong(
						due, now + 1000, std::memory_order_acq_rel))
					{
						const auto name = Read<const char*>(g_base + 0x5cc7538);
						char map[64]{};
						CopyText(name, map, sizeof(map));
						cached_island.store(std::strcmp(map, "mp_sm_island_1") == 0, std::memory_order_release);
					}
					if (cached_island.load(std::memory_order_acquire))
					{
						const auto decision = budget.accept(now);
						if (!decision.forward)
						{
							SetLastError(saved);
							return 0; // The original printer has no required side effects on duplicate log messages.
						}
						if (decision.suppressed_since_last_forward)
							LOG_WARN("EngineNet", "Island: suppressed %llu duplicate RenderTargetFormat warnings in previous window",
								static_cast<unsigned long long>(decision.suppressed_since_last_forward));
					}
				}
			game_console::print_engine(text, flags);
			const bool trace = console::TraceEnabled();
#ifdef REPLAY_DISABLE_CUSTOM_CONTENT
			constexpr bool watch_pipeline = false;
#else
			const bool watch_pipeline = channel == 16;
#endif
			// Native DB/LUI errors can have normal flags. Custom content additionally
			// observes channel 16 pipeline failures; other traces need no copy or hash.
			if (!g_inLog && text && (trace || flags >= 2 || channel == 10 || channel == 13 || watch_pipeline))
			{
				g_inLog = true;
				char line[1500]{};
				CopyText(text, line, sizeof(line));
				if (char* userinfo = strstr(line, "Full userinfo is "))
					strcpy_s(userinfo, sizeof(line) - size_t(userinfo - line), "Full userinfo is <redacted>");
				// Replay's LUI error reporter writes on channel 13 with normal print
				// flags, so the network-only filter below used to discard Lua failures.
				// Keep UI messages outside its burst budget: map/network loading must
				// not hide the error that caused a menu to stop working.
				if (channel == 13 && line[0])
				{
					if (strstr(line, "error") || strstr(line, "Error") || strstr(line, "ERROR") || strstr(line, "stack traceback:"))
						LOG_ERR("LUI", "%s", line);
					else
						LOG_INFO("LUI", "%s", line);
				}
				// This is an expected headless/offline-service polling condition and can fire
				// every frame. Drop it before Admit() so it also cannot inflate the synthetic
				// repeated/burst suppression counter.
				const bool noisyServicePoll = strstr(line, "Services aren't ready yet.") != nullptr;
				// Native DB errors use channel 10 without error flags. Preserve the
				// reason for a rejected zone independently of network burst logging.
				if (!strncmp(line, "DB ERROR:", 9)) LOG_ERR("Fastfile", "%s", line);
				if (strstr(line, "failed to create graphics pipeline state")) frontend_materials::ReportPipelineFailure();
				if (!noisyServicePoll && (trace || flags >= 2) &&
					(channel == 14 || channel == 15 || channel == 16 || channel == 25 || flags == 3 || (!trace && flags == 2) || strstr(line, "Party") ||
						strstr(line, "NET_") || strstr(line, "DWNet") || strstr(line, "ONLINE/") || strstr(line, "EXE/")) &&
					line[0] && Admit(line, channel, flags))
				{
					// Replay channel 15 carries server initialization/game/client events.
					// Only the dedicated process promotes those to the operator window;
					// the native formatted trace below remains available in its logfile.
					if (dedicated::Enabled() && channel == 15 && line[0] != ' ') LOG_INFO("Server", "%s", line);
					if (trace)
						LOG_INFO("EngineNet", "t=%llu pid=%lu tid=%lu side=%s ch=%u flags=%d caller=0x%llX %s", GetTickCount64(),
							GetCurrentProcessId(), GetCurrentThreadId(), g_side, channel, flags,
							static_cast<unsigned long long>(CallerRva(_ReturnAddress())), line);
					else if (flags == 3 && channel != 13)
						LOG_ERR("EngineNet", "ch=%u %s", channel, line);
					else if (flags == 2 && channel != 13)
						LOG_WARN("EngineNet", "ch=%u %s", channel, line);
				}
				g_inLog = false;
			}
			SetLastError(saved);
			return g_print.load(std::memory_order_acquire)(channel, text, flags);
		}

		void Error(int code, const char* format, va_list args)
		{
			const DWORD saved = GetLastError();
			char text[1500]{};
			if (format)
			{
				va_list copy;
				va_copy(copy, args);
				_vsnprintf_s(text, sizeof(text), _TRUNCATE, format, copy);
				va_end(copy);
				Clean(text);
			}
			OP_ERR("Engine error | code=%d | %s", code, text[0] ? text : "unknown");
			LOG_ERR("Join", "t=%llu side=%s Com_Error code=%d caller=0x%llX message='%s'", GetTickCount64(), g_side, code,
				static_cast<unsigned long long>(CallerRva(_ReturnAddress())), text);
			Snapshot("Com_Error");
			void* frames[8]{};
			const auto count = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
			for (USHORT i = 0; i < count; ++i)
				if (const auto rva = CallerRva(frames[i]))
					LOG_ERR("Join", "Com_Error frame=%u game_RVA=0x%llX", unsigned(i), static_cast<unsigned long long>(rva));
			SetLastError(saved);
			g_error.load(std::memory_order_acquire)(code, format, args);
		}

		bool Services(int controller, const char** reason, const char** detail)
		{
			const bool result = g_services.load(std::memory_order_acquire)(controller, reason, detail);
			const DWORD saved = GetLastError();
			if (!result)
			{
				char message[256]{}, key[64]{};
				PointerText(reason, message, sizeof(message));
				// Replay's early failure branches leave *detail untouched. Never read it here.
				sprintf_s(key, "services controller=%d", controller);
				if (Admit(key, 0, 0))
				{
					LOG_WARN("Join", "t=%llu side=%s client service gate refused join: controller=%d reason='%s' caller=0x%llX",
						GetTickCount64(), g_side, controller, message, static_cast<unsigned long long>(CallerRva(_ReturnAddress())));
					Snapshot("service gate failure", controller);
				}
			}
			SetLastError(saved);
			return result;
		}

		void Failed(void* info, const char* reason, uint8_t disconnectReason)
		{
			const DWORD saved = GetLastError();
			char message[512]{};
			CopyText(reason, message, sizeof(message));
			OP_WARN("Join failed | %s", message[0] ? message : "unknown reason");
			LOG_ERR("Join", "t=%llu side=%s attempt=%u join failed reason='%s' disconnect=%u state=%d caller=0x%llX", GetTickCount64(),
				g_side, g_attempt.load(), message, unsigned(disconnectReason), Read<int>(reinterpret_cast<uintptr_t>(info), -1),
				static_cast<unsigned long long>(CallerRva(_ReturnAddress())));
			Snapshot("join attempt failed");
			SetLastError(saved);
			g_failed.load(std::memory_order_acquire)(info, reason, disconnectReason);
		}

		void* Cleanup(void* activeClient, void* info)
		{
			// TU19 inlines failure producers (including HOSTUNREACH at16077A0).
			// Their common consumer1605780 owns teardown and clears the reason at+47432.
			char reason[512]{};
			CopyText(Read<const char*>(reinterpret_cast<uintptr_t>(info) + 47432), reason, sizeof(reason));
			if (matchmaking::TransportJoinFailure(reason)) ++g_transportFailures;
			const bool previous = g_retryCleanup;
			g_retryCleanup = online_services::OnNativeJoinFailure(reason);
			LOG_INFO("Join", "native join cleanup: reason='%s' pending_LAN_retry=%d", reason, g_retryCleanup);
			void* result = g_cleanup.load(std::memory_order_acquire)(activeClient, info);
			g_retryCleanup = previous;
			return result;
		}

		uintptr_t ErrorMessage(const char* message)
		{
			// Keep native connection/session/party teardown. Only defer this attempt's
			// user-facing transport error while its single LAN retry remains available.
			if (g_retryCleanup && CallerRva(_ReturnAddress()) == 0x1605852) return 0;
			return g_errorMessage.load(std::memory_order_acquire)(message);
		}

		uintptr_t RecoverFrontend()
		{
			// Native1608C40 destroys both PartyData sessions and schedules Main after
			// a failed join. A public candidate failure with a remaining LAN route is
			// not a terminal assignment failure.1605780 already closed the failed JOIN
			// connection/session via16070A0; preserve the existing menu/party until retry.
			if (g_retryCleanup && CallerRva(_ReturnAddress()) == 0x1605857)
			{
				LOG_INFO("Party", "public candidate failed; retaining frontend party and menu for LAN retry");
				return 0;
			}
			return g_recoverFrontend.load(std::memory_order_acquire)();
		}
	}

	unsigned TransportFailureSequence()
	{
		return g_transportFailures.load();
	}

	void ResponseName(uintptr_t base, int response, char* out, size_t size)
	{
		if (!out || !size) return;
		out[0] = 0;
		if (response >= 0 && response <= 76)
		{
			const auto name = Read<uintptr_t>(base + kResponseNames + size_t(response) * 8);
			if (name >= base && name < base + 0x1324b000) CopyText(reinterpret_cast<const char*>(name), out, size);
		}
		if (!out[0]) _snprintf_s(out, size, _TRUNCATE, "UNKNOWN(%d)", response);
	}

	void FormatAddress(const void* address, char* out, size_t size)
	{
		struct NetAddress
		{
			int type;
			uint8_t ip[4];
			uint16_t port, flags;
			int localNetId, handle;
		} value{};

		static_assert(sizeof(NetAddress) == 20);
		if (!safemem::ReadBytes(address, &value, sizeof(value)))
		{
			_snprintf_s(out, size, _TRUNCATE, "<unreadable>");
			return;
		}
		_snprintf_s(out, size, _TRUNCATE, "%u.%u.%u.%u port_raw=%u type=%d handle=%d netid=%d", value.ip[0], value.ip[1], value.ip[2],
			value.ip[3], value.port, value.type, value.handle, value.localNetId);
	}

	void Snapshot(const char* event, int controller)
	{
		if (!g_installed.load(std::memory_order_acquire) || controller < 0 || controller >= 8) return;
		const auto user = g_base + kUser + size_t(controller) * 0xe8;
		// These exact callees are read-only predicates used by the native gate.
		// Report the installed local-auth shim's status without entering an
		// uninitialized DW object during an early Com_Error. -1 means unavailable.
		const int status = offlineauth::IsReady(controller) ? 2 : -1;
		const int net = Read<int>(g_base + game::kDWNetStateRVA, -1);
		// Fatal startup errors can precede dvar/NetInit construction. Do not call a
		// predicate that dereferences those objects until its dependencies exist.
		int relay = -1;
		if (net >= 2 && net <= 4 && Read<uintptr_t>(g_base + 0xe36e910) && Read<uintptr_t>(g_base + 0xd46c238))
			relay = reinterpret_cast<bool (*)()>(g_base + 0x12bd870)();
		LOG_INFO("Join",
			"t=%llu pid=%lu side=%s attempt=%u %s: controller=%d signin=%d dw_status=%d net_started=%d relay_ready=%d content_allowed=%d net_state=%d net_mode=%d net_error=%d net_attempt=%d client_state=%d xuid=%llu",
			GetTickCount64(), GetCurrentProcessId(), g_side, g_attempt.load(), event, controller, Read<int>(user, -1), status,
			net >= 2 && net <= 4, relay, Read<int>(g_base + kAuth, -1) == 2, net, Read<int>(g_base + game::kDWNetModeRVA, -1),
			Read<int>(g_base + game::kDWNetErrorRVA, -1), Read<int>(g_base + game::kDWNetAttemptsRVA, -1),
			Read<int>(g_base + game::kClient0ConnectionStateRVA, -1), static_cast<unsigned long long>(Read<uint64_t>(user + 0x90)));
	}

	void BeginAttempt(const char* path)
	{
		if (!g_installed.load(std::memory_order_acquire)) return;
		++g_attempt;
		g_started = GetTickCount64();
		g_lastProgress = g_started;
		g_lastPoll = 0;
		g_lastState = g_lastClient = g_lastResponse = -1;
		// Previous failure strings stay in the global until a new failure occurs.
		g_lastReason = Read<uintptr_t>(g_base + kJoinInfo + 0xb948);
		g_watching = true;
		Snapshot(path);
		offlineauth::LogSnapshot();
	}

	void Pump()
	{
		if (!g_installed.load(std::memory_order_acquire) || !g_watching) return;
		const auto now = GetTickCount64();
		if (now - g_lastPoll < 100) return;
		g_lastPoll = now;
		const auto info = g_base + kJoinInfo;
		const int state = Read<int>(info, -1);
		const int client = Read<int>(g_base + game::kClient0ConnectionStateRVA, -1);
		const int response = Read<int>(info + 0xb938, -1);
		const auto reason = Read<uintptr_t>(info + 0xb948);
		const bool changed = state != g_lastState || client != g_lastClient || response != g_lastResponse || reason != g_lastReason;
		if (changed || now - g_lastProgress >= 5000)
		{
			char name[128]{};
			ResponseName(g_base, response, name, sizeof(name));
			LOG_INFO("Join", "t=%llu attempt=%u elapsed_ms=%llu join_state=%d client_state=%d response=%d(%s)", now, g_attempt.load(),
				now - g_started, state, client, response, name);
			if (reason && (reason != g_lastReason || (state == 0 && g_lastState > 0)))
			{
				char message[512]{};
				CopyText(reinterpret_cast<const char*>(reason), message, sizeof(message));
				LOG_ERR("Join", "attempt=%u stored failure='%s' failed_state=%d disconnect=%u", g_attempt.load(), message,
					Read<int>(info + 0xb940, -1), unsigned(Read<uint8_t>(info + 0xb93c)));
			}
			g_lastState = state;
			g_lastClient = client;
			g_lastResponse = response;
			g_lastReason = reason;
			g_lastProgress = now;
		}
		// Keep observing through connection/map load, but don't log forever.
		if (now - g_started >= 120000)
		{
			Snapshot("end of 120-second join observation");
			g_watching = false;
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (g_installed.load(std::memory_order_acquire)) return hook::Status::Installed;
		g_base = base;
		g_side = dedicated::Enabled() ? "server" : "client";
		const uint8_t relayBytes[] = { 0x48, 0x83, 0xec, 0x28, 0x48, 0x8d, 0x0d, 0xa5, 0xe9, 0x1a, 0x0c };
		uint8_t actual[sizeof(relayBytes)]{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x12bd870), actual, sizeof(actual)) ||
			memcmp(actual, relayBytes, sizeof(actual)))
			return hook::Status::NotReady;
		#define JOIN_HOOK(rva, fn, bytes, original) \
    { auto status = hook::Install(reinterpret_cast<void*>(base + rva), fn, bytes, sizeof(bytes), original); \
      if (status != hook::Status::Installed) return status; }
		JOIN_HOOK(kPrint, Print, kPrintBytes, g_print);
		JOIN_HOOK(kError, Error, kErrorBytes, g_error);
		JOIN_HOOK(kServices, Services, kJoinBytes, g_services);
		JOIN_HOOK(kJoinFailed, Failed, kJoinBytes, g_failed);
		constexpr uint8_t cleanupBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xfa };
		constexpr uint8_t errorMessageBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x81, 0xec, 0x20,
			0x08, 0x00, 0x00 };
		JOIN_HOOK(0x1605780, Cleanup, cleanupBytes, g_cleanup);
		JOIN_HOOK(0x12b3710, ErrorMessage, errorMessageBytes, g_errorMessage);
		constexpr uint8_t recoveryBytes[] = { 0x48, 0x83, 0xec, 0x28, 0xe8, 0x97, 0xf2, 0xb7, 0xff, 0x48, 0x8b, 0xc8 };
		JOIN_HOOK(0x1608c40, RecoverFrontend, recoveryBytes, g_recoverFrontend);
		#undef JOIN_HOOK
		g_installed.store(true, std::memory_order_release);
		LOG_INFO("Join", "Replay engine/network messages, service gate and join failure diagnostics installed; side=%s pid=%lu", g_side,
			GetCurrentProcessId());
		return hook::Status::Installed;
	}
}
