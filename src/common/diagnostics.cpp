#include "common/diagnostics.h"
#include "common/state.h"
#include "common/console.h"

#include "engine/replay_symbols.h"

#include "diagnostics/asset_context.h"
#include "client/frontend/frontend_animtree128.h"
#include "diagnostics/weapons/weapon_files.h"

#include <atomic>
#include <cstdio>
namespace
{
	HANDLE g_file = INVALID_HANDLE_VALUE;
	HANDLE g_archive = INVALID_HANDLE_VALUE;
	thread_local bool g_capturing = false;
	std::atomic<unsigned> g_records{ 0 };
	std::atomic<unsigned> g_breakpoints{ 0 };

	void WriteCapture(const char* data, DWORD size)
	{
		DWORD written = 0;
		if (g_file != INVALID_HANDLE_VALUE) WriteFile(g_file, data, size, &written, nullptr);
		if (g_archive != INVALID_HANDLE_VALUE) WriteFile(g_archive, data, size, &written, nullptr);
	}

	void WriteFrame(unsigned index, uintptr_t pc, uintptr_t sp, DWORD threadId)
	{
		MEMORY_BASIC_INFORMATION mbi{};
		VirtualQuery((void*)pc, &mbi, sizeof(mbi));
		char module[MAX_PATH]{};
		if (mbi.Type == MEM_IMAGE) GetModuleFileNameA((HMODULE)mbi.AllocationBase, module, MAX_PATH);
		char line[1024]{};
		const auto gameBase = state::moduleBase.load();
		uintptr_t symbolOffset = 0;
		const char* symbol =
			(pc >= gameBase && pc - gameBase < state::imageSize.load()) ? replaysymbols::Find(pc - gameBase, symbolOffset) : nullptr;
		const int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
			"frame[%u] tid=%lu pc=%016llX sp=%016llX module=%s base=%p rva=%llX symbol=%s+0x%llX\r\n", index, threadId,
			(unsigned long long)pc, (unsigned long long)sp, module, mbi.AllocationBase,
			(unsigned long long)(pc - (uintptr_t)mbi.AllocationBase), symbol ? symbol : "<unresolved>", symbolOffset);
		if (n > 0) WriteCapture(line, n);
	}

	void InspectWeaponSwitch(const CONTEXT* c, uintptr_t base)
	{
		// Pinned Replay C98DE1 fault through 111BA42. R13 is the original pmove
		// object: native 113BA06 reads its ps at +8 and 113BA36 its map at +380.
		// Observe only; never repair/suppress a bad state or consume the exception.
		if (!weapon_files::Enabled() || c->Rip != base + 0xC98DE1) return;
		auto read = [](uintptr_t at, void* out, SIZE_T n)
		{
			SIZE_T done = 0;
			return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at), out, n, &done) && done == n;
		};
		uintptr_t caller = 0, ps = 0, map = 0, entries = 0;
		if (!read(c->Rsp + 0x58, &caller, 8) || caller != base + 0x111BA42 || !read(c->R13 + 8, &ps, 8) || !read(c->R13 + 0x380, &map, 8) ||
			!ps || !map || !read(map + 8, &entries, 8))
			return;
		char line[1200]{};
		auto emit = [&](const char* label, uintptr_t at, size_t size)
		{
			unsigned char data[512]{};
			if (size > sizeof(data) || !read(at, data, size)) return;
			for (size_t start = 0; start < size; start += 32)
			{
				int n = sprintf_s(
					line, "WEAPON_SWITCH %s+%03llX at=%016llX bytes=", label, (unsigned long long)start, (unsigned long long)(at + start));
				for (size_t i = start; i < size && i < start + 32; ++i)
					n += sprintf_s(line + n, sizeof(line) - n, "%02X", data[i]);
				n += sprintf_s(line + n, sizeof(line) - n, "\r\n");
				WriteCapture(line, n);
			}
		};
		emit("ps_header", ps, 0x180);
		emit("ps_weapon", ps + 0x500, 0x200);
		emit("pmove", c->R13, 0x60);
		const size_t offsets[]{ 0x6D8, 0x6F8 };
		for (auto offset : offsets)
		{
			uint16_t handle = 0, index = 0;
			uint32_t count = 0;
			uintptr_t def = 0;
			if (!read(ps + offset, &handle, 2) || handle >= 4096 || !read(entries + 62 * handle, &index, 2) || index != handle) continue;
			emit("weapon_map_entry", entries + 62 * handle, 62);
			if (!read(entries + 62 * handle + 2, &index, 2) || !read(base + 0xC6EB724, &count, 4) || index > count || index >= 550 ||
				!read(base + 0xC6EC870 + 8 * index, &def, 8) || !def)
				continue;
			emit("complete_definition", def, 0x120);
			uintptr_t nested = 0;
			if (read(def + 8, &nested, 8) && nested)
			{
				emit("nested_models", nested, 0xC0);
				emit("nested_scalars", nested + 0x188, 0x70);
			}
		}
	}

	bool UnwindFrames(const CONTEXT* source, DWORD threadId = GetCurrentThreadId())
	{
		bool capturedOtherThread = false;
		// A bad stack must not cause this diagnostic to consume the game exception.
		__try
		{
			CONTEXT context = *source;
			for (unsigned frame = 0; frame < 32; ++frame)
			{
				// Replay D85BD0 saves the timed-out thread's context before raising
				// 1337 on the watchdog thread. Only that exact return site admits
				// the saved context, avoiding stale globals from another exception.
				if (context.Rip == state::moduleBase.load() + 0xD85D0F) capturedOtherThread = true;
				WriteFrame(frame, context.Rip, context.Rsp, threadId);
				const DWORD64 previousSp = context.Rsp;
				DWORD64 imageBase = 0;
				const PRUNTIME_FUNCTION function = context.Rip ? RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr) : nullptr;
				if (function)
				{
					void* handlerData = nullptr;
					DWORD64 establisher = 0;
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
				}
				else
				{
					SIZE_T read = 0;
					DWORD64 caller = 0;
					if (!ReadProcessMemory(GetCurrentProcess(), (void*)context.Rsp, &caller, sizeof(caller), &read)) break;
					context.Rip = caller;
					context.Rsp += sizeof(caller);
				}
				if (!context.Rip || context.Rsp <= previousSp) break;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
		return capturedOtherThread;
	}

	void InspectWatchdogContext(uintptr_t base)
	{
		// Original Replay D85C3B/D85C42/D85CBC and D85D09. The game has
		// already suspended and captured the thread; this observer only reads it.
		auto read = [](uintptr_t address, void* out, SIZE_T bytes)
		{
			SIZE_T done = 0;
			return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), out, bytes, &done) && done == bytes;
		};
		unsigned char valid = 0;
		unsigned index = ~0u;
		CONTEXT context{};
		HANDLE thread = nullptr;
		if (!base || !read(base + 0x5D0A063, &valid, sizeof(valid)) || !valid || !read(base + 0x29D4760, &index, sizeof(index)) ||
			index >= 28 || !read(base + 0x5D0A0B0, &context, sizeof(context)) ||
			!read(base + 0xD57F650 + index * sizeof(HANDLE), &thread, sizeof(thread)))
			return;
		const DWORD tid = GetThreadId(thread);
		char line[512]{};
		const int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
			"WATCHDOG captured_native_thread index=%u tid=%lu RIP=%016llX RSP=%016llX "
			"RCX=%016llX RDX=%016llX R8=%016llX R9=%016llX\r\n",
			index, tid, context.Rip, context.Rsp, context.Rcx, context.Rdx, context.R8, context.R9);
		if (n > 0) WriteCapture(line, n);
		UnwindFrames(&context, tid);
	}

	void InspectStartupCallback(const CONTEXT* c)
	{
		DWORD64 caller = 0;
		SIZE_T read = 0;
		if (!ReadProcessMemory(GetCurrentProcess(), (void*)c->Rsp, &caller, sizeof(caller), &read)) return;
		const uintptr_t base = state::moduleBase.load();
		if (caller != base + 0x3153FE) return;
		// In sub_1403061A0 the indirect call at +0xF257 uses [rsp+0x1EC8].
		// At the exception, the call's return address adds eight bytes to that offset.
		const size_t offsets[] = { 0x15A8, 0x1ED0, 0x1188, 0xBB0, 0x480 };
		char line[1024]{};
		for (size_t offset : offsets)
		{
			DWORD64 value = 0;
			if (!ReadProcessMemory(GetCurrentProcess(), (void*)(c->Rsp + offset), &value, sizeof(value), &read)) continue;
			const int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "bootstrap_stack+%llX=%016llX\r\n", (unsigned long long)offset, value);
			if (n > 0) WriteCapture(line, n);
			if (offset == 0x1ED0 && value)
			{
				WriteFrame(99, value, c->Rsp, GetCurrentThreadId());
				unsigned char bytes[32]{};
				if (ReadProcessMemory(GetCurrentProcess(), (void*)value, bytes, sizeof(bytes), &read))
				{
					int pos = sprintf_s(line, "callback_bytes=");
					for (size_t i = 0; i < read; ++i)
						pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ", bytes[i]);
					pos += sprintf_s(line + pos, sizeof(line) - pos, "\r\n");
					WriteCapture(line, pos);
				}
			}
		}
	}

	void InspectFrontend128RenderSlot(const CONTEXT* c, uintptr_t base)
	{
		// The 1.28 test scene can reach Replay's render-command worker with a
		// missing type-3 resource binding. This records the command that selected
		// the null table entry without changing the engine's exception behavior.
		if (!frontend_animtree128::Active() || c->Rip != base + 0x1982C7B || c->R9 != 0x20) return;
		SIZE_T read = 0;
		uintptr_t record = 0;
		if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(c->Rsp + 8), &record, sizeof(record), &read) ||
			read != sizeof(record))
			return;
		unsigned char command[6]{};
		const bool commandRead = record &&
			ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(record), command, sizeof(command), &read) &&
			read == sizeof(command);
		char line[512]{};
		if (!commandRead)
		{
			const int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "frontend128_render_slot record_unreadable=%016llX table=%016llX\r\n",
				static_cast<unsigned long long>(record), c->R12);
			if (n > 0) WriteCapture(line, n);
			return;
		}
		const unsigned value = static_cast<unsigned>(command[2]) | static_cast<unsigned>(command[3]) << 8;
		const unsigned slot = static_cast<unsigned>(command[4]) | static_cast<unsigned>(command[5]) << 8;
		uintptr_t resource = 0;
		bool resourceRead = false;
		// Replay's next render-state class starts at +0x2460: this class has
		// exactly (0x2460 - 0x22B8) / 8 = 53 pointer slots.
		if (c->R12 && slot < 53)
		{
			const uintptr_t address = c->R12 + 0x22B8 + slot * sizeof(uintptr_t);
			resourceRead = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), &resource, sizeof(resource), &read) &&
				read == sizeof(resource);
		}
		const int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
			"frontend128_render_slot record=%016llX bytes=%02X %02X %02X %02X %02X %02X "
			"type=%u flags=%u value=%u slot=%u table=%016llX resource=%016llX read=%d\r\n",
			static_cast<unsigned long long>(record), command[0], command[1], command[2], command[3], command[4], command[5], command[0],
			command[1], value, slot, c->R12, static_cast<unsigned long long>(resource), resourceRead ? 1 : 0);
		if (n > 0) WriteCapture(line, n);
	}

	LONG CALLBACK ExceptionObserver(EXCEPTION_POINTERS* pointers)
	{
		const auto* record = pointers->ExceptionRecord;
		const DWORD savedError = GetLastError();
		if (g_capturing) return EXCEPTION_CONTINUE_SEARCH;
		const DWORD code = record->ExceptionCode;
		if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_IN_PAGE_ERROR &&
			code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
			code != EXCEPTION_BREAKPOINT && code != 0x1337 && code != 0x1338)
			return EXCEPTION_CONTINUE_SEARCH;
		if (g_file == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
		if (code == EXCEPTION_BREAKPOINT ? g_breakpoints.fetch_add(1) >= 16 : g_records.fetch_add(1) >= 32)
			return EXCEPTION_CONTINUE_SEARCH;
		g_capturing = true;
		const auto* c = pointers->ContextRecord;
		MEMORY_BASIC_INFORMATION mbi{};
		VirtualQuery(record->ExceptionAddress, &mbi, sizeof(mbi));
		char line[2048]{};
		const auto base = state::moduleBase.load();
		const auto size = state::imageSize.load();
		const bool inGame = c->Rip >= base && c->Rip - base < size;
		const int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
			"FIRST-CHANCE (may be handled by game) code=%08lX tid=%lu address=%p allocation=%p game_rva=%llX\r\n"
			"RIP=%016llX RSP=%016llX RBP=%016llX RCX=%016llX RDX=%016llX R8=%016llX R9=%016llX RAX=%016llX\r\n"
			"RBX=%016llX RSI=%016llX RDI=%016llX R10=%016llX R11=%016llX R12=%016llX R13=%016llX R14=%016llX R15=%016llX EFLAGS=%08lX\r\n"
			"operation=%llu referenced_address=%016llX hook_state=%d bool_calls=%llu variant_calls=%llu\r\n",
			code, GetCurrentThreadId(), record->ExceptionAddress, mbi.AllocationBase, inGame ? c->Rip - base : ~0ULL, c->Rip, c->Rsp,
			c->Rbp, c->Rcx, c->Rdx, c->R8, c->R9, c->Rax, c->Rbx, c->Rsi, c->Rdi, c->R10, c->R11, c->R12, c->R13, c->R14, c->R15, c->EFlags,
			record->NumberParameters > 0 ? record->ExceptionInformation[0] : 0,
			record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0, state::hookState.load(), state::boolDvarsSeen.load(),
			state::dvarsSeen.load());
		if (n > 0) WriteCapture(line, n);
		InspectFrontend128RenderSlot(c, base);
		InspectWeaponSwitch(c, base);
		const auto& asset = assetcontext::current;
		if (asset.active)
		{
			const int count = _snprintf_s(line, sizeof(line), _TRUNCATE,
				"ACTIVE ASSET type=%d name='%s' asset=%016llX phase=%s HavokData=%016llX HavokBytes=%u\r\n", asset.type, asset.name,
				asset.asset, asset.phase, asset.shapeData, asset.shapeBytes);
			if (count > 0) WriteCapture(line, count);
		}
		unsigned char instruction[16]{};
		SIZE_T instructionBytes = 0;
		if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(c->Rip), instruction, sizeof(instruction), &instructionBytes))
		{
			int pos = sprintf_s(line, "fault_instruction_bytes=");
			for (size_t i = 0; i < instructionBytes; ++i)
				pos += sprintf_s(line + pos, sizeof(line) - pos, "%02X ", instruction[i]);
			pos += sprintf_s(line + pos, sizeof(line) - pos, "\r\n");
			WriteCapture(line, pos);
		}
		const bool capturedOtherThread = UnwindFrames(c);
		if (code == 0x1337 && capturedOtherThread) InspectWatchdogContext(base);
		InspectStartupCallback(c);
		unsigned long long stack[32]{};
		SIZE_T read = 0;
		if (ReadProcessMemory(GetCurrentProcess(), (void*)c->Rsp, stack, sizeof(stack), &read))
		{
			for (size_t i = 0; i < read / sizeof(stack[0]); ++i)
			{
				const int length =
					_snprintf_s(line, sizeof(line), _TRUNCATE, "stack+%03llX=%016llX\r\n", (unsigned long long)i * 8, stack[i]);
				if (length > 0) WriteCapture(line, length);
			}
		}
		FlushFileBuffers(g_file);
		if (g_archive != INVALID_HANDLE_VALUE) FlushFileBuffers(g_archive);
		g_capturing = false;
		SetLastError(savedError);
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

