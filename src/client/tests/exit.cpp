#include <windows.h>

#include "client/component/scheduler.hpp"
#include "client/loader/component_loader.hpp"
#include "engine/image.hpp"
#include "engine/imports.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace console { void Print(const char*, ...) {} }

namespace
{
	using exit_function = void (WINAPI*)(UINT);
	void* fixture_image{};
	void** exit_slot{};
	std::wstring marker_path;
	std::atomic<bool> worker_entered{}, worker_finished{};
	bool recursive_exit{};
	unsigned checks{};

	void require(const bool condition, const char* label)
	{
		++checks;
		if (!condition)
		{
			std::fprintf(stderr, "FAIL: %s\n", label);
			ExitProcess(1);
		}
	}

	void append_marker(const char value)
	{
		const auto file = CreateFileW(marker_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		require(file != INVALID_HANDLE_VALUE, "open shutdown marker");
		DWORD written{};
		const auto result = WriteFile(file, &value, 1, &written, nullptr);
		CloseHandle(file);
		require(result && written == 1, "write shutdown marker");
	}

	class shutdown_fixture final : public component_interface
	{
	public:
		component_priority priority() override { return component_priority::signatures; }
		void pre_destroy() override
		{
			require(worker_finished.load(std::memory_order_acquire), "active callback finished before exit");
			require(scheduler::status().workers == 0, "shared workers joined before exit");
			require(!scheduler::schedule([] { return std::nullopt; }).accepted(), "shutdown rejects new work");
			append_marker('C');
			if (recursive_exit) reinterpret_cast<exit_function>(*exit_slot)(24);
		}
	};

	void shutdown_handler()
	{
		component_loader::pre_destroy();
		component_loader::pre_destroy();
		append_marker('H');
	}

	void make_image()
	{
		fixture_image = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		require(fixture_image != nullptr, "allocate loaded PE fixture");
		auto* bytes = static_cast<unsigned char*>(fixture_image);
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
		dos->e_magic = IMAGE_DOS_SIGNATURE;
		dos->e_lfanew = 128;
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes + 128);
		nt->Signature = IMAGE_NT_SIGNATURE;
		nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
		nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
		nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		nt->OptionalHeader.SizeOfImage = 4096;
		nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
		nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = { 512, 40 };
		auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes + 512);
		descriptor->Name = 600;
		descriptor->OriginalFirstThunk = 640;
		descriptor->FirstThunk = 680;
		std::memcpy(bytes + 600, "KERNEL32.dll", 13);
		reinterpret_cast<IMAGE_THUNK_DATA64*>(bytes + 640)->u1.AddressOfData = 720;
		std::memcpy(bytes + 722, "ExitProcess", 12);
		exit_slot = reinterpret_cast<void**>(bytes + 680);
		*exit_slot = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess"));
		require(*exit_slot != nullptr, "resolve kernel exit export");
		DWORD previous{};
		require(VirtualProtect(fixture_image, 4096, PAGE_READONLY, &previous), "protect fixture IAT read-only");
	}

	[[noreturn]] void child(const wchar_t* mode, const wchar_t* path)
	{
		marker_path = path;
		recursive_exit = std::wcscmp(mode, L"--recursive") == 0;
		make_image();
		const auto original = *exit_slot;
		component_loader::register_component(std::make_unique<shutdown_fixture>(), "shutdown_fixture");
		if (std::wcscmp(mode, L"--fallback") != 0) system_runtime::set_shutdown_handler(&shutdown_handler);
		require(component_loader::post_start() == hook::Status::Installed, "child post_start");
		require(component_loader::post_load() == hook::Status::Installed, "child post_load");
		require(component_loader::post_load() == hook::Status::Installed, "exit import install is idempotent");
		require(component_loader::post_unpack() == hook::Status::Installed, "child post_unpack");
		require(*exit_slot != original, "host IAT now points to cooperative exit");
		MEMORY_BASIC_INFORMATION protection{};
		require(VirtualQuery(exit_slot, &protection, sizeof(protection)) == sizeof(protection) &&
			protection.Protect == PAGE_READONLY, "IAT protection restored");
		const auto task = scheduler::schedule([]() -> std::optional<scheduler::milliseconds>
		{
			worker_entered.store(true, std::memory_order_release);
			Sleep(100);
			worker_finished.store(true, std::memory_order_release);
			return std::chrono::hours(1);
		});
		require(task.accepted(), "active callback accepted");
		const auto deadline = GetTickCount64() + 5000;
		while (!worker_entered.load(std::memory_order_acquire))
		{
			require(GetTickCount64() < deadline, "worker entered before exit deadline");
			Sleep(1);
		}
		reinterpret_cast<exit_function>(*exit_slot)(23);
		__assume(0);
	}

	void run_child(const wchar_t* mode, const DWORD expected_exit, const char* expected_marker)
	{
		wchar_t executable[32768]{}, temporary[MAX_PATH]{}, marker[MAX_PATH]{};
		require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "find test executable");
		require(GetTempPathW(MAX_PATH, temporary) != 0 && GetTempFileNameW(temporary, L"iw8", 0, marker) != 0,
			"create isolated marker file");
		auto command = std::wstring(L"\"") + executable + L"\" " + mode + L" \"" + marker + L"\"";
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const auto created = CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
			CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
		if (!created) DeleteFileW(marker);
		require(created, "launch isolated exit fixture");
		CloseHandle(process.hThread);
		const auto waited = WaitForSingleObject(process.hProcess, 10000);
		if (waited != WAIT_OBJECT_0)
		{
			TerminateProcess(process.hProcess, 2);
			WaitForSingleObject(process.hProcess, 1000);
		}
		DWORD exit_code{};
		const auto retrieved = GetExitCodeProcess(process.hProcess, &exit_code);
		CloseHandle(process.hProcess);
		const auto file = CreateFileW(marker, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		char recorded[16]{};
		DWORD read{};
		const auto readable = file != INVALID_HANDLE_VALUE && ReadFile(file, recorded, sizeof(recorded), &read, nullptr);
		if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
		const auto removed = DeleteFileW(marker);
		require(waited == WAIT_OBJECT_0, "cooperative child exit completed within deadline");
		require(retrieved && exit_code == expected_exit, "forward original process exit code");
		require(readable && read == std::strlen(expected_marker) &&
			std::memcmp(recorded, expected_marker, read) == 0, "shutdown callback ran exactly once after worker join");
		require(removed, "remove isolated marker file");
	}
}

namespace game
{
	uintptr_t base() noexcept { return reinterpret_cast<uintptr_t>(fixture_image); }
	uint32_t image_size() noexcept { return 4096; }
	bool initialized() noexcept { return fixture_image != nullptr; }
}

int wmain(const int argc, wchar_t** argv)
{
	if (argc == 3) child(argv[1], argv[2]);
	run_child(L"--normal", 23, "CH");
	run_child(L"--fallback", 23, "C");
	run_child(L"--recursive", 24, "C");
	std::printf("Production exit: %u parent checks passed; normal, fallback and recursive exits drain active workers\n", checks);
	return 0;
}
