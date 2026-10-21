#include "link_diagnostics.hpp"

#include "common/console.h"

#include "engine/image.hpp"
#include "engine/inline_hook.h"

#include "thirdparty/minhook/include/MinHook.h"

#include "engine/safemem.h"

#include "gsc/internal/state.hpp"

#include <algorithm>
#include <charconv>
#include <array>
#include <atomic>
#include <mutex>

#include <intrin.h>
extern "C"
{
	void* iw8x_native_error{};
	void iw8x_error_trace(int, const char*, ...);

	void iw8x_record_native_error(int kind, const char* format, std::uint64_t first, std::uint64_t second, const void* caller)
	{
		const auto saved_error = GetLastError();
		const auto caller_rva = game::rva(caller);
#ifdef IW8X_ISLAND_PORT_PROBE
		if (kind == 5)
		{
			const auto* context = game::at<std::uint8_t>(game::addresses::gsc::kServerContextRva);
			for (const auto offset : { std::size_t{ 0x34D8 }, std::size_t{ 0xB9B0 }, std::size_t{ 0xB9C0 }, std::size_t{ 0xB9C8 } })
			{
				std::uintptr_t pointer{};
				std::uintptr_t values[8]{};
				if (safemem::ReadBytes(context + offset, &pointer, sizeof(pointer)) &&
					safemem::ReadBytes(reinterpret_cast<void*>(pointer), values, sizeof(values)))
				{
					LOG_ERR("GSCFrame", "context[0x%zX]=%p", offset, reinterpret_cast<void*>(pointer));
					for (unsigned i = 0; i < std::size(values); ++i)
						LOG_ERR("GSCFrame", "word[%u]=0x%llX", i, values[i]);
				}
			}
		}
#endif
		LOG_ERR("GSCLink",
			"native Com_Error RVA=0x12AB4A0 kind=%d format='%s' arg0=%llu (0x%llX) arg1=0x%llX caller_RVA=0x%llX source='%s' tid=%lu", kind,
			format ? format : "<none>", first, first, second, caller_rva < game::image_size() ? caller_rva : 0,
			gsc::link_diagnostics::current_source.name.data(), GetCurrentThreadId());
		// The native animation-link failure forwards a single preformatted string.
		// Read it only for that proven format; numeric error arguments are not
		// interpreted as pointers. This observer never affects error handling.
		if (format && (std::strcmp(format, "\x1f%s") == 0 || std::strcmp(format, "%s") == 0))
		{
			char message[1500]{};
			if (safemem::ReadString(reinterpret_cast<const char*>(first), message, sizeof(message)))
				LOG_ERR("GSCLink", "native error text='%s'", message);
		}
		void* frames[16]{};
		const auto count = CaptureStackBackTrace(0, 16, frames, nullptr);
		for (USHORT i = 0; i < count; ++i)
		{
			const auto rva = game::rva(frames[i]);
			LOG_ERR("GSCLink", "error frame=%u pc=%p game_RVA=0x%llX", unsigned(i), frames[i], rva < game::image_size() ? rva : 0);
		}
		SetLastError(saved_error);
	}
}

namespace gsc::link_diagnostics
{
	namespace
	{
		using LinkFunction = std::uintptr_t (*)(void*, std::uint32_t, std::uint32_t, std::uint8_t*);
		void* link_target{};
		void* link_trampoline{};
		void* error_target{};
		void* error_trampoline{};
		std::mutex install_mutex;
		void* fault_observer{};
		std::atomic<bool> fault_reported{};
		LinkFunction native_link{};
#ifdef IW8X_ISLAND_PORT_PROBE
		using runtime_error_fn = void (*)(void*, std::uintptr_t, unsigned, const char*, const char*);
		runtime_error_fn original_runtime_error{};
		void* runtime_error_target{};
		void* runtime_error_trampoline{};
		constexpr std::uint8_t runtime_error_entry[]{ 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0xb8, 0x48, 0x20, 0x00, 0x00 };

