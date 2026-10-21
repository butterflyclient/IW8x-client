#include "gsc/compiled/compat/script_link_diag.h"
#include "gsc/compiled/compat/script_link_diag124_imports.h"
#include "common/console.h"
#include "common/deployment_profile.h"
#include "engine/safemem.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>
namespace script_link_diag
{
	namespace
	{
		// Replay 1.20.4.7623265 only. IDA: 0x13165F0 has precisely two calls to
		// 0x1318150, both for "unknown function". The latter is a five-byte thunk
		// into the native error formatter. Hooking the thunk preserves the formatter
		// and all of the linker's resolver/control flow.
		constexpr uintptr_t kUnknownFunctionError = 0x1318150;
		constexpr uintptr_t kCompileScript = 0x1317460;
		constexpr uint8_t kUnknownFunctionErrorBytes[] = { 0xE9, 0x8B, 0xFF, 0xFF, 0xFF, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC };
		constexpr uint8_t kCompileScriptBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x20, 0x57, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40 };
		constexpr uintptr_t kImageSize = 0x1324B000;
		constexpr size_t kScriptFileType = 52;
		constexpr uintptr_t kPoolCountTable = 0x23FBBA0;
		constexpr uintptr_t kPoolStrideTable = 0x2458160;
		constexpr uintptr_t kPoolPointerTable = 0xB8F5A80;

		struct ScriptFile
		{
			const char* name;
			uint32_t compressedLength;
			uint32_t sourceLength;
			uint32_t codeLength;
			uint32_t padding;
			const uint8_t* compressed;
			const uint8_t* code;
		};

		static_assert(sizeof(ScriptFile) == 40);

		using ErrorFn = void (*)(void*, const void*, const char*);
		using CompileFn = uintptr_t (*)(void*, const char*, const void*, int, int);
		std::atomic<ErrorFn> g_original{ nullptr };
		std::atomic<CompileFn> g_compile{ nullptr };
		std::atomic<unsigned> g_reports{ 0 };
		std::atomic<unsigned> g_exceptionReports{ 0 };
		uintptr_t g_base = 0;

		struct CompileEvent
		{
			uint64_t sequence = 0;
			char name[128]{};
			uint32_t depth = 0;
			uint32_t cacheKey = 0;
			uint32_t compiledHandle = 0;
			uintptr_t result = 0;
			bool returned = false;
		};

		constexpr size_t kEventCount = 1024;
		thread_local CompileEvent g_compileEvents[kEventCount]{};
		thread_local uint64_t g_compileSequence = 0;
		thread_local uint32_t g_compileDepth = 0;

		template <typename T>
		T read(uintptr_t address)
		{
			T value{};
			safemem::ReadBytes(reinterpret_cast<const void*>(address), &value, sizeof(value));
			return value;
		}

		bool selected_port()
		{
			wchar_t config[MAX_PATH]{};
			const DWORD length = GetModuleFileNameW(nullptr, config, MAX_PATH);
			if (!length || length >= MAX_PATH) return false;
			wchar_t* slash = std::wcsrchr(config, L'\\');
			if (!slash || wcscpy_s(slash + 1, MAX_PATH - size_t(slash + 1 - config), deployment::kConfigFile)) return false;
			wchar_t scene[32]{};
			GetPrivateProfileStringW(L"ui", L"frontend_scene", L"1.16", scene, 32, config);
			return _wcsicmp(scene, L"1.24") == 0 ||
					(GetPrivateProfileIntW(L"ports", L"custom_maps", 0, config) == 1);
		}

		void clean(char* text)
		{
			for (char* p = text; *p; ++p)
				if (static_cast<unsigned char>(*p) < 32 || *p == 127) *p = ' ';
		}

		struct Location
		{
			char owner[256]{};
			uint32_t codeOffset = 0;
			uint32_t poolSlot = 0;
			bool found = false;
		};

