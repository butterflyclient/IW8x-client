#include "network/backend/backend_agent.h"
#include "dedicated/auto_server.h"

#include "common/deployment_profile.h"

#include "dedicated/dedicated.h"

#include "common/console.h"

#include "network/native_network.h"
#include "dedicated/headless/non_player.h"

#include <windows.h>

#include <filesystem>
#include <string>
#include <cwctype>
#include <mutex>
namespace auto_server
{
	namespace
	{
		struct Host
		{
			DWORD pid;
			FILETIME created;
		};

		HANDLE marker = nullptr, mapping = nullptr;
		Host* host = nullptr;

		bool Describe(HANDLE process, Host& host, const wchar_t* executable)
		{
			wchar_t actual[MAX_PATH]{};
			DWORD length = MAX_PATH;
			FILETIME exit{}, kernel{}, user{};
			return WaitForSingleObject(process, 0) == WAIT_TIMEOUT && QueryFullProcessImageNameW(process, 0, actual, &length) &&
				!_wcsicmp(actual, executable) && GetProcessTimes(process, &host.created, &exit, &kernel, &user);
		}

		bool Alive(const Host& host, const wchar_t* executable)
		{
			if (!host.pid) return false;
			HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, host.pid);
			if (!process) return false;
			Host current{};
			const bool alive = Describe(process, current, executable) && CompareFileTime(&host.created, &current.created) == 0;
			CloseHandle(process);
			return alive;
		}

		bool LaunchLocked(const wchar_t* executable, const std::wstring& extra = {}, std::wstring_view startupOptions = {})
		{
			std::wstring command = L"\"" + std::wstring(executable) + L"\" --dedi -bap " +
				std::wstring(startupOptions.empty() ? L"--auto-playlist --min-players 1" : startupOptions) + extra;
			STARTUPINFOW startup{};
			startup.cb = sizeof(startup);
			startup.dwFlags = STARTF_USESHOWWINDOW;
			startup.wShowWindow = SW_SHOWNOACTIVATE;
			PROCESS_INFORMATION process{};
			const auto root = std::filesystem::path(executable).parent_path();
			if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &startup, &process))
			{
				LOG_ERR("AutoServer", "could not start dedicated process: Windows error %lu", GetLastError());
				return false;
			}
			host->pid = process.dwProcessId;
			Describe(process.hProcess, *host, executable);
			LOG_INFO("AutoServer", "started replacement/automatic dedicated process pid=%lu", host->pid);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			return true;
		}
	}

	bool WaitForPreviousHost()
	{
		static std::once_flag once;
		static bool ready = true;
		std::call_once(once,
			[]
			{
				if (!dedicated::Enabled()) return;
				const wchar_t* argument = wcsstr(GetCommandLineW(), L" --recycled-host ");
				if (!argument) return;
				DWORD pid = 0;
				unsigned long long created = 0;
				if (swscanf_s(argument, L" --recycled-host %lu:%llu", &pid, &created) != 2 || !pid || pid == GetCurrentProcessId())
				{
					ready = false;
					return;
				}
				HANDLE previous = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
				if (!previous) return; // already exited before this DLL was loaded
				wchar_t executable[MAX_PATH]{};
				GetModuleFileNameW(nullptr, executable, MAX_PATH);
				Host actual{};
				const FILETIME expected{ DWORD(created), DWORD(created >> 32) };
				if (Describe(previous, actual, executable) && CompareFileTime(&actual.created, &expected) == 0)
					ready = WaitForSingleObject(previous, 30000) == WAIT_OBJECT_0;
				CloseHandle(previous);
			});
		return ready;
	}

	bool RestartDedicated(std::wstring_view startup)
	{
		if (deployment::kClientOnly) return false;
		if (!dedicated::Enabled() || !marker || !host) return false;
		const auto wait = WaitForSingleObject(marker, 5000);
		if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return false;
		wchar_t executable[MAX_PATH]{};
		GetModuleFileNameW(nullptr, executable, MAX_PATH);
		Host current{};
		const bool ownsRecord = host->pid == GetCurrentProcessId() && Describe(GetCurrentProcess(), current, executable) &&
			CompareFileTime(&host->created, &current.created) == 0;
		bool launched = false;
		if (ownsRecord)
		{
			const auto created = (static_cast<unsigned long long>(current.created.dwHighDateTime) << 32) | current.created.dwLowDateTime;
			// The child waits before opening logs/sockets. Publish its PID under the
			// same mutex used by client startup so another client cannot spawn a duplicate.
			std::wstring extra = L" --recycled-host " + std::to_wstring(GetCurrentProcessId()) + L":" + std::to_wstring(created);
			// Keep the verified native host mode across end_server. Otherwise the
			// replacement silently registers a local player and reserves slot 0.
			if (headless::NonPlayerHost()) extra += L" --native-non-player";
			const auto root = std::filesystem::path(executable).parent_path();
			CopyFileW((root / L"mw120rproxy.dedi.log").c_str(), (root / L"mw120rproxy.dedi.previous.log").c_str(), FALSE);
			launched = LaunchLocked(executable, extra, startup);
		}
		ReleaseMutex(marker);
		if (!launched) return false;
		backend_agent::Stop(); // Flush the host award outbox before process recycling.
		ExitProcess(0);
	}

	void StartOrRetain()
	{
		if (deployment::kClientOnly)
		{
			LOG_INFO("AutoServer", "client-only DLL: local dedicated/backend launch disabled");
			return;
		}
		if (!dedicated::Enabled())
		{
			// Starting a client must never start a game server, including installs
			// retaining the old auto_server=1 override. The explicit server script
			// owns initial startup; dedicated map recycling still uses this registry.
			LOG_INFO("AutoServer", "client launch: dedicated server starts separately with Start Dedicated.cmd");
			return;
		}
		if (!backend_agent::EnsureLocal())
		{
			LOG_ERR("AutoServer", "standalone backend could not start");
			return;
		}
		wchar_t executable[MAX_PATH]{};
		const auto size = GetModuleFileNameW(nullptr, executable, MAX_PATH);
		if (!size || size >= MAX_PATH) return;
		if (!marker)
		{
			uint64_t hash = 14695981039346656037ull;
			for (wchar_t c : std::wstring(executable))
				hash = (hash ^ uint64_t(towlower(c))) * 1099511628211ull;
			wchar_t name[96]{};
			swprintf_s(name, L"Local\\MW120Dedicated_%016llX", static_cast<unsigned long long>(hash));
			marker = CreateMutexW(nullptr, FALSE, name);
			wcscat_s(name, L"_process");
			mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Host), name);
			if (mapping) host = static_cast<Host*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Host)));
		}
		if (!marker || !host)
		{
			LOG_ERR("AutoServer", "cannot open local host registry");
			return;
		}
		const auto wait = WaitForSingleObject(marker, 5000);
		if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return;
		// PID + creation time + executable prevent stale markers and PID reuse from
		// suppressing a new host after a previous dedicated process has exited.
		if (Alive(*host, executable))
		{
			LOG_INFO("AutoServer", "using dedicated process pid=%lu", host->pid);
			ReleaseMutex(marker);
			return;
		}
		if (dedicated::Enabled())
		{
			host->pid = GetCurrentProcessId();
			Describe(GetCurrentProcess(), *host, executable);
		}
		else
		{
			LaunchLocked(executable);
		}
		ReleaseMutex(marker);
		// The host keeps this shared record alive when its launching client exits.
	}
}