		void observe_runtime_error(void* context, const std::uintptr_t position, unsigned index, const char* message, const char* dialog)
		{
			struct report
			{
				std::uintptr_t position{};
				std::array<char, 512> message{};
			};
			static std::mutex report_mutex;
			static std::array<report, 64> reports{};
			static std::size_t report_count{};
			char error[512]{};
			(void)safemem::ReadString(message, error, sizeof(error));
			bool record{};
			{
				const std::lock_guard lock(report_mutex);
				const auto end = reports.begin() + report_count;
				const auto found = std::find_if(reports.begin(), end, [&](const auto& entry)
				{
					return entry.position == position && std::strcmp(entry.message.data(), error) == 0;
				});
				if (found == end && report_count < reports.size())
				{
					auto& entry = reports[report_count++];
					entry.position = position;
					std::memcpy(entry.message.data(), error, sizeof(error));
					record = true;
				}
			}
			if (!record)
			{
				original_runtime_error(context, position, index, message, dialog);
				return;
			}
			LOG_ERR("GSCRuntime", "native error position=%p index=%u context=%p message='%s'", reinterpret_cast<void*>(position), index, context, error);
				std::string exact_script;
				std::size_t exact_offset{};
				if (gsc::DescribeIslandCodeSite(position, exact_script, exact_offset))
					LOG_ERR("GSCRuntime", "linked Island ScriptFile=%s bytecode_offset=%zu", exact_script.c_str(), exact_offset);
			std::uint8_t bytes[128]{};
			if (position >= 64 && safemem::ReadBytes(reinterpret_cast<void*>(position - 64), bytes, sizeof(bytes)))
			{
				constexpr char digits[] = "0123456789abcdef";
				char text[257]{};
				for (unsigned i = 0; i < std::size(bytes); ++i)
				{
					text[2 * i] = digits[bytes[i] >> 4];
					text[2 * i + 1] = digits[bytes[i] & 15];
				}
				LOG_ERR("GSCRuntime", "position window start=%p bytes=%s", reinterpret_cast<void*>(position - 64), text);
			}
			original_runtime_error(context, position, index, message, dialog);
		}
#endif
		FindVariable find_variable{};
		using SLToString = const char* (*)(std::uint32_t);
		SLToString sl_string{};

		LONG CALLBACK ObserveValueFault(EXCEPTION_POINTERS* exception)
		{
			if (!exception || !exception->ExceptionRecord || !exception->ContextRecord ||
				exception->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
				game::rva(exception->ExceptionRecord->ExceptionAddress) != 0x131D7EF ||
				fault_reported.exchange(true, std::memory_order_relaxed))
				return EXCEPTION_CONTINUE_SEARCH;

			const auto& registers = *exception->ContextRecord;
			LOG_ERR("GSCFault", "RemoveRefToValue string fault: value=0x%llX target=%p tid=%lu",
				registers.R8,
				reinterpret_cast<void*>(exception->ExceptionRecord->ExceptionInformation[1]), GetCurrentThreadId());
			LOG_ERR("GSCFault", "registers: rcx=%llX rdx=%llX r8=%llX r9=%llX rsi=%llX rdi=%llX rbx=%llX r12=%llX r13=%llX r14=%llX r15=%llX",
				registers.Rcx, registers.Rdx, registers.R8, registers.R9, registers.Rsi, registers.Rdi, registers.Rbx,
				registers.R12, registers.R13, registers.R14, registers.R15);
			std::uintptr_t stack[24]{};
			if (safemem::ReadBytes(reinterpret_cast<void*>(registers.Rsp), stack, sizeof(stack)))
				for (unsigned i = 0; i < std::size(stack); ++i)
				{
					const auto rva = game::rva(reinterpret_cast<void*>(stack[i]));
					if (rva < game::image_size()) LOG_ERR("GSCFault", "stack[%u] game_RVA=0x%llX", i, rva);
				}

			const auto* context = game::at<std::uint8_t>(game::addresses::gsc::kServerContextRva);
			for (const auto offset : { std::size_t{ 0xB9C0 }, std::size_t{ 0xB9C8 } })
			{
				std::uintptr_t pointer{};
				std::uint64_t words[8]{};
				if (safemem::ReadBytes(context + offset, &pointer, sizeof(pointer)) && pointer &&
					safemem::ReadBytes(reinterpret_cast<void*>(pointer), words, sizeof(words)))
					LOG_ERR("GSCFault", "context+0x%zX pointer=%p words=%llX,%llX,%llX,%llX,%llX,%llX,%llX,%llX",
						offset, reinterpret_cast<void*>(pointer), words[0], words[1], words[2], words[3], words[4], words[5], words[6], words[7]);
			}
			return EXCEPTION_CONTINUE_SEARCH;
		}

