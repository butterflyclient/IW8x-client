#include "dedicated/assets/asset_mark_guard.h"

#include "engine/addresses.hpp"
#include "engine/safemem.h"

#include "common/console.h"

#include <windows.h>

#include <cstring>
namespace asset_mark_guard
{
	namespace
	{
		volatile LONG g_missingParents = 0;
		bool g_installed = false;

		void Jump(uint8_t* code, uintptr_t target)
		{
			const uint8_t instruction[] = { 0xff, 0x25, 0, 0, 0, 0 };
			memcpy(code, instruction, sizeof(instruction));
			memcpy(code + 6, &target, 8);
		}
	}

	hook::Status Install(uintptr_t base)
	{
		std::lock_guard lock(hook::detail::Mutex());
		if (g_installed) return hook::Status::Installed;
		// DB_MarkXAsset's missing-child diagnostic assumes its parent lookup
		// succeeded. Replay can return null during headless map teardown. Skip only
		// the diagnostic; the stock missing-child path already returns success.
		// Evidence: replay_auto_unload_crash.json / auto_host_teardown_44456.log.
		constexpr uint8_t expected[] = { 0x48, 0x8b, 0xd8, 0x0f, 0xb7, 0x48, 0x12 };
		constexpr uint8_t epilogue[] = { 0xb8, 0x01, 0, 0, 0 };
		uint8_t actual[sizeof(epilogue)]{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x11ad9a5), actual, sizeof(actual)) ||
			memcmp(actual, epilogue, sizeof(actual)))
			return hook::Status::NotReady;
		// MinHook validates both target and detour as executable at creation. The
		// bridge is not reachable until Enable below; seal it RX before enabling.
		auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		if (!code) return hook::Status::Failed;
		// The hook is inside a function: no C++ ABI call, stack adjustment or
		// register clobber on the ordinary path. Preserve the original flags too.
		const uint8_t begin[] = { 0x9c, 0x48, 0x85, 0xc0, 0x75, 28, 0x9d, 0x48, 0xb8 };
		memcpy(code, begin, sizeof(begin));
		const uintptr_t counter = reinterpret_cast<uintptr_t>(&g_missingParents);
		memcpy(code + 9, &counter, 8);
		code[17] = 0xf0;
		code[18] = 0xff;
		code[19] = 0x00; // lock inc dword [rax]
		Jump(code + 20, base + 0x11ad9a5);
		code[34] = 0x9d;
		void* trampoline = nullptr;
		const auto status = hook::detail::Prepare(reinterpret_cast<void*>(base + 0x11ad8cf), code, expected, sizeof(expected), &trampoline);
		if (status != hook::Status::Installed)
		{
			VirtualFree(code, 0, MEM_RELEASE);
			return status;
		}
		Jump(code + 35, reinterpret_cast<uintptr_t>(trampoline));
		DWORD old = 0;
		if (!VirtualProtect(code, 64, PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), code, 49) ||
			!hook::detail::Enable(reinterpret_cast<void*>(base + 0x11ad8cf)))
			return hook::Status::Failed;
		g_installed = true;
		LOG_INFO("Dedicated", "guarded missing-parent asset diagnostic at Replay RVA 0x11ad8cf");
		return hook::Status::Installed;
	}

	void Report()
	{
		static LONG previous = 0;
		const LONG count = InterlockedCompareExchange(&g_missingParents, 0, 0);
		if (count != previous)
		{
			LOG_WARN("Dedicated", "map teardown: skipped %ld null parent-asset diagnostic(s)", count - previous);
			previous = count;
		}
	}
}
