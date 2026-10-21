#include "client/loader/component_loader.hpp"
#include "engine/imports.hpp"
#include "engine/asset_pools.hpp"
#include <condition_variable>
#include <chrono>

#include <windows.h>

#include "engine/addresses.hpp"

#include <atomic>
#include <exception>
#include <mutex>

#include "client/autostart.h"
#include "client/online_config.h"
#include "dedicated/auto_server.h"
#include "dedicated/assets/bap.h"

#include "common/console.h"

#include "dedicated/dedicated.h"

#include "common/deployment_profile.h"

#include "dedicated/window.h"

#include "common/diagnostics.h"
#include "common/embedded_assets.h"

#include "dedicated/headless/headless.h"
#include "diagnostics/night/night_files.h"
#include "diagnostics/night/night_entry_pool.h"
#include "network/native_network.h"


#include "network/online_services.h"
#include "proxy/xinput.h"

#include "bootstrap/startup_compat.h"

#include "common/state.h"

#include "bootstrap/trigger.h"

namespace
{
	uintptr_t game_base = 0;
	std::recursive_mutex install_mutex;
	bool unpack_complete = false;
	bool trigger_armed = false;
	bool gdi_compatibility = false;
	std::atomic<bool> splash_seen{ false };
	std::atomic<bool> ready{ false };
	std::mutex boot_mutex;
	std::condition_variable boot_wakeup;

	void shutdown_components()
	{
		std::lock_guard<std::recursive_mutex> lock(install_mutex);
		component_loader::pre_destroy();
	}

	bool dedicated_requested()
	{
		const auto* command = GetCommandLineW();
		if (!command) return false;
		for (const auto* flag = command; (flag = wcsstr(flag, L"--dedi")) != nullptr; flag += 6)
		{
			const auto before = flag == command ? L' ' : flag[-1];
			const auto after = flag[6];
			if ((before == L' ' || before == L'\t' || before == L'"') &&
				(!after || after == L' ' || after == L'\t' || after == L'"')) return true;
		}
		return false;
	}

	bool check_target(uintptr_t base, uint32_t& size)
	{
		wchar_t path[MAX_PATH]{};
		if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
		const wchar_t* name = wcsrchr(path, L'\\');
		if (!name || _wcsicmp(name + 1, L"game_dx12_ship_replay.exe") != 0) return false;

		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
			nt->FileHeader.TimeDateStamp != 0x5e9baf80 || nt->OptionalHeader.SizeOfImage != 0x1324b000)
			return false;

