#include "bap.h"
#include "console.h"

#include "game.h"
#include "safemem.h"

#include "state.h"
#include "thirdparty/minhook/include/MinHook.h"

#include <windows.h>
#include <intrin.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <new>

#include "dedicated/assets/bap/internal.hpp"
namespace bap::detail
{
	static constexpr std::size_t kEventCount = 256;

	static std::array<EventSlot, kEventCount> g_events{};

	static std::atomic<std::uint64_t> g_eventSequence{ 0 };

	static std::atomic<std::uint64_t> g_eventDrops{ 0 };

	std::atomic<uintptr_t> g_traceAuxBase{ 0 };

	static std::atomic<bool> g_traceEnabled{ false };

	static std::atomic<bool> g_dumping{ false };

	static HANDLE g_crashFile = INVALID_HANDLE_VALUE;

	static void* g_crashObserver = nullptr;

	static uintptr_t g_crashGameBase = 0;

	static std::size_t g_crashGameSize = 0;

	static uintptr_t g_crashProxyBase = 0;

	static std::size_t g_crashProxySize = 0;

	// Owned exclusively by the observer. No large crash-time stack allocation.
	static std::array<BapEvent, kEventCount> g_crashEvents{};

	bool SnapshotIndex(uintptr_t address, uintptr_t base, std::size_t count, std::uint32_t& index)
	{
		if (!base || address < base || address - base >= sizeof(Entry) * count || (address - base) % sizeof(Entry)) return false;
		index = static_cast<std::uint32_t>((address - base) / sizeof(Entry));
		return true;
	}

	EntrySnapshot SnapshotEntry(const Entry* entry)
	{
		EntrySnapshot snapshot;
		snapshot.address = reinterpret_cast<uintptr_t>(entry);
		if (SnapshotIndex(snapshot.address, g_traceAuxBase.load(std::memory_order_acquire), kCapacity, snapshot.index))
			snapshot.pool = "aux";
		else if (SnapshotIndex(
					 snapshot.address, g_crashGameBase + game::kDBAssetEntryStockPoolRVA, game::kDBAssetEntryStockCount, snapshot.index))
			snapshot.pool = "stock";
		// ReadProcessMemory fails safely for bad/guarded/unmapped pointers, including
		// pointers outside both pools. Never dereference a suspect entry for tracing.
		SIZE_T read = 0;
		snapshot.readable =
			entry && ReadProcessMemory(GetCurrentProcess(), entry, &snapshot.fields, sizeof(Entry), &read) && read == sizeof(Entry);
		return snapshot;
	}

	__declspec(noinline) void RecordEventImpl(const char* name, uintptr_t caller, const Entry* entry, const char* phase,
		const Entry* related, std::uint64_t arg0, std::uint64_t arg1)
	{
		if (!g_traceEnabled.load(std::memory_order_acquire) || g_dumping.load(std::memory_order_acquire)) return;
		const DWORD savedError = GetLastError();
		BapEvent event;
		event.sequence = g_eventSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
		event.hook = name;
		event.phase = phase;
		event.caller = caller;
		event.tid = GetCurrentThreadId();
		event.entry = SnapshotEntry(entry);
		if (related) event.related = SnapshotEntry(related);
		event.arg0 = arg0;
		event.arg1 = arg1;
		auto& slot = g_events[(event.sequence - 1) % kEventCount];
		// No waiting, including when an exception interrupts a writer. A delayed
		// producer must not overwrite a newer event after the ring wraps.
		if (TryAcquireSRWLockExclusive(&slot.lock))
		{
			if (slot.event.sequence < event.sequence)
				slot.event = event;
			else
				g_eventDrops.fetch_add(1, std::memory_order_relaxed);
			ReleaseSRWLockExclusive(&slot.lock);
		}
		else
		{
			g_eventDrops.fetch_add(1, std::memory_order_relaxed);
		}
		SetLastError(savedError);
	}

	__declspec(noinline) void RecordEvent(const char* name, uintptr_t caller, const Entry* entry, const char* phase, const Entry* related,
		std::uint64_t arg0, std::uint64_t arg1)
	{
		// Inline bridges are not ordinary ABI call sites: the diagnostic's Win32
		// calls/memory copies must not introduce XMM/x87 clobbers into Replay.
		alignas(16) unsigned char fpState[512];
		_fxsave64(fpState);
		RecordEventImpl(name, caller, entry, phase, related, arg0, arg1);
		_fxrstor64(fpState);
	}

