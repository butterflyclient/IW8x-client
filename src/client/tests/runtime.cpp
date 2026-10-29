#include "common/utils/thread_pool.hpp"
#include "client/component/scheduler.hpp"
#include "client/loader/component_loader.hpp"
#include "engine/runtime_signatures.hpp"
#include "engine/imports.hpp"
#include "network/backend/join_cleanup_gate.hpp"
#include "client/mp_dvar_registration.hpp"
#include "dedicated/runtime/map_restart_request.hpp"
#include "dedicated/runtime/island_playlist_override.hpp"
#include "common/utils/completion_budget.hpp"
#include <windows.h>

#include <atomic>
#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <filesystem>
#include <future>
#include <thread>

namespace console { void Print(const char*, ...) {} }

namespace
{
	using namespace std::chrono_literals;
	using pool = utils::thread_pool;
	using clock_type = pool::clock;
	unsigned checks = 0;
	void require(bool condition, const char* label)
	{
		++checks;
		if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
	}
	void test_mp_dvar_registration()
	{
		bool registered = true;
		unsigned registrations = 0;
		const auto ready = [&] { return registered; };
		const auto restore = [&] { ++registrations; registered = true; };
		require(client::mp_dvar_registration::ensure(ready, restore) && registrations == 0,
			"healthy MP allocation preserves registered dvars");
		registered = false;
		require(client::mp_dvar_registration::ensure(ready, restore) && registrations == 1,
			"join after MP shutdown restores registration before allocation");
		require(client::mp_dvar_registration::ensure(ready, restore) && registrations == 1,
			"repeated allocation does not re-register live dvars");
		registered = false;
		require(!client::mp_dvar_registration::ensure(ready, [&] { ++registrations; }) && registrations == 2,
			"failed native registration cannot proceed to allocation");
		require(client::mp_dvar_registration::ensure(ready, restore) && registrations == 3,
			"later mode teardown permits another native registration");
	}
	void test_join_cleanup_gate()
	{
		using join_cleanup_gate::skip;
		using join_cleanup_gate::reset_invalid_list;
		require(skip(0, 0), "assigned BR transition skips absent cleanup object");
		require(skip(2, 0), "MP to Warzone transition skips absent singleton after disconnect");
		require(!skip(1, 0), "active SP frontend state retains native cleanup");
		require(!skip(2, 0x10000), "initialized cleanup object remains native");
		require(reset_invalid_list(2, 1, false), "assigned join drops unreadable cleanup entry");
		require(reset_invalid_list(2, 2, false), "reject cleanup count overlapping metadata");
		require(reset_invalid_list(2, 1, false), "stale cleanup entry after shutdown still guarded");
		require(!reset_invalid_list(2, 1, true), "valid cleanup entry retains stock destructor");
		require(!reset_invalid_list(2, 0, false), "empty cleanup list retains stock path");
		require(!reset_invalid_list(1, 1, false), "inactive cleanup list is not modified");
	}
	void test_dedicated_map_restart_request()
	{
		using request = dedicated::detail::MapRestartRequest;
		request state;
		require(state.Queue() == request::Result::NotReady && !state.Pending(), "map_restart is rejected outside gameplay");
		state.SetReady(true);
		require(state.Queue() == request::Result::Queued && state.Pending(), "console queues one map_restart for VM frame");
		require(state.Queue() == request::Result::AlreadyQueued, "duplicate map_restart requests collapse");
		state.Finish();
		require(!state.Pending() && state.Queue() == request::Result::Queued, "successful VM restart retires its request");
		state.SetReady(false);
		require(!state.Pending() && state.Queue() == request::Result::NotReady, "teardown clears pending VM restart");
		state.SetReady(true);
		require(state.Queue() == request::Result::Queued && state.Pending(), "new map generation accepts restart");
		state.SetReady(false);
		require(!state.Pending(), "pending restart never leaks to the next map");
	}
	void test_island_playlist_override()
	{
		using dedicated::detail::AllowIslandPlaylistMapOverride;
		require(AllowIslandPlaylistMapOverride("mp_sm_island_1", "mp_donetsk", "br", "br", true, true, true),
			"explicit Island can inherit real BR playlist settings in opt-in mode");
		require(AllowIslandPlaylistMapOverride("mp_sm_island_1", "mp_donetsk", "br", "br", true, false, true),
			"explicit Island map inherits playlist BR mode when mode omitted");
		require(!AllowIslandPlaylistMapOverride("mp_sm_island_1", "mp_donetsk", "br", "br", true, true, false),
			"Island overlay disabled outside opt-in compatibility mode");
		require(!AllowIslandPlaylistMapOverride("mp_donetsk", "mp_shipment", "br", "br", true, true, true),
			"non-Island map cannot override playlist selection");
		require(!AllowIslandPlaylistMapOverride("mp_sm_island_1", "mp_donetsk", "war", "war", true, true, true),
			"multiplayer playlist cannot be substituted into Island BR");
		require(!AllowIslandPlaylistMapOverride("mp_sm_island_1", "mp_donetsk", "br", "war", true, true, true),
			"incompatible requested mode is rejected");
		require(!AllowIslandPlaylistMapOverride("mp_sm_island_1", "mp_donetsk", "br", "br", false, true, true),
			"normal playlist rotation still chooses its original map");
	}
	bool until(const std::function<bool()>& predicate)
	{
		const auto end = clock_type::now() + 10s;
		while (!predicate())
		{
			if (clock_type::now() >= end) return false;
			std::this_thread::sleep_for(1ms);
		}
		return true;
	}