		size = nt->OptionalHeader.SizeOfImage;
		return true;
	}

	void clear_previous_run_crash_markers()
	{
		wchar_t path[MAX_PATH]{};
		if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;

		wchar_t* tail = wcsrchr(path, L'\\');
		if (!tail) return;

		for (const auto* marker : { L"__game_dx12_ship_replay", L"__ModernWarfare" })
		{
			wcscpy_s(tail + 1, MAX_PATH - size_t(tail + 1 - path), marker);
			WIN32_FILE_ATTRIBUTE_DATA file{};
			if (GetFileAttributesExW(path, GetFileExInfoStandard, &file) &&
				!(file.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && !file.nFileSizeHigh &&
				file.nFileSizeLow <= 16)
			{
				DeleteFileW(path);
			}
		}
	}

	bool install_components()
	{
		std::lock_guard<std::recursive_mutex> lock(install_mutex);
		if (unpack_complete) return true;
		if (!splash_seen.load(std::memory_order_acquire)) return false;

		const auto status = component_loader::post_unpack();
		if (status == hook::Status::NotReady) return false;
		unpack_complete = true;
		state::hookState.store(
			static_cast<int>(status == hook::Status::Installed ? state::HookState::Installed : state::HookState::Failed));

		if (status == hook::Status::Installed)
		{
#ifndef IW8X_CLIENT_ONLY
			auto_server::StartOrRetain();
#endif
			online_services::Component_Stage_Unpack();
#ifndef IW8X_CLIENT_ONLY
			if (dedicated::Enabled()) dedicated::ArmStartup();
#endif
			if (dedicated::Enabled())
				LOG_INFO("Core", "authentication, fence and dedicated/headless startup hooks installed");
			else
				LOG_INFO("Core", "regular client fence/auth startup hooks installed (example-compatible path)");
		}
		else
		{
			LOG_ERR("Core", "authentication/fence hook installation failed");
			if (dedicated::Enabled())
			{
				shutdown_components();
				ExitProcess(ERROR_DLL_INIT_FAILED);
			}
		}
		return true;
	}

	bool on_splash_loaded()
	{
#ifndef IW8X_CLIENT_ONLY
		if (!auto_server::WaitForPreviousHost()) ExitProcess(ERROR_TIMEOUT);
#endif
		// Init is idempotent: preserve early critical-patch logs if the background
		// initialization thread has not opened the log yet.
		console::Init(dedicated::Enabled());
		// Suppress frontend/UI initialization before returning to native startup.
		// Command work remains disarmed until the auth/dvar/guard hooks are ready.
#ifndef IW8X_CLIENT_ONLY
		if (dedicated::Enabled() && dedicated::Install(game_base) != hook::Status::Installed)
		{
			LOG_ERR("Dedicated", "critical splash patch failed; terminating before partial dedicated startup");
			ExitProcess(ERROR_DLL_INIT_FAILED);
		}
		// Native NET_Start happens before post_unpack in the dedicated boot.
		// The configured gameplay UDP port must enter bdNetStartParams before
		// bdCommonAddr and the server's joinable XSESSION_INFO are constructed.
		// Delaying these hooks until online_services::Install left the dedicated
		// host on the client default 3074 while advertised_port expected 3075,
		// permanently preventing server session publication (session=0).
		if (dedicated::Enabled() && native_network::Install(game_base) != hook::Status::Installed)
		{
			LOG_ERR("Network", "critical dedicated endpoint hooks failed before NET_Start");
			ExitProcess(ERROR_DLL_INIT_FAILED);
		}
		// Renderer/device interception is dedicated-only. The regular client keeps
		// the exact native renderer startup used by the example DLL.
		if (dedicated::Enabled())
		{
			const auto windows = dedicated_window::Install();
			if (windows != hook::Status::Installed)
			{
				LOG_ERR("HeadlessWindow", "could not suppress dedicated game windows at the splash boundary");
				ExitProcess(ERROR_DLL_INIT_FAILED);
			}
			const auto graphics = headless::InstallDeviceHooks(game_base);
			if (graphics != hook::Status::Installed)
			{
				LOG_ERR("Headless", "could not arm device hooks at the splash boundary");
				ExitProcess(ERROR_DLL_INIT_FAILED);
			}
		}
#endif
		{
			std::lock_guard lock(boot_mutex);
			splash_seen.store(true, std::memory_order_release);
		}
		boot_wakeup.notify_all();
		state::hookState.store(static_cast<int>(state::HookState::SignalSeen));
		if (!dedicated::Enabled())
		{
			// Full DLL clients need the same barrier as client-only builds: native
			// dvar/profile startup must not overtake signature and identity preparation.
			// HTTP work stays on initialize_thread, outside the loader lock.
			std::unique_lock lock(boot_mutex);
			if (!boot_wakeup.wait_for(lock, std::chrono::seconds(30), [] { return ready.load(std::memory_order_acquire); }))
				LOG_ERR("Core", "client initialization did not complete before the splash deadline");
		}
		if (ready.load(std::memory_order_acquire)) install_components();
		return true;
	}

	DWORD WINAPI startup_monitor(LPVOID)
	{
		{
			std::unique_lock lock(boot_mutex);
			if (!boot_wakeup.wait_for(lock, std::chrono::seconds(120), [] { return splash_seen.load(std::memory_order_acquire); }))
			{
				state::hookState.store(static_cast<int>(state::HookState::Failed));
				LOG_ERR("Core", "game splash was not observed; hooks were not installed");
				lock.unlock();
				shutdown_components();
				return 0;
			}
		}

		const ULONGLONG installStart = GetTickCount64();
		while (!install_components())
		{
			if (GetTickCount64() - installStart >= 15000)
			{
				state::hookState.store(static_cast<int>(state::HookState::PrologueMismatch));
				{
					std::lock_guard<std::recursive_mutex> lock(install_mutex);
					LOG_ERR("Core", "component %s did not become ready within startup deadline", component_loader::active_component());
				}
				shutdown_components();
				if (dedicated::Enabled()) ExitProcess(ERROR_DLL_INIT_FAILED);
				return 0;
			}
			std::unique_lock lock(boot_mutex);
			boot_wakeup.wait_for(lock, std::chrono::milliseconds(10));
		}
		if (state::hookState.load() == static_cast<int>(state::HookState::Failed)) shutdown_components();
		return 0;
	}

	DWORD WINAPI initialize_thread(LPVOID)
	{
#ifndef IW8X_CLIENT_ONLY
		if (!auto_server::WaitForPreviousHost()) ExitProcess(ERROR_TIMEOUT);
#endif
		HMODULE pinned = nullptr;
		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&initialize_thread), &pinned);

		try
		{
			console::Init(dedicated::Enabled());
			LOG_INFO("Startup", "GDI clone compatibility repair at RVA 0x3061a0: %s",
				gdi_compatibility ? "applied" : "FAILED (target bytes/protection mismatch)");

			proxy::Preload();

			game_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
			uint32_t image_size = 0;
			if (!check_target(game_base, image_size))
			{
				LOG_WARN("Core", "unsupported host; XInput forwarding only");
				return 0;
			}

			system_runtime::set_shutdown_handler(&shutdown_components);
			hook::Status start_status;
			{
				std::lock_guard<std::recursive_mutex> lock(install_mutex);
				start_status = component_loader::post_start();
			}
			if (start_status != hook::Status::Installed)
			{
				state::hookState.store(static_cast<int>(state::HookState::Failed));
				shutdown_components();
				ExitProcess(ERROR_REVISION_MISMATCH);
			}

			state::moduleBase.store(game_base);
			state::imageSize.store(image_size);

#ifdef IW8X_CLIENT_ONLY
			online_config::synchronize();
#endif

			hook::Status load_status;
			{
				std::lock_guard<std::recursive_mutex> lock(install_mutex);
				load_status = component_loader::post_load();
			}
			if (load_status != hook::Status::Installed)
			{
				state::hookState.store(static_cast<int>(state::HookState::Failed));
				LOG_ERR("Auth", "local identity/component preparation failed");
				shutdown_components();
				return 0;
			}

			if (!trigger_armed)
			{
				state::hookState.store(static_cast<int>(state::HookState::Failed));
				LOG_ERR("Core", "splash hook unavailable; auth/fence hooks will not install");
				shutdown_components();
				return 0;
			}

			state::hookState.store(static_cast<int>(state::HookState::WaitingForSignal));
			{
				std::lock_guard lock(boot_mutex);
				ready.store(true, std::memory_order_release);
			}
			boot_wakeup.notify_all();
			startup_monitor(nullptr);
		}
		catch (const std::exception& e)
		{
			state::hookState.store(static_cast<int>(state::HookState::Failed));
			LOG_ERR("Core", "initialization failed: %s", e.what());
			shutdown_components();
		}
		catch (...)
		{
			state::hookState.store(static_cast<int>(state::HookState::Failed));
			LOG_ERR("Core", "initialization failed with an unknown exception");
			shutdown_components();
		}
		return 0;
	}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(module);

		game_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		uint32_t image_size = 0;
		if (check_target(game_base, image_size))
		{
			if (deployment::kClientOnly && dedicated_requested())
			{
				OutputDebugStringA("IW8x client DLL cannot run a dedicated server. Use the server DLL.\n");
				return FALSE;
			}
			// Native TACT metadata is needed before the host's entry point.
			// Lua and playlist resources stay in memory; only missing native
			// config/index files are materialized, without replacing game data.
			if (!embedded_assets::PrepareNativeMetadata(module))
			{
				OutputDebugStringA("Replay proxy: embedded asset preparation failed\n");
				return FALSE;
			}
			// Match the example DLL for both client and dedicated startup: these
			// attach-time steps happen before the Replay CRT/window bootstrap.
			clear_previous_run_crash_markers();
			state::moduleBase.store(game_base);
			state::imageSize.store(image_size);
			// Reference startup raises physicsasset and BAP raises
			// xmodeldetailcollision before DB_InitXAssetPools. This must happen
			// under process attach: by the splash boundary Replay has already
			// allocated and linked the native pools.
			wchar_t port_config[MAX_PATH]{};
			const bool port_requested = native_network::ConfigurationPath(port_config, MAX_PATH) &&
				night_files::requested(port_config);
#ifndef IW8X_CLIENT_ONLY
			if (!bap::PrepareNativePools(game_base, port_requested))
#else
			if (port_requested && !asset_pools::prepare(game_base, true))
#endif
			{
				OutputDebugStringA("Replay proxy: native collision pool preparation failed\n");
				return FALSE;
			}
			// The BAP auxiliary DB entry router detours some of the very same
			// instructions relocated by night_entry_pool. Installing both corrupts
			// BAP's verified native hook sites (DB_RemoveStashedAsset among them).
			// On dedicated BAP hosts, use BAP's router and keep the relocated
			// table exclusive to ordinary converted-map client/server startup.
#ifndef IW8X_CLIENT_ONLY
			const bool bap_entry_router = dedicated::Enabled() && bap::Enabled();
#else
			constexpr bool bap_entry_router = false;
#endif
			if (port_requested && !bap_entry_router && !night_entry_pool::Prepare(game_base))
			{
				OutputDebugStringA("Replay proxy: converted-map asset-entry preparation failed\n");
				return FALSE;
			}
			if (port_requested && bap_entry_router)
				OutputDebugStringA("Replay proxy: BAP auxiliary entry router selected; incompatible converted-map entry relocation bypassed\n");

			diagnostics::Initialize(module);
			gdi_compatibility = startup::ApplyGdiCompatibility(game_base);
			trigger_armed = trigger::InstallLoadImageTrigger(&on_splash_loaded, game_base, 0x1bd347f);
		}

		// Both modes run the same console/fence/auth bootstrap. --dedi only
		// changes what gets layered on at the splash boundary.
		HANDLE thread = CreateThread(nullptr, 0, initialize_thread, nullptr, 0, nullptr);
		if (thread) CloseHandle(thread);
	}
	return TRUE;
}