	template <typename... Args>
	void CrashLine(const char* format, Args... args)
	{
		char line[2048];
		const int size = _snprintf_s(line, sizeof(line), _TRUNCATE, format, args...);
		if (size > 0)
		{
			DWORD written = 0;
			WriteFile(g_crashFile, line, static_cast<DWORD>(size), &written, nullptr);
		}
	}

	void FormatRva(char* text, std::size_t size, uintptr_t pc, uintptr_t base, std::size_t imageSize)
	{
		if (base && pc >= base && pc - base < imageSize)
			_snprintf_s(text, size, _TRUNCATE, "0x%llX", static_cast<unsigned long long>(pc - base));
		else
			strcpy_s(text, size, "n/a");
	}

	void DumpEntry(const EntrySnapshot& snapshot)
	{
		CrashLine("entry=%016llX pool=%s ", static_cast<unsigned long long>(snapshot.address), snapshot.pool);
		if (snapshot.index != ~0u)
			CrashLine("index=%u ", snapshot.index);
		else
			CrashLine("index=n/a ");
		if (snapshot.readable)
		{
			const auto& entry = snapshot.fields;
			CrashLine("type=%u zone=%u hashNext=%u stashNext=%u flags=0x%02X header=%016llX", static_cast<unsigned>(entry.type),
				static_cast<unsigned>(entry.zoneIndex), entry.hashNext, entry.stashNext, static_cast<unsigned>(entry.flags),
				reinterpret_cast<unsigned long long>(entry.header));
		}
		else
		{
			CrashLine("type=n/a zone=n/a hashNext=n/a stashNext=n/a fields=unreadable");
		}
	}