namespace diagnostics
{
	void Initialize(HMODULE self)
	{
		wchar_t path[MAX_PATH]{};
		const DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
		if (!n || n >= MAX_PATH) return;
		wchar_t* slash = wcsrchr(path, L'\\');
		if (!slash) return;
		wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"mw120rproxy.exceptions.log");
		g_file =
			CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (g_file == INVALID_HANDLE_VALUE) return;
		SYSTEMTIME stamp{};
		GetSystemTime(&stamp);
		wchar_t archiveName[128]{};
		swprintf_s(archiveName, L"mw120rproxy.crash.%04u%02u%02u-%02u%02u%02u-%lu.log", stamp.wYear, stamp.wMonth, stamp.wDay, stamp.wHour,
			stamp.wMinute, stamp.wSecond, GetCurrentProcessId());
		wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), archiveName);
		g_archive =
			CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (!AddVectoredExceptionHandler(1, &ExceptionObserver))
		{
			const DWORD error = GetLastError();
			CloseHandle(g_file);
			g_file = INVALID_HANDLE_VALUE;
			if (g_archive != INVALID_HANDLE_VALUE) CloseHandle(g_archive);
			g_archive = INVALID_HANDLE_VALUE;
			LOG_WARN("Diagnostics", "exception observer unavailable (%lu)", error);
		}
	}
}