	void test_pool()
	{
		pool workers;
		require(!workers.schedule([] { return std::nullopt; }), "reject before start");
		require(workers.start(2, 3), "start two workers");
		require(!workers.start(), "reject duplicate start");
		auto first = workers.schedule([] { return std::nullopt; }, 1h);
		auto second = workers.schedule([] { return std::nullopt; }, 1h);
		auto third = workers.schedule([] { return std::nullopt; }, 1h);
		require(bool(first) && bool(second) && bool(third), "delayed work holds slots");
		require(!workers.schedule([] { return std::nullopt; }), "bounded admission");
		const auto idle = workers.status().wakeups;
		std::this_thread::sleep_for(50ms);
		require(workers.status().wakeups == idle, "workers sleep on future deadlines");
		first.cancel(true);
		require(!first, "cancel queued work");
		std::atomic<unsigned> called{ 0 };
		auto immediate = workers.schedule([&]() -> std::optional<pool::milliseconds>
		{
			++called;
			throw std::runtime_error("expected fixture exception");
		});
		require(until([&] { return called == 1 && !immediate; }), "callback exception retires");
		require(workers.status().exceptions == 1, "exception isolation count");
		second.cancel(); third.cancel();
		std::atomic<int> active{ 0 }, peak{ 0 }, repetitions{ 0 };
		auto repeating = workers.schedule([&]() -> std::optional<pool::milliseconds>
		{
			const auto count = ++active;
			peak.store((std::max)(peak.load(), count));
			std::this_thread::sleep_for(1ms);
			--active;
			if (++repetitions == 10) return std::nullopt;
			return 0ms;
		});
		for (int i = 0; i < 20; ++i) repeating.wake();
		require(until([&] { return !repeating; }), "repeating work finishes");
		require(peak == 1 && repetitions == 10, "one owner never overlaps");
		std::promise<void> entered, release;
		auto released = release.get_future().share();
		auto blocked = workers.schedule([&]() -> std::optional<pool::milliseconds>
		{
			entered.set_value(); released.wait(); return 1h;
		});
		entered.get_future().wait();
		auto canceled = std::async(std::launch::async, [&] { blocked.cancel(true); });
		require(canceled.wait_for(20ms) == std::future_status::timeout, "cancel waits for active callback");
		release.set_value();
		require(canceled.wait_for(10s) == std::future_status::ready && !blocked, "active cancel retires repeat");
		auto old = workers.schedule([] { return std::optional{ 1h }; }, 1h);
		workers.stop();
		require(!old && !workers.schedule([] { return std::nullopt; }), "stop rejects and invalidates");
		require(workers.start(1, 3), "restart epoch");
		old.wake(); old.cancel(true);
		std::atomic<int> drained{ 0 };
		auto drain = workers.schedule([&] { ++drained; return std::optional{ 1h }; }, 1h);
		workers.stop(true);
		require(drained == 1 && !drain, "drain delayed task once without repeat");
		require(workers.start(4, 4096), "load workers");
		std::atomic<unsigned> completed{ 0 };
		const auto start = clock_type::now();
		for (int i = 0; i < 3200; ++i)
			require(workers.schedule([&]() -> std::optional<pool::milliseconds> { ++completed; return std::nullopt; }).accepted(),
				"load accepted");
		require(until([&] { return completed == 3200; }), "load complete");
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now() - start).count();
		std::printf("Pool load: 3200 one-shot tasks=%lldus workers=4 high_water=%zu\n", elapsed, workers.status().high_water);
		workers.stop();
	}

	void test_imports()
	{
		std::vector<uint8_t> bytes(4096);
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
		dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 128;
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes.data() + 128);
		nt->Signature = IMAGE_NT_SIGNATURE;
		nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
		nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
		nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
		nt->OptionalHeader.SizeOfImage = DWORD(bytes.size());
		nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
		nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = { 512, 40 };
		auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(bytes.data() + 512);
		descriptor->Name = 600; descriptor->OriginalFirstThunk = 640; descriptor->FirstThunk = 680;
		std::memcpy(bytes.data() + 600, "KERNEL32.dll", 13);
		auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(bytes.data() + 640);
		names[0].u1.AddressOfData = 720;
		std::memcpy(bytes.data() + 722, "ExitProcess", 12);
		const auto base = reinterpret_cast<uintptr_t>(bytes.data());
		require(game::imports::find(base, bytes.size(), "kernel32.DLL", "ExitProcess") == reinterpret_cast<void**>(base + 680),
			"bounded case-insensitive IAT lookup");
		require(!game::imports::find(base, bytes.size(), "KERNEL32.dll", "Missing"), "missing import rejected");
		names[0].u1.AddressOfData = bytes.size() - 1;
		require(!game::imports::find(base, bytes.size(), "KERNEL32.dll", "ExitProcess"), "truncated import name rejected");
		require(!game::imports::find(base, 512, "KERNEL32.dll", "ExitProcess"), "invalid image span rejected");
		const auto inaccessible = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
		require(inaccessible && !game::imports::find(reinterpret_cast<uintptr_t>(inaccessible), 4096, "KERNEL32.dll", "ExitProcess"),
			"unreadable loaded image rejected");
		if (inaccessible) VirtualFree(inaccessible, 0, MEM_RELEASE);
		utils::completion_budget budget;
		unsigned count = 0; while (budget.next()) ++count;
		require(count == 16, "service completion count budget");
		utils::completion_budget slow;
		require(slow.next(), "service budget permits first callback");
		std::this_thread::sleep_for(3ms);
		require(!slow.next(), "service completion time budget");
	}

	uint64_t cpu_us()
	{
		FILETIME creation{}, exit{}, kernel{}, user{};
		GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
		const auto to_value = [](FILETIME time) { return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
		return (to_value(kernel) + to_value(user)) / 10;
	}

	void benchmark()
	{
		std::atomic<bool> stopping{ false };
		std::atomic<unsigned> ticks{ 0 };
		std::vector<std::thread> old_workers;
		const auto old_cpu = cpu_us();
		for (const auto delay : { 50ms, 100ms, 100ms, 100ms, 50ms })
			old_workers.emplace_back([&, delay]
			{
				while (!stopping)
				{
					++ticks;
					std::this_thread::sleep_for(delay);
				}
			});
		std::this_thread::sleep_for(1s);
		stopping = true;
		for (auto& thread : old_workers) thread.join();
		const auto baseline_cpu = cpu_us() - old_cpu;
		pool workers;
		workers.start(2, 512);
		// Let thread startup finish before measuring idle wakeups.
		std::this_thread::sleep_for(20ms);
		const auto before = workers.status();
		const auto new_cpu = cpu_us();
		std::this_thread::sleep_for(1s);
		std::printf("Synthetic idle (1s): before_threads=5 before_ticks=%u before_cpu_us=%llu "
			"after_threads=2 after_wakeups=%llu after_cpu_us=%llu\n",
			ticks.load(), baseline_cpu, workers.status().wakeups - before.wakeups, cpu_us() - new_cpu);

		for (const bool pooled : { false, true })
		{
			std::mutex mutex;
			std::condition_variable cv;
			std::deque<std::pair<clock_type::time_point, unsigned>> queued;
			std::vector<double> latency(200);
			std::atomic<unsigned> completed{ 0 };
			std::atomic<bool> finish{ false };
			auto execute = [&](clock_type::time_point start, unsigned index)
			{
				latency[index] = std::chrono::duration<double, std::micro>(clock_type::now() - start).count();
				++completed; cv.notify_all();
			};
			std::thread legacy;
			if (!pooled) legacy = std::thread([&]
			{
				while (!finish)
				{
					std::deque<std::pair<clock_type::time_point, unsigned>> batch;
					{ std::lock_guard lock(mutex); batch.swap(queued); }
					for (const auto& [start, index] : batch) execute(start, index);
					std::this_thread::sleep_for(50ms);
				}
			});
			for (unsigned i = 0; i < latency.size(); ++i)
			{
				const auto start = clock_type::now();
				if (pooled)
					require(workers.schedule([&, start, i]() -> std::optional<pool::milliseconds>
					{ execute(start, i); return std::nullopt; }).accepted(), "latency admission");
				else { std::lock_guard lock(mutex); queued.emplace_back(start, i); }
				std::this_thread::sleep_for(2ms);
			}
			require(until([&] { return completed == latency.size(); }), "latency completion");
			finish = true;
			if (legacy.joinable()) legacy.join();
			std::sort(latency.begin(), latency.end());
			double sum = 0; for (const auto value : latency) sum += value;
			std::printf("Synthetic latency (200 tasks, 2ms submissions): %s avg=%.2fus p95=%.2fus p99=%.2fus\n",
				pooled ? "shared_cv_pool" : "legacy_50ms_poll", sum / latency.size(), latency[189], latency[197]);
		}
		workers.stop();
	}

	std::vector<int> stages;
	class fixture final : public component_interface
	{
		int id_;
		bool retry_;
	public:
		fixture(int id, bool retry) : id_(id), retry_(retry) {}
		hook::Status post_start() override { stages.push_back(id_); return hook::Status::Installed; }
		hook::Status post_load() override { stages.push_back(id_ + 10); return hook::Status::Installed; }
		hook::Status post_unpack() override
		{
			stages.push_back(id_ + 20);
			if (retry_) { retry_ = false; return hook::Status::NotReady; }
			return hook::Status::Installed;
		}
		void pre_destroy() override { stages.push_back(id_ + 30); }
		component_priority priority() override { return static_cast<component_priority>(400 - id_); }
	};

	void test_components()
	{
		component_loader::register_component(std::make_unique<fixture>(1, false), "first");
		component_loader::register_component(std::make_unique<fixture>(2, true), "retry");
		require(component_loader::post_unpack() == hook::Status::NotReady, "unpack requires earlier stages");
		require(component_loader::post_start() == hook::Status::Installed, "component start");
		require(component_loader::post_load() == hook::Status::Installed, "component load");
		require(component_loader::post_unpack() == hook::Status::NotReady, "named pending component");
		require(std::string(component_loader::active_component()) == "retry", "pending diagnostic owner");
		require(component_loader::post_unpack() == hook::Status::Installed, "component retry");
		require(component_loader::post_unpack() == hook::Status::Installed, "component idempotence");
		require(stages == std::vector<int>({ 1, 2, 11, 12, 21, 22, 22 }), "success steps never repeated");

		unsigned game = 0;
		for (int i = 0; i < 256; ++i) require(scheduler::post_game([&] { ++game; }), "game queue bounded admission");
		require(!scheduler::post_game([] {}), "game queue overload");
		scheduler::pump_game();
		require(game > 0 && game <= 64, "game completion count bound");
		while (game < 256) scheduler::pump_game();
		unsigned slow = 0;
		for (int i = 0; i < 4; ++i) scheduler::post_game([&] { ++slow; std::this_thread::sleep_for(3ms); scheduler::pump_game(); });
		scheduler::pump_game();
		require(slow == 1, "game time budget and recursion guard");
		component_loader::pre_destroy();
		component_loader::pre_destroy();
		require(stages[stages.size() - 2] == 32 && stages.back() == 31, "reverse shutdown once");
		require(!scheduler::schedule([] { return std::nullopt; }), "runtime shutdown rejects work");
	}
}

