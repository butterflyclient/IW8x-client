#pragma once
#include "bindings.hpp"
#include "native_dispatch_stats.hpp"
#include "players.hpp"

#include "engine/inline_hook.h"
#include "engine/addresses.hpp"

#include <filesystem>
namespace iw8x::gsc::internal
{
	using native_dispatch_snapshot = native_dispatch_stats<game::addresses::gsc_api::plugin_capacity>::snapshot;
	native_dispatch_snapshot take_native_dispatch_stats() noexcept;
	[[nodiscard]] hook::Status install_runtime();
	bool runtime_ready() noexcept;
	bool gameplay_server() noexcept;
	using client_connect_fn = const char* (*)(int, uint32_t);
	// Replay runtime owns the G_ClientMP_Connect detour; caller is captured at that
	// engine entry so the DirectConnect acceptance edge remains distinguishable.
	const char* observe_client_connect(int index, uint32_t script_id, const void* caller, client_connect_fn original);
	void initialize_tables(const std::filesystem::path& root);
	iw8x_status reserve_native(bool method, std::string_view plugin, std::string_view name);
	void restore_port_tables(void* context);
	void publish_tables(void* context);
	void dispatch_native(void* context, bool method, uint32_t slot, uint64_t self) noexcept;
	std::string native_name(bool method, uint32_t slot);
	void* server_context();
	entity_ref capture_entity(uint64_t packed);
	iw8x_status validate_entity(entity_ref entity);
	void invalidate_entity(uint32_t index) noexcept;
	void invalidate_entities() noexcept;
	iw8x_status notify_native(entity_ref, std::string_view, std::span<const value>);
	void prepare_return(void* context, const value& result);
	void write_return(void* context, const value& result);
	std::vector<value> read_arguments(void* context);
	bool vm_idle(void* context);
	iw8x_status validate_return(const value&);
	bool typed_dvars_available();
	iw8x_status read_typed_dvar(std::string_view, uint32_t, value&);
	bool invocation_available();
	iw8x_status call_builtin_native(bool, entity_ref, std::string_view, std::span<const value>, value&);
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
		// IW8 1.20.4 ScriptableSv is native entity *class 8*, never a public
		// IW8X_ENTITY/class-0 handle. Keep its ownership and lifetime entirely
		// inside the dedicated runtime, and never marshal it as a plugin entity.
		struct native_scriptable_spawn
		{
			uint32_t instance_index{};
			uint32_t entity_class{};
			uint64_t map_epoch{};
			uint64_t script_generation{};
		};
		iw8x_status spawn_island_native_scriptable(std::span<const value> args, native_scriptable_spawn& instance);
		// Genuine Replay1.20 native Scriptable class-8 reference; caller-supplied
		// read-only arguments are validated by the exact method whitelist.
		iw8x_status query_island_native_scriptable(const native_scriptable_spawn& instance,
			std::string_view method, value& result, std::span<const value> args = {});
			// Native Replay1.20 class-8 ScriptableSv state transition, isolated
			// from the public plugin entity API. Only the authentic "opening"
			// state may be requested and only after caller verifies eligibility.
			iw8x_status begin_island_native_cache_opening(const native_scriptable_spawn& instance,
				std::string_view part);
			// Replay1.20 native release is only available to the Island-owned
			// Scriptable class-8 reference after an independently confirmed
			// successful pickup/inventory commit. Never called on a merely opened
			// or still-interactable cache. Not exposed through the plugin ABI.
			iw8x_status release_consumed_island_native_scriptable(
				const native_scriptable_spawn& instance, bool acquisition_committed);
#endif
	bool script_invocation_available();
	void invalidate_script_exports() noexcept;
	void resolve_script_exports(void*) noexcept;
	uint32_t resolve_script_native(void*, const char*, uint32_t);
	iw8x_status registered_script_handle(iw8x_plugin_id, iw8x_registration, uint32_t&, uint32_t&, std::string_view*) noexcept;
	iw8x_status call_script_native(uint32_t, uint32_t, std::span<const value>, value&);
	iw8x_status call_script_on_native(entity_ref, uint32_t, uint32_t, std::span<const value>, value&);
	void invocation_trampoline(void*);
	inline constexpr uint32_t invocation_builtin_id = game::addresses::gsc_api::invocation_builtin_id;
}