		struct Operation
		{
			void* context{};
			std::uint32_t module{}, function{};
			std::uintptr_t caller{}, operand{};
			ExportState state{};
		};

		thread_local std::array<Operation, 8> recent{};
		thread_local std::size_t count{};

		std::string_view ModuleName(const char* raw)
		{
			if (!raw) return "<none>";
			const std::string_view value(raw);
			std::uint32_t token{};
			const auto result = std::from_chars(value.data(), value.data() + value.size(), token);
			if (result.ec == std::errc{} && result.ptr == value.data() + value.size())
				return iw8x::gsc::internal::script_function_name(token);
			return value;
		}

		void PrintOperation(const char* label, const Operation& op)
		{
			const auto* raw = sl_string(op.module);
			const auto module = ModuleName(raw);
			const auto function = iw8x::gsc::internal::script_function_name(op.function);
			LOG_ERR("GSCLink",
				"%s: module='%s' catalog_name='%.*s' SL_id=%u function_token=0x%X catalog_name='%.*s' context=%p caller_RVA=0x%llX operand=%p state=%s",
				label, raw ? raw : "<none>", int(module.size()), module.data(), op.module, op.function, int(function.size()),
				function.data(), op.context, static_cast<unsigned long long>(op.caller), reinterpret_cast<void*>(op.operand),
				op.state == ExportState::missing ? "missing export"
					: op.state == ExportState::present ? "resolved"
					: "pending module");
		}

		std::uintptr_t Link(void* context, std::uint32_t module, std::uint32_t function, std::uint8_t* operand)
		{
			const Operation op{ context, module, function, game::rva(_ReturnAddress()), reinterpret_cast<std::uintptr_t>(operand),
				Inspect(context, module, function, find_variable) };
			if (op.state == ExportState::missing)
			{
				const auto& source = current_source;
				LOG_ERR("GSCLink",
					"Scr_LinkFunction RVA=0x1316800: native missing-export branch at 0x13168E4 calls Com_Error at 0x13168F2; asset_type=52 source='%s' header=%p bytecode=%p bytes=%d operand_offset=%lld tid=%lu",
					source.name.data(), source.asset, reinterpret_cast<void*>(source.bytecode), source.bytecode_size,
					static_cast<long long>(op.operand) - static_cast<long long>(source.bytecode), GetCurrentThreadId());
				PrintOperation("first failing lookup", op);
				const auto* bytes = static_cast<const std::uint8_t*>(context);
				const auto field = [&](std::size_t offset)
				{
					std::uint32_t value;
					std::memcpy(&value, bytes + offset, sizeof(value));
					return value;
				};
				LOG_ERR("GSCLink", "loaded_object=%u exports_object=%u pending_object=%u loading=%u vm_frames=%u checksum=0x%X",
					field(game::addresses::gsc::kLoadedScriptsOffset), field(game::addresses::gsc::kExportsOffset),
					field(game::addresses::gsc::kPendingScriptsOffset), unsigned(bytes[game::addresses::gsc::kLoadingScriptsOffset]),
					field(game::addresses::gsc::kVMFrameCountOffset), field(game::addresses::gsc::kProgramChecksumOffset));
				const auto begin = count > recent.size() ? count - recent.size() : 0;
				for (auto i = begin; i < count; ++i)
					if (recent[i % recent.size()].context == context)
						PrintOperation("previous completed lookup", recent[i % recent.size()]);
				void* stack[16]{};
				const auto frames = CaptureStackBackTrace(0, 16, stack, nullptr);
				for (USHORT i = 0; i < frames; ++i)
				{
					const auto rva = game::rva(stack[i]);
					LOG_ERR("GSCLink", "frame=%u pc=%p game_RVA=0x%llX", unsigned(i), stack[i],
						static_cast<unsigned long long>(rva < game::image_size() ? rva : 0));
				}
			}
			// Keep the stock success/pending/error paths and native relative fixup.
			// Com_Error may longjmp, so all retained diagnostic records are TLS values,
			// never pointers to stack objects whose destructors it would bypass.
			const auto result = native_link(context, module, function, operand);
#ifdef IW8X_ISLAND_PORT_PROBE
			// Observe the native signed24 fixup range; never change its result.
			static const bool observe_range = []
			{
				char enabled[2]{};
				return GetEnvironmentVariableA("IW8X_ISLAND_NUMERIC_CANONICAL", enabled, sizeof(enabled)) == 1 && enabled[0] == '1';
			}();
			if (observe_range && op.state == ExportState::present)
			{
				using namespace game::addresses::gsc;
				const auto* bytes = static_cast<const std::uint8_t*>(context);
				const auto* children = *reinterpret_cast<const std::uint8_t* const*>(bytes + kChildrenOffset);
				const auto file = find_variable(context, *reinterpret_cast<const std::uint32_t*>(bytes + kExportsOffset), module);
				const auto exports = *reinterpret_cast<const std::uint32_t*>(children + kChildStride * file + kChildObjectOffset);
				const auto entry = find_variable(context, exports, function);
				const auto target = *reinterpret_cast<const std::uint32_t*>(children + kChildStride * entry + kChildObjectOffset);
				const auto relative = static_cast<std::int32_t>(target - static_cast<std::uint32_t>(op.operand));
				static unsigned reported{};
				if ((relative < -0x400000 || relative > 0x3FFFFF) && reported++ < 16)
					LOG_ERR("GSCLink", "native signed24 fixup overflow: source='%s' module=%u function=%u operand=%p target_low=0x%X offset=%d",
						current_source.name.data(), module, function, operand, target, relative);
			}
#endif
			recent[count++ % recent.size()] = op;
			return result;
		}
	}

