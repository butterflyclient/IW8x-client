#include "dedicated/window.h"

#include "common/console.h"

#include "thirdparty/minhook/include/MinHook.h"

#include <atomic>
#include <mutex>

#include <windows.h>
namespace dedicated_window
{
	namespace
	{
		using CreateWindowExWFn = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
		using CreateWindowExAFn = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
		using ShowWindowFn = BOOL(WINAPI*)(HWND, int);
		using ShowWindowAsyncFn = BOOL(WINAPI*)(HWND, int);
		using SetWindowPosFn = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
		using SetWindowPlacementFn = BOOL(WINAPI*)(HWND, const WINDOWPLACEMENT*);

		std::atomic<CreateWindowExWFn> g_createWindowExW{ nullptr };
		std::atomic<CreateWindowExAFn> g_createWindowExA{ nullptr };
		std::atomic<ShowWindowFn> g_showWindow{ nullptr };
		std::atomic<ShowWindowAsyncFn> g_showWindowAsync{ nullptr };
		std::atomic<SetWindowPosFn> g_setWindowPos{ nullptr };
		std::atomic<SetWindowPlacementFn> g_setWindowPlacement{ nullptr };
		std::atomic<bool> g_installed{ false };
		std::mutex g_installLock;
		ULONGLONG g_nextSweep = 0;

		bool IsConsole(HWND hwnd)
		{
			const HWND console = GetConsoleWindow();
			return console && hwnd == console;
		}

		HWND WINAPI CreateWindowExWDetour(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y, int width,
			int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
		{
			style &= ~WS_VISIBLE;
			return g_createWindowExW.load(std::memory_order_acquire)(
				exStyle, className, windowName, style, x, y, width, height, parent, menu, instance, param);
		}

		HWND WINAPI CreateWindowExADetour(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style, int x, int y, int width,
			int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param)
		{
			style &= ~WS_VISIBLE;
			return g_createWindowExA.load(std::memory_order_acquire)(
				exStyle, className, windowName, style, x, y, width, height, parent, menu, instance, param);
		}

		BOOL WINAPI ShowWindowDetour(HWND hwnd, int command)
		{
			auto original = g_showWindow.load(std::memory_order_acquire);
			return original(hwnd, IsConsole(hwnd) ? command : SW_HIDE);
		}

		BOOL WINAPI ShowWindowAsyncDetour(HWND hwnd, int command)
		{
			auto original = g_showWindowAsync.load(std::memory_order_acquire);
			return original(hwnd, IsConsole(hwnd) ? command : SW_HIDE);
		}

		BOOL WINAPI SetWindowPosDetour(HWND hwnd, HWND insertAfter, int x, int y, int cx, int cy, UINT flags)
		{
			if (!IsConsole(hwnd))
			{
				flags &= ~SWP_SHOWWINDOW;
				flags |= SWP_HIDEWINDOW;
			}
			return g_setWindowPos.load(std::memory_order_acquire)(hwnd, insertAfter, x, y, cx, cy, flags);
		}

		BOOL WINAPI SetWindowPlacementDetour(HWND hwnd, const WINDOWPLACEMENT* placement)
		{
			if (IsConsole(hwnd) || !placement) return g_setWindowPlacement.load(std::memory_order_acquire)(hwnd, placement);
			WINDOWPLACEMENT hidden = *placement;
			hidden.showCmd = SW_HIDE;
			return g_setWindowPlacement.load(std::memory_order_acquire)(hwnd, &hidden);
		}

		template <typename Fn>
		bool InstallApi(HMODULE user32, const char* name, Fn detour, std::atomic<Fn>& original)
		{
			if (original.load(std::memory_order_acquire)) return true;
			void* target = reinterpret_cast<void*>(GetProcAddress(user32, name));
			if (!target)
			{
				LOG_ERR("HeadlessWindow", "user32!%s not found", name);
				return false;
			}
			const MH_STATUS init = MH_Initialize();
			if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
			{
				LOG_ERR("HeadlessWindow", "MinHook init failed for %s: %s", name, MH_StatusToString(init));
				return false;
			}
			void* trampoline = nullptr;
			const MH_STATUS created = MH_CreateHook(target, reinterpret_cast<void*>(detour), &trampoline);
			if (created != MH_OK)
			{
				LOG_ERR("HeadlessWindow", "hook create failed for user32!%s: %s", name, MH_StatusToString(created));
				return false;
			}
			original.store(reinterpret_cast<Fn>(trampoline), std::memory_order_release);
			const MH_STATUS enabled = MH_EnableHook(target);
			if (enabled != MH_OK)
			{
				LOG_ERR("HeadlessWindow", "hook enable failed for user32!%s: %s", name, MH_StatusToString(enabled));
				original.store(nullptr, std::memory_order_release);
				MH_RemoveHook(target);
				return false;
			}
			return true;
		}

		BOOL CALLBACK HideTopLevel(HWND hwnd, LPARAM)
		{
			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);
			if (pid != GetCurrentProcessId() || IsConsole(hwnd) || !IsWindowVisible(hwnd)) return TRUE;
			auto show = g_showWindow.load(std::memory_order_acquire);
			if (show)
				show(hwnd, SW_HIDE);
			else
				::ShowWindow(hwnd, SW_HIDE);
			return TRUE;
		}

		void HideExistingWindows()
		{
			EnumWindows(&HideTopLevel, 0);
		}
	}

	hook::Status Install()
	{
		std::lock_guard<std::mutex> guard(g_installLock);
		if (g_installed.load(std::memory_order_acquire)) return hook::Status::Installed;

		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (!user32)
		{
			LOG_ERR("HeadlessWindow", "user32.dll is not loaded");
			return hook::Status::Failed;
		}

		// Visibility functions go first so anything created while the remaining
		// hooks are being installed still cannot be shown.
		if (!InstallApi(user32, "ShowWindow", &ShowWindowDetour, g_showWindow) ||
			!InstallApi(user32, "ShowWindowAsync", &ShowWindowAsyncDetour, g_showWindowAsync) ||
			!InstallApi(user32, "SetWindowPos", &SetWindowPosDetour, g_setWindowPos) ||
			!InstallApi(user32, "SetWindowPlacement", &SetWindowPlacementDetour, g_setWindowPlacement) ||
			!InstallApi(user32, "CreateWindowExW", &CreateWindowExWDetour, g_createWindowExW) ||
			!InstallApi(user32, "CreateWindowExA", &CreateWindowExADetour, g_createWindowExA))
			return hook::Status::Failed;

		g_installed.store(true, std::memory_order_release);
		HideExistingWindows();
		LOG_INFO("HeadlessWindow", "dedicated game-window visibility suppressed; mw120rproxy console remains visible");
		return hook::Status::Installed;
	}

	void MaintainHidden()
	{
		if (!g_installed.load(std::memory_order_acquire)) return;
		const ULONGLONG now = GetTickCount64();
		if (now < g_nextSweep) return;
		g_nextSweep = now + 1000;
		HideExistingWindows();
	}
}