	void DumpCrash(const EXCEPTION_POINTERS* pointers)
	{
		const auto& record = *pointers->ExceptionRecord;
		const auto& c = *pointers->ContextRecord;
		const auto end = g_eventSequence.load(std::memory_order_acquire);
		const auto begin = end > kEventCount ? end - kEventCount + 1 : 1;
		// Snapshot first, before file I/O. Only try-lock: a fault may have interrupted
		// a writer on this very thread. Missing/in-flight slots are explicitly shown.
		for (auto sequence = begin; sequence <= end; ++sequence)
		{
			const auto index = (sequence - 1) % kEventCount;
			g_crashEvents[index].sequence = 0;
			auto& slot = g_events[index];
			if (TryAcquireSRWLockShared(&slot.lock))
			{
				if (slot.event.sequence == sequence) g_crashEvents[index] = slot.event;
				ReleaseSRWLockShared(&slot.lock);
			}
		}

		char gameRva[32], proxyRva[32];
		FormatRva(gameRva, sizeof(gameRva), c.Rip, g_crashGameBase, g_crashGameSize);
		FormatRva(proxyRva, sizeof(proxyRva), c.Rip, g_crashProxyBase, g_crashProxySize);
		CrashLine("\r\n[BAP-CRASH]\r\nchance=first (observer only; game may handle)\r\ncode=%08lX\r\n"
				  "rip=%016llX game_rva=%s proxy_rva=%s\r\n",
			record.ExceptionCode, c.Rip, gameRva, proxyRva);
		const bool memoryFault = (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
			record.NumberParameters >= 2;
		if (memoryFault)
		{
			const auto operation = record.ExceptionInformation[0];
			CrashLine("access=%s operation=%llu\r\ntarget=%016llX\r\n",
				operation == 0 ? "READ"
					: operation == 1 ? "WRITE"
					: operation == 8 ? "EXECUTE"
					: "UNKNOWN",
				static_cast<unsigned long long>(operation), static_cast<unsigned long long>(record.ExceptionInformation[1]));
			if (record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR && record.NumberParameters >= 3)
				CrashLine("in_page_status=%08llX\r\n", static_cast<unsigned long long>(record.ExceptionInformation[2]));
		}
		else
		{
			CrashLine("access=n/a\r\ntarget=n/a\r\n");
		}
		CrashLine("tid=%lu pid=%lu tick_ms=%llu\r\n\r\n"
				  "RAX=%016llX\r\nRBX=%016llX\r\nRCX=%016llX\r\nRDX=%016llX\r\n"
				  "RSI=%016llX\r\nRDI=%016llX\r\nR8 =%016llX\r\nR9 =%016llX\r\n"
				  "R10=%016llX\r\nR11=%016llX\r\nR12=%016llX\r\nR13=%016llX\r\n"
				  "R14=%016llX\r\nR15=%016llX\r\nRSP=%016llX\r\nRBP=%016llX\r\n"
				  "EFLAGS=%08lX MXCSR=%08lX\r\n",
			GetCurrentThreadId(), GetCurrentProcessId(), GetTickCount64(), c.Rax, c.Rbx, c.Rcx, c.Rdx, c.Rsi, c.Rdi, c.R8, c.R9, c.R10,
			c.R11, c.R12, c.R13, c.R14, c.R15, c.Rsp, c.Rbp, c.EFlags, c.MxCsr);
		const M128A xmm[] = { c.Xmm0, c.Xmm1, c.Xmm2, c.Xmm3, c.Xmm4, c.Xmm5, c.Xmm6, c.Xmm7, c.Xmm8, c.Xmm9, c.Xmm10, c.Xmm11, c.Xmm12,
			c.Xmm13, c.Xmm14, c.Xmm15 };
		for (unsigned i = 0; i < std::size(xmm); ++i)
			CrashLine("XMM%-2u=%016llX%016llX\r\n", i, static_cast<unsigned long long>(xmm[i].High), xmm[i].Low);
		CrashLine("\r\ngame_base=%016llX proxy_base=%016llX\r\n"
				  "aux_base=%016llX aux_count=%zu stock_base=%016llX stock_count=%zu entry_size=%zu\r\n"
				  "last BAP events: (capacity=%zu latest=%llu dropped=%llu; snapshots taken at event time)\r\n",
			static_cast<unsigned long long>(g_crashGameBase), static_cast<unsigned long long>(g_crashProxyBase),
			static_cast<unsigned long long>(g_traceAuxBase.load(std::memory_order_acquire)), kCapacity,
			static_cast<unsigned long long>(g_crashGameBase + game::kDBAssetEntryStockPoolRVA), game::kDBAssetEntryStockCount,
			sizeof(Entry), kEventCount, end, g_eventDrops.load(std::memory_order_relaxed));
		for (auto sequence = begin; sequence <= end; ++sequence)
		{
			const auto& event = g_crashEvents[(sequence - 1) % kEventCount];
			if (event.sequence != sequence)
			{
				CrashLine("#%06llu <unavailable: in-flight, busy, or overwritten>\r\n", sequence);
				continue;
			}
			FormatRva(gameRva, sizeof(gameRva), event.caller, g_crashGameBase, g_crashGameSize);
			FormatRva(proxyRva, sizeof(proxyRva), event.caller, g_crashProxyBase, g_crashProxySize);
			CrashLine("#%06llu %-24s caller=%s%s caller_pc=%016llX proxy_rva=%s tid=%lu phase=%s ", event.sequence, event.hook,
				gameRva[0] == '0' ? "+" : "", gameRva, static_cast<unsigned long long>(event.caller), proxyRva, event.tid, event.phase);
			DumpEntry(event.entry);
			CrashLine(" arg0=0x%llX arg1=0x%llX", event.arg0, event.arg1);
			if (event.related.address)
			{
				CrashLine(" related={");
				DumpEntry(event.related);
				CrashLine("}");
			}
			CrashLine("\r\n");
		}
		CrashLine("[/BAP-CRASH]\r\n");
		FlushFileBuffers(g_crashFile);
	}

	LONG CALLBACK BapExceptionObserver(EXCEPTION_POINTERS* pointers)
	{
		if (!pointers || !pointers->ExceptionRecord || !pointers->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
		const DWORD code = pointers->ExceptionRecord->ExceptionCode;
		// Capture hardware faults without consuming debugger/C++ notification noise.
		if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_IN_PAGE_ERROR && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
			code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_DATATYPE_MISALIGNMENT && code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED &&
			code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW && code != EXCEPTION_STACK_OVERFLOW &&
			!(code >= EXCEPTION_FLT_DENORMAL_OPERAND && code <= EXCEPTION_FLT_UNDERFLOW))
			return EXCEPTION_CONTINUE_SEARCH;
		if (g_crashFile == INVALID_HANDLE_VALUE || g_dumping.exchange(true, std::memory_order_acq_rel)) return EXCEPTION_CONTINUE_SEARCH;
		const DWORD savedError = GetLastError();
		DumpCrash(pointers);
		SetLastError(savedError);
		g_dumping.store(false, std::memory_order_release);
		// Never change the CONTEXT or catch/repair the original game exception.
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

namespace bap
{
	using namespace detail;

	bool InitializeCrashDiagnostics(uintptr_t moduleBase)
	{
		if (!Enabled()) return true;
		std::lock_guard<std::mutex> lock(hook::detail::Mutex());
		if (g_crashObserver) return g_crashGameBase == moduleBase;
		HMODULE self = nullptr;
		wchar_t path[MAX_PATH]{};
		if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&BapExceptionObserver), &self))
			return false;
		const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
		wchar_t* slash = length && length < MAX_PATH ? wcsrchr(path, L'\\') : nullptr;
		constexpr wchar_t filename[] = L"mw120rproxy.bap_crash.log";
		if (!slash || MAX_PATH - (slash + 1 - path) < std::size(filename))
		{
			LOG_ERR("BAP", "cannot resolve the BAP crash log path beside the proxy DLL");
			return false;
		}
		wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), filename);
		g_crashGameBase = moduleBase;
		g_crashGameSize = state::imageSize.load(std::memory_order_acquire);
		g_crashProxyBase = reinterpret_cast<uintptr_t>(self);
		IMAGE_DOS_HEADER dos{};
		IMAGE_NT_HEADERS64 nt{};
		SIZE_T read = 0;
		if (ReadProcessMemory(GetCurrentProcess(), self, &dos, sizeof(dos), &read) && read == sizeof(dos) &&
			dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
			ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(g_crashProxyBase + dos.e_lfanew), &nt, sizeof(nt), &read) &&
			read == sizeof(nt) && nt.Signature == IMAGE_NT_SIGNATURE)
			g_crashProxySize = nt.OptionalHeader.SizeOfImage;
		g_crashFile =
			CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (g_crashFile == INVALID_HANDLE_VALUE)
		{
			LOG_ERR("BAP", "cannot open mw120rproxy.bap_crash.log (%lu)", GetLastError());
			return false;
		}
		// Install ahead of the generic observer; no shared rate limit or EngineNet I/O.
		g_crashObserver = AddVectoredExceptionHandler(1, &BapExceptionObserver);
		if (!g_crashObserver)
		{
			const DWORD error = GetLastError();
			CloseHandle(g_crashFile);
			g_crashFile = INVALID_HANDLE_VALUE;
			LOG_ERR("BAP", "cannot install first-chance BAP exception observer (%lu)", error);
			return false;
		}
		CrashLine("BAP first-chance observer armed pid=%lu capacity=%zu aux_count=%zu stock_count=%zu\r\n"
				  "arg0/arg1: Allocate=type/zone, FindAsset=type/name-pointer (bad-index=index), "
				  "MarkZonePairAware=group/0, LifecycleMark=type/value, CollisionMode=requested/effective\r\n",
			GetCurrentProcessId(), kEventCount, kCapacity, game::kDBAssetEntryStockCount);
		FlushFileBuffers(g_crashFile);
		g_traceEnabled.store(true, std::memory_order_release);
		LOG_INFO("BAP", "first-chance observer armed: mw120rproxy.bap_crash.log; last %zu BAP events", kEventCount);
		return true;
	}

	void TraceCollisionMode(uintptr_t caller, int requestedMode, int effectiveMode, bool completed)
	{
		if (g_traceEnabled.load(std::memory_order_acquire))
			RecordEvent("CollisionMode", caller, nullptr, completed ? "return" : "enter", nullptr, static_cast<unsigned>(requestedMode),
				static_cast<unsigned>(effectiveMode));
	}
}