		Location locate_script_code(const void* codeAddress)
		{
			Location result{};
			const auto at = reinterpret_cast<uintptr_t>(codeAddress);
			const auto count = read<uint32_t>(g_base + kPoolCountTable + kScriptFileType * 4);
			const auto stride = read<uint32_t>(g_base + kPoolStrideTable + kScriptFileType * 4);
			const auto pool = read<uintptr_t>(g_base + kPoolPointerTable + kScriptFileType * 8);
			if (!at || !pool || !count || count > 4096 || stride != sizeof(ScriptFile)) return result;
			std::vector<ScriptFile> records(count);
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(pool), records.data(), records.size() * sizeof(ScriptFile)))
				return result;
			for (uint32_t slot = 0; slot < count; ++slot)
			{
				const ScriptFile& asset = records[slot];
				const auto begin = reinterpret_cast<uintptr_t>(asset.code);
				if (!begin || !asset.codeLength || asset.codeLength > 4 * 1024 * 1024 || at < begin || at - begin >= asset.codeLength)
					continue;
				safemem::ReadString(asset.name, result.owner, sizeof(result.owner));
				clean(result.owner);
				result.codeOffset = static_cast<uint32_t>(at - begin);
				result.poolSlot = slot;
				result.found = result.owner[0] != 0;
				return result;
			}
			return result;
		}

		const source124::Import* find_import(const Location& location, int& distance)
		{
			const source124::Import* nearest = nullptr;
			distance = 9;
			if (!location.found) return nullptr;
			for (const auto& row : source124::kImports)
			{
				if (std::strcmp(location.owner, row.owner)) continue;
				const int delta = static_cast<int>(location.codeOffset) - static_cast<int>(row.codeOffset);
				const int absolute = delta < 0 ? -delta : delta;
				if (absolute < distance)
				{
					nearest = &row;
					distance = absolute;
				}
				if (distance == 0) break;
			}
			return nearest;
		}

		uintptr_t compile_script(void* context, const char* name, const void* imports, int importCount, int recursionDepth)
		{
			const DWORD savedError = GetLastError();
			const uint64_t sequence = ++g_compileSequence;
			CompileEvent& event = g_compileEvents[sequence % kEventCount];
			event = {};
			event.sequence = sequence;
			safemem::ReadString(name, event.name, sizeof(event.name));
			clean(event.name);
			event.depth = ++g_compileDepth;
			const uintptr_t ctx = reinterpret_cast<uintptr_t>(context);
			event.cacheKey = ctx ? read<uint32_t>(ctx + 3168) : 0;
			event.compiledHandle = ctx ? read<uint32_t>(ctx + 3172) : 0;
			SetLastError(savedError);
			const auto result = g_compile.load(std::memory_order_acquire)(context, name, imports, importCount, recursionDepth);
			const DWORD nativeError = GetLastError();
			if (event.sequence == sequence)
			{
				event.result = result;
				event.returned = true;
			}
			--g_compileDepth;
			SetLastError(nativeError);
			return result;
		}

		void report_compile_order()
		{
			const uint64_t first = g_compileSequence >= kEventCount ? g_compileSequence - kEventCount + 1 : 1;
			const uint64_t recent = g_compileSequence > 64 ? g_compileSequence - 63 : 1;
			LOG_ERR("ScriptLink124", "compile trace entries=%llu depth=%u window_first=%llu",
				static_cast<unsigned long long>(g_compileSequence), g_compileDepth, static_cast<unsigned long long>(first));
			for (uint64_t seq = first; seq <= g_compileSequence; ++seq)
			{
				const CompileEvent& event = g_compileEvents[seq % kEventCount];
				if (event.sequence != seq) continue;
				const bool tracked =
					!std::strcmp(event.name, "1788") || !std::strcmp(event.name, "1806") || std::strstr(event.name, "mp_frontend5");
				if (!tracked && seq < recent) continue;
				LOG_ERR("ScriptLink124",
					"compile seq=%llu depth=%u name='%s' returned=%d result=%llu cache_key_pre=%u compiled_handle_pre=%u",
					static_cast<unsigned long long>(seq), event.depth, event.name, event.returned,
					static_cast<unsigned long long>(event.result), event.cacheKey, event.compiledHandle);
			}
		}

		void unknown_function(void* context, const void* code, const char* message)
		{
			const DWORD savedError = GetLastError();
			// Bounded to keep an unexpected linker loop from flooding the logfile.
			const unsigned report = g_reports.fetch_add(1, std::memory_order_relaxed);
			if (report < 8)
			{
				const Location location = locate_script_code(code);
				int distance = 9;
				const auto* sourceImport = find_import(location, distance);
				char text[96]{};
				safemem::ReadString(message, text, sizeof(text));
				clean(text);
				const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
				const uintptr_t callerRva = caller >= g_base && caller < g_base + kImageSize ? caller - g_base : 0;
				const uintptr_t ctx = reinterpret_cast<uintptr_t>(context);
				// A matching code pointer plus exact import offset identifies the
				// source owner/function; nearby offsets remain explicitly tentative.
				LOG_ERR("ScriptLink124",
					"native_error='%s' caller_RVA=0x%llX owner='%s' pool_slot=%u code_offset=%u exact_import=%d candidate_distance=%d target='%s' function=%u current_handle=%u script_cache_key=%u compiled_handle=%u context=%p code=%p",
					text, static_cast<unsigned long long>(callerRva), location.found ? location.owner : "<code outside ScriptFile pool>",
					location.poolSlot, location.codeOffset, sourceImport && distance == 0, sourceImport ? distance : -1,
					sourceImport ? sourceImport->target : "<unmapped>", sourceImport ? sourceImport->function : 0,
					ctx ? read<uint32_t>(ctx + 3144) : 0, ctx ? read<uint32_t>(ctx + 3168) : 0, ctx ? read<uint32_t>(ctx + 3172) : 0,
					context, code);
				if (report == 0) report_compile_order();
			}
			SetLastError(savedError);
			g_original.load(std::memory_order_acquire)(context, code, message);
		}

		LONG CALLBACK script_exception(EXCEPTION_POINTERS* exception)
		{
			if (!exception || !exception->ExceptionRecord || !exception->ContextRecord ||
				exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
				return EXCEPTION_CONTINUE_SEARCH;
			const auto* record = exception->ExceptionRecord;
			const auto* registers = exception->ContextRecord;
			const uintptr_t rip = static_cast<uintptr_t>(registers->Rip);
			if ((rip != g_base + 0x131D6DD && rip != g_base + 0x131D7EF) || g_exceptionReports.fetch_add(1, std::memory_order_relaxed) >= 4)
				return EXCEPTION_CONTINUE_SEARCH;
			const uintptr_t ret = read<uintptr_t>(static_cast<uintptr_t>(registers->Rsp) + 0x28);
			const uintptr_t address = record->NumberParameters >= 2 ? static_cast<uintptr_t>(record->ExceptionInformation[1]) : 0;
			const uintptr_t code = static_cast<uintptr_t>(registers->Rsi);
			const uintptr_t frame = static_cast<uintptr_t>(registers->R12);
			const uintptr_t stack = frame ? read<uintptr_t>(frame + 0x10) : 0;
			const Location location = locate_script_code(reinterpret_cast<const void*>(code ? code - 1 : 0));
			uint8_t codeBytes[32]{};
			const bool codeReadable =
				code >= 16 && safemem::ReadBytes(reinterpret_cast<const void*>(code - 16), codeBytes, sizeof(codeBytes));
			char codeHex[sizeof(codeBytes) * 2 + 1]{};
			if (codeReadable)
			{
				constexpr char hex[] = "0123456789ABCDEF";
				for (size_t i = 0; i < sizeof(codeBytes); ++i)
				{
					codeHex[i * 2] = hex[codeBytes[i] >> 4];
					codeHex[i * 2 + 1] = hex[codeBytes[i] & 15];
				}
			}
			LOG_ERR("ScriptLink124",
				"native_script_handle_access_violation rip_RVA=0x%llX read=%p table_RAX=%p index_RBP=%llu offset_R14=%llu ctx_RCX=%p return_RVA=0x%llX compile_sequence=%llu code_RSI=%p frame_R12=%p vm_R13=%p vm_code=%p vm_field=%u vm_arg=%u vm_stack=%p script_owner='%s' script_offset=%u script_pool_slot=%u code_window=%s",
				static_cast<unsigned long long>(rip - g_base), reinterpret_cast<void*>(address), reinterpret_cast<void*>(registers->Rax),
				static_cast<unsigned long long>(registers->Rbp), static_cast<unsigned long long>(registers->R14),
				reinterpret_cast<void*>(registers->Rcx),
				static_cast<unsigned long long>(ret >= g_base && ret < g_base + kImageSize ? ret - g_base : 0),
				static_cast<unsigned long long>(g_compileSequence), reinterpret_cast<void*>(code), reinterpret_cast<void*>(frame),
				reinterpret_cast<void*>(registers->R13), reinterpret_cast<void*>(frame ? read<uintptr_t>(frame) : 0),
				frame ? read<uint32_t>(frame + 8) : 0, frame ? read<uint32_t>(frame + 12) : 0, reinterpret_cast<void*>(stack),
				location.found ? location.owner : "<compiled code>", location.codeOffset, location.poolSlot,
				codeReadable ? codeHex : "<unreadable>");
			if (stack)
			{
				for (int slot = -2; slot <= 1; ++slot)
				{
					const uintptr_t at = stack + static_cast<intptr_t>(slot) * 16;
					LOG_ERR("ScriptLink124", "vm_stack_slot delta=%d address=%p value=%p type=%u", slot, reinterpret_cast<void*>(at),
						reinterpret_cast<void*>(read<uintptr_t>(at)), read<uint8_t>(at + 8));
				}
			}
			return EXCEPTION_CONTINUE_SEARCH;
		}
	}

	hook::Status Install(uintptr_t gameBase)
	{
		if (!selected_port() || g_original.load(std::memory_order_acquire)) return hook::Status::Installed;
		g_base = gameBase;
		const auto target = reinterpret_cast<void*>(gameBase + kUnknownFunctionError);
		uint8_t observed[sizeof(kUnknownFunctionErrorBytes)]{};
		if (!safemem::ReadBytes(target, observed, sizeof(observed)) || std::memcmp(observed, kUnknownFunctionErrorBytes, sizeof(observed)))
		{
			LOG_ERR("ScriptLink124", "Replay linker error helper RVA 0x1318150 did not match fingerprint; diagnostic disabled");
			return hook::Status::Installed;
		}
		const auto compileTarget = reinterpret_cast<void*>(gameBase + kCompileScript);
		uint8_t compileBytes[sizeof(kCompileScriptBytes)]{};
		if (safemem::ReadBytes(compileTarget, compileBytes, sizeof(compileBytes)) &&
			!std::memcmp(compileBytes, kCompileScriptBytes, sizeof(compileBytes)))
		{
			const auto compileStatus =
				hook::Install(compileTarget, compile_script, kCompileScriptBytes, sizeof(kCompileScriptBytes), g_compile);
			if (compileStatus != hook::Status::Installed) LOG_ERR("ScriptLink124", "optional compile-order trace could not be installed");
		}
		else
		{
			LOG_ERR("ScriptLink124", "compile-order helper RVA 0x1317460 did not match fingerprint");
		}
		const auto status =
			hook::Install(target, unknown_function, kUnknownFunctionErrorBytes, sizeof(kUnknownFunctionErrorBytes), g_original);
		if (status == hook::Status::Installed)
			LOG_INFO("ScriptLink124", "read-only port script diagnostic installed at RVA 0x1318150; import labels remain 1.24-specific");
		else
			LOG_ERR("ScriptLink124", "could not install optional linker diagnostic; native behavior retained");
		if (!AddVectoredExceptionHandler(1, script_exception))
			LOG_ERR("ScriptLink124", "optional native script-handle exception recorder could not be installed");
		// This probe is optional. Its failure must never prevent game startup.
		return hook::Status::Installed;
	}
}
