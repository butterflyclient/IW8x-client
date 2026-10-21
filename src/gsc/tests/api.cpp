#include "gsc/api.hpp"
#include "gsc/internal/bindings.hpp"
#include "gsc/internal/native_dispatch_stats.hpp"
#include "gsc/internal/players.hpp"
#include "gsc/internal/state.hpp"
#include "plugin/sdk/gsc.hpp"
#include "plugin/loader.hpp"

#include "common/commands/command.hpp"
#include "dedicated/runtime/commands.hpp"

#include <Windows.h>

#include <atomic>
#include <future>
#include <fstream>
#include <iostream>
#include <thread>
#include <type_traits>
extern "C" int sdk_c_version(void);
static_assert(std::is_standard_layout_v<iw8x_player> && std::is_trivially_copyable_v<iw8x_player>);
static_assert(sizeof(iw8x_player) == 24 && offsetof(iw8x_player, generation) == 8 && offsetof(iw8x_player, xuid) == 16);
static_assert(sizeof(iw8x_event) == 88 && offsetof(iw8x_event, entity) == 16 && offsetof(iw8x_event, player) == 32);
static_assert(offsetof(iw8x_plugin_host, register_native) == 24 && offsetof(iw8x_plugin_host, subscribe) == 96);
static_assert(offsetof(iw8x_plugin_host, hook_function) == 176 && offsetof(iw8x_plugin_host, get_dvar_string) == 184);
static_assert(offsetof(iw8x_plugin_host, player_valid) == 192);
static_assert(offsetof(iw8x_plugin_host, broadcast) == 240 && offsetof(iw8x_plugin_host, tell) == 248);
static_assert(offsetof(iw8x_plugin_host, call_builtin) == 256 && offsetof(iw8x_plugin_host, get_dvar) == 264);
static_assert(offsetof(iw8x_plugin_host, register_script) == 272 && offsetof(iw8x_plugin_host, call_script) == 280);
static_assert(offsetof(iw8x_plugin_host, register_script_name) == 288);
static_assert(offsetof(iw8x_plugin_host, call_script_on) == 296);
static_assert(sizeof(iw8x_plugin_host) == 304 && sizeof(iw8x_weapon) == 12);
static_assert(sizeof(iw8x_weapon_snapshot) == 80 && sizeof(iw8x_value) == 24);

namespace
{
	std::atomic<unsigned> dedicated_restart_vm_pumps{};
	std::atomic<bool> dedicated_restart_scope_valid{true};
}

// Test-only stand-in for the production dedicated command integration.
// Do not omit the callback from this fixture: the server VM frame must still
// invoke the real production boundary in lifecycle.cpp.
namespace dedicated::detail
{
	void PumpMapRestartVm() noexcept
	{
		dedicated_restart_vm_pumps.fetch_add(1, std::memory_order_relaxed);
		if (!iw8x::gsc::internal::in_game_scope())
			dedicated_restart_scope_valid.store(false, std::memory_order_relaxed);
	}
}

namespace
{
	using namespace iw8x;
	using namespace iw8x::gsc;
	namespace core = iw8x::gsc::internal;
	unsigned checks{};

	void expect(bool pass, const char* message)
	{
		++checks;
		if (!pass) throw std::runtime_error(message);
	}

	const auto& host = host_api();
	uint64_t entity_generation = 7;
	unsigned notifications{};
	std::vector<value> notified_args;

	iw8x_status valid(iw8x_entity e)
	{
		return e.index == 5 && e.generation == entity_generation && !e.reserved ? IW8X_OK : IW8X_STALE;
	}

	iw8x_status notify(iw8x_entity e, std::string_view name, std::span<const value> args)
	{
		expect((!e.generation || valid(e) == IW8X_OK) && !name.empty(), "notify target/name");
		++notifications;
		notified_args.assign(args.begin(), args.end());
		return IW8X_OK;
	}

	iw8x_status dvar_string(std::string_view name, value& output)
	{
		if (name != "ui_mapname") return IW8X_NOT_FOUND;
		output = value("mp_test");
		return IW8X_OK;
	}

	std::vector<std::string> broadcast_messages;
	std::vector<std::pair<iw8x_player, std::string>> tell_messages;