	void Install(std::uintptr_t base)
	{
		std::lock_guard lock(install_mutex);
		if (!fault_observer) fault_observer = AddVectoredExceptionHandler(1, &ObserveValueFault);
		std::lock_guard hook_lock(hook::detail::Mutex());
		using namespace game::addresses::gsc;
		if (!link_trampoline)
		{
			link_target = reinterpret_cast<void*>(base + kScrLinkFunctionRva);
			if (hook::detail::Prepare(link_target, reinterpret_cast<void*>(&Link), Scr_LinkFunction_entry, sizeof(Scr_LinkFunction_entry),
					&link_trampoline) != hook::Status::Installed)
				return;
		}
		find_variable = reinterpret_cast<FindVariable>(base + game::kScrFindVariableRVA);
		sl_string = reinterpret_cast<SLToString>(base + game::addresses::gsc_api::SL_ConvertToString);
		native_link = reinterpret_cast<LinkFunction>(link_trampoline);
		const std::array link_targets{ link_target };
		if (!hook::detail::EnableBatch(link_targets)) return;
		// Preserve the entire variadic boundary, including stack/float arguments.
		if (!error_trampoline)
		{
			error_target = reinterpret_cast<void*>(base + kComErrorRva);
			if (hook::detail::Prepare(error_target, reinterpret_cast<void*>(&iw8x_error_trace), Com_Error_entry, sizeof(Com_Error_entry),
					&error_trampoline) != hook::Status::Installed)
				return;
		}
		iw8x_native_error = error_trampoline;
		const std::array error_targets{ error_target };
		(void)hook::detail::EnableBatch(error_targets);
#ifdef IW8X_ISLAND_PORT_PROBE
		if (!runtime_error_trampoline)
		{
			runtime_error_target = reinterpret_cast<void*>(base + 0x1322940);
			if (hook::detail::Prepare(runtime_error_target, reinterpret_cast<void*>(&observe_runtime_error), runtime_error_entry,
					sizeof(runtime_error_entry), &runtime_error_trampoline) != hook::Status::Installed) return;
		}
		original_runtime_error = reinterpret_cast<runtime_error_fn>(runtime_error_trampoline);
		const std::array runtime_error_targets{ runtime_error_target };
		(void)hook::detail::EnableBatch(runtime_error_targets);
#endif
	}
} // namespace gsc::link_diagnostics
