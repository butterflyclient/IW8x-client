#pragma once
#include "plugin/sdk/abi.h"
#include "plugin/sdk/value.hpp"

#include <span>
#include <chrono>
namespace iw8x::gsc
{
	const iw8x_plugin_host& host_api();
	uint64_t supported_events() noexcept;

	// Internal calls only; no pointer to these appears in the plugin ABI.
	struct invocation_result
	{
		iw8x_status status{};
		value result;
		std::string error;
	};

	invocation_result invoke(std::string_view name, bool method, entity_ref self, std::span<const value> args);
	void emit(uint32_t event, entity_ref entity = {}, iw8x_player player = {}, iw8x_player other_player = {}, uint32_t reason = 0,
		uint32_t flags = 0);
	void scripts_begin_load() noexcept;
	void scripts_loaded() noexcept;
	void scripts_shutdown() noexcept;
	void map_starting() noexcept;
	void map_ready() noexcept;
	void map_shutdown() noexcept;
	void map_restart() noexcept;
	void server_starting() noexcept;
	void server_ready() noexcept;
	void server_stopping() noexcept;
	void server_stopped() noexcept;
	void pump_game() noexcept;
	void pump_worker() noexcept;
	void set_worker_wakeup(void (*notify)() noexcept) noexcept;
	void notify_worker() noexcept;
	std::chrono::milliseconds worker_delay() noexcept;
}