	iw8x_status broadcast_chat(std::string_view message)
	{
		broadcast_messages.emplace_back(message);
		return IW8X_OK;
	}

	iw8x_status tell_chat(iw8x_player player, std::string_view message)
	{
		tell_messages.emplace_back(player, message);
		return IW8X_OK;
	}

	void configure()
	{
		core::engine_bindings bindings{};
		bindings.capabilities = IW8X_CAP_FUNCTIONS | IW8X_CAP_METHODS | IW8X_CAP_ENTITIES | IW8X_CAP_NOTIFY | IW8X_CAP_GAME_TASKS |
			IW8X_CAP_ASYNC | IW8X_CAP_DVARS | IW8X_CAP_PLAYERS | IW8X_CAP_CHAT;
		bindings.valid = &valid;
		bindings.notify = &notify;
		bindings.get_dvar_string = &dvar_string;
		bindings.player_valid = &core::validate_player;
		bindings.player_name = &core::read_player_name;
		bindings.player_entity = &core::read_player_entity;
		bindings.broadcast_chat = &broadcast_chat;
		bindings.tell_chat = &tell_chat;
		core::configure_bindings(bindings);
	}

	invocation_result call(std::string_view name, std::initializer_list<value> args = {}, bool method = false, iw8x_entity self = {})
	{
		core::game_scope scope;
		return invoke(name, method, self, { args.begin(), args.size() });
	}

	void count_destroy(void* p)
	{
		++*static_cast<int*>(p);
	}
#include "api/scheduler.inc"
	std::vector<std::string> diagnostic_lines;

	void capture_diagnostic(const char*, const char* message)
	{
		diagnostic_lines.emplace_back(message);
	}

	std::promise<void>* sink_entered{};
	std::shared_future<void> sink_release;

	void blocking_sink(const char*, const char*)
	{
		sink_entered->set_value();
		sink_release.wait();
	}
#include "api/diagnostics.inc"
	void empty_native(void*, uint64_t, iw8x_entity)
	{
	}
#include "api/registration.inc"
#include "api/players_events.inc"
#include "api/map_lifecycle.inc"
#include "api/plugins.inc"
	void native_missing_two_hot_slots()
	{
		core::native_dispatch_stats<4> stats;
		for (unsigned i = 0; i < 4; ++i) stats.record(false, 2, true, 10);
		for (unsigned i = 0; i < 3; ++i) stats.record(true, 1, true, 20);
		for (unsigned i = 0; i < 2; ++i) stats.record(false, 0, true, 5);
		stats.record(false, 3, false, 7);
		const auto result = stats.take();
		expect(result.calls == 10 && result.missing == 9, "native histogram counts");
		expect(result.missing_top_slot == 2 && !result.missing_top_method && result.missing_top_calls == 4,
			"native histogram first slot identity");
		expect(result.missing_second_slot == 1 && result.missing_second_method && result.missing_second_calls == 3,
			"native histogram second slot identity");
		expect(result.elapsed_us == 117, "native histogram dispatch duration");
		const auto cleared = stats.take();
		expect(cleared.calls == 0 && cleared.missing == 0 && cleared.missing_top_calls == 0 &&
			cleared.missing_second_calls == 0, "native histogram reset after snapshot");
	}
}

int main()
{
	try
	{
		configure();
			native_missing_two_hot_slots();
		registries_and_values();
		events_commands_tasks_handles();
		queued_commands();
		task_bursts();
		runtime_diagnostics();
		player_lifecycle_and_accessors();
		event_reentrancy_and_scope();
		event_unload_wait();
		server_map_lifecycle();
		exceptions_and_unload_wait();
		map_shutdown_during_worker();
		wchar_t module[MAX_PATH]{};
		GetModuleFileNameW(nullptr, module, MAX_PATH);
		dll_and_manifest(std::filesystem::path(module).parent_path());
		expect(core::state().owners.empty() && core::state().natives.empty() && core::state().tasks.empty() &&
				core::state().listeners.empty() && core::state().commands.empty(),
			"all owners and registrations cleaned");
		failed_unload_is_quarantined();
		std::cout << "GSC API: " << checks << " checks passed (including separate DLL load/unload)\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "FAILED: " << e.what() << '\n';
		return 1;
	}
}
