#pragma once
#include "../ownership.hpp"
#include "diagnostics.hpp"

#include <array>
#include <atomic>

#include "plugin/sdk/value.hpp"

#include <chrono>
#include <map>
#include <vector>
namespace iw8x::gsc::internal
{
	struct owned_data
	{
		std::shared_ptr<owner> plugin;
		void* data{};
		iw8x_destroy_callback destroy{};
	};

	struct native_entry : owned_data
	{
		iw8x_native_callback callback{};
	};

	struct listener : owned_data
	{
		uint32_t event{};
		iw8x_event_callback callback{};
	};

	struct task : owned_data
	{
		iw8x_registration id{};
		uint32_t scope{};
		uint64_t epoch{};
		std::chrono::steady_clock::time_point due;
		iw8x_task_callback work{}, complete{};
		bool worker{}, running{}, canceled{};
	};

	struct resource : owned_data
	{
		uint64_t type{}, epoch{};
		uint32_t scope{}, generation{ 1 }, borrows{};
		bool occupied{}, retired{};
	};

	struct command_entry : owned_data
	{
		iw8x_command_callback callback{};
		uint64_t bridge_token{};
	};

	struct script_entry
	{
		std::shared_ptr<owner> plugin;
		std::string script;
		uint32_t function_token{};
		uint32_t result_type{};
		uint32_t handle{};
		uint64_t epoch{};
		std::string label;
	};

	struct registry
	{
		runtime_mutex mutex{ "registry" };
		std::map<iw8x_plugin_id, std::shared_ptr<owner>> owners;
		std::map<std::pair<bool, std::string>, native_entry> natives;
		std::map<iw8x_registration, listener> listeners;
		std::map<iw8x_registration, std::shared_ptr<task>> tasks;
		// Conservative wakeup hints: cancellation may leave an earlier hint, never
		// a later one. Idle pumps do not walk delayed tasks (e.g. welcome messages).
		std::array<std::chrono::steady_clock::time_point, 2> task_wakeup{};
		std::atomic<int64_t> task_wakeup_ticks[2]{};
		std::map<std::string, command_entry> commands;
		std::map<iw8x_registration, script_entry> scripts;
		std::vector<resource> resources;
		uint64_t next_owner = 1, next_registration = 1, epoch = 1, lifecycle_epoch = 1;
		uint64_t script_generation = 1;
		bool map_active = false;
	};

	registry& state();
	bool destroy_data(const owned_data& data) noexcept;
	std::string checked_name(iw8x_string name);
	iw8x_status register_native(iw8x_plugin_id, iw8x_string, uint32_t, iw8x_native_callback, void*, iw8x_destroy_callback);
	iw8x_status subscribe(iw8x_plugin_id, uint32_t, iw8x_event_callback, void*, iw8x_destroy_callback, iw8x_registration*);
	iw8x_status schedule(iw8x_plugin_id, uint32_t, uint32_t, iw8x_task_callback, void*, iw8x_destroy_callback, iw8x_registration*);
	iw8x_status async(iw8x_plugin_id, uint32_t, iw8x_task_callback, iw8x_task_callback, void*, iw8x_destroy_callback, iw8x_registration*);
	iw8x_status cancel_task(iw8x_plugin_id, iw8x_registration);
	iw8x_status create_handle(iw8x_plugin_id, uint64_t, uint32_t, void*, iw8x_destroy_callback, iw8x_native_handle*);
	iw8x_status with_handle(iw8x_plugin_id, iw8x_native_handle, uint64_t, iw8x_resource_callback, void*);
	iw8x_status release_handle(iw8x_plugin_id, iw8x_native_handle, uint64_t);
	iw8x_status add_command(iw8x_plugin_id, iw8x_string, iw8x_command_callback, void*, iw8x_destroy_callback);
	iw8x_status register_script(iw8x_plugin_id, iw8x_string, uint32_t, uint32_t, iw8x_registration*);
	uint32_t script_function_token(iw8x_string);
	uint32_t script_module_token(std::string_view normalized_path);
	std::string_view script_function_name(uint32_t token);
	void resolve_script_exports(void*) noexcept;
	iw8x_status registered_script_handle(iw8x_plugin_id, iw8x_registration, uint32_t&, uint32_t&, std::string_view* = nullptr) noexcept;
	void invalidate_script_exports() noexcept;
	void erase_command_bridge(const std::string& name, uint64_t token);
	void retire_resource(resource& item);
	void pump_tasks(bool worker);
	void end_map();
}