int main(int argc, char** argv)
{
	test_mp_dvar_registration();
	test_join_cleanup_gate();
	test_dedicated_map_restart_request();
	test_island_playlist_override();
	test_pool();
	test_imports();
	benchmark();
	if (argc > 1)
	{
		std::ifstream stream(argv[1], std::ios::binary);
		const std::vector<uint8_t> file{ std::istreambuf_iterator<char>(stream), {} };
		runtime_signatures::report report;
		require(runtime_signatures::scan(file, report), report.failure.c_str());
		require(report.resolved == runtime_signatures::count(), "full runtime registry");
		std::printf("String signature preflight: %zu/%zu patterns=%lluus\n", report.resolved, report.patterns, report.elapsed_us);
		auto damaged = file;
		if (damaged.size() > 2) damaged[0] = 0;
		require(!runtime_signatures::scan(damaged, report), "malformed image rejected");
		damaged = file;
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(damaged.data());
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(damaged.data() + dos->e_lfanew);
		const auto* sections = IMAGE_FIRST_SECTION(nt);
		bool changed = false;
		constexpr DWORD signature_rva = 0x3061a0;
		for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index)
		{
			const auto& section = sections[index];
			if (signature_rva < section.VirtualAddress || signature_rva - section.VirtualAddress >= section.SizeOfRawData) continue;
			const auto offset = size_t(section.PointerToRawData) + signature_rva - section.VirtualAddress;
			require(offset < damaged.size(), "signature fixture byte is in the file");
			damaged[offset] ^= 1;
			changed = true;
			break;
		}
		require(changed && !runtime_signatures::scan(damaged, report) && !report.failure.empty(),
			"valid PE with a changed native signature fails closed");
		require(!runtime_signatures::initialize(L"missing-fixture.exe"), "missing executable rejected");
		const auto image_path = std::filesystem::path(argv[1]);
		const auto start = clock_type::now();
		require(runtime_signatures::initialize(image_path.c_str()) && runtime_signatures::ready(),
			"startup snapshot hash and registry publish only after validation");
		std::printf("Startup hash/read/preflight: %lldus\n",
			std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now() - start).count());
	}
	test_components();
	std::printf("Production runtime: %u checks passed\n", checks);
}
