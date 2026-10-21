#include "api.hpp"
#include "gsc/internal/bindings.hpp"
#include "gsc/internal/state.hpp"
namespace iw8x::gsc
{
	uint64_t supported_events() noexcept
	{
		return (1ull << IW8X_SCRIPTS_BEGIN_LOAD) | (1ull << IW8X_SCRIPTS_LOADED) | (1ull << IW8X_SCRIPTS_SHUTDOWN) |
			(1ull << IW8X_MAP_STARTING) | (1ull << IW8X_MAP_STARTED) | (1ull << IW8X_MAP_SHUTDOWN) | (1ull << IW8X_MAP_RESTART) |
			(1ull << IW8X_PLAYER_CONNECTING) | (1ull << IW8X_PLAYER_CONNECTED) | (1ull << IW8X_PLAYER_DISCONNECTING) |
			(1ull << IW8X_PLAYER_DISCONNECTED) | (1ull << IW8X_PLAYER_SPAWNED) | (1ull << IW8X_PLAYER_DIED) | (1ull << IW8X_PLAYER_KILLED) |
			(1ull << IW8X_SERVER_STARTING) | (1ull << IW8X_SERVER_STARTED) | (1ull << IW8X_SERVER_STOPPING) | (1ull << IW8X_SERVER_STOPPED);
	}

	namespace internal
	{
		iw8x_status subscribe(iw8x_plugin_id id, uint32_t event, iw8x_event_callback callback, void* data, iw8x_destroy_callback destroy,
			iw8x_registration* output)
		{
			if (!callback || !output || event >= IW8X_EVENT_COUNT) return IW8X_INVALID_ARGUMENT;
			if (!(supported_events() & (1ull << event))) return IW8X_UNSUPPORTED;
			const auto plugin = find_owner(id);
			lease active(plugin);
			if (!active) return IW8X_STOPPING;
			auto& s = state();
			std::lock_guard lock(s.mutex);
			if (s.listeners.size() >= 4096 || !s.next_registration) return IW8X_LIMIT;
			const auto token = s.next_registration++;
			s.listeners.emplace(token, listener{ { plugin, data, destroy }, event, callback });
			*output = token;
			return IW8X_OK;
		}
	}

	void emit(uint32_t event, entity_ref entity, iw8x_player player, iw8x_player other_player, uint32_t reason, uint32_t flags)
	{
		using namespace internal;
		if (event >= IW8X_EVENT_COUNT || !(supported_events() & (1ull << event))) return;
		slow_scope timer("event.delivery", {}, 0, event);
		std::vector<std::pair<iw8x_registration, listener>> snapshot;
		iw8x_event value{};
		value.type = event;
		value.entity = entity;
		value.player = player;
		value.other_player = other_player;
		value.reason = reason;
		value.flags = flags;
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			value.epoch = event >= IW8X_SCRIPTS_BEGIN_LOAD && event <= IW8X_SCRIPTS_SHUTDOWN ? s.epoch : s.lifecycle_epoch;
			for (const auto& [id, entry] : s.listeners)
				if (entry.event == event) snapshot.emplace_back(id, entry);
		}
		for (const auto& [id, entry] : snapshot)
		{
			lease active(entry.plugin);
			slow_scope timer("event.callback", entry.plugin->name, entry.plugin->id, id);
			if (active)
				guarded(entry.plugin,
					[&]
					{
						entry.callback(entry.data, &value);
					});
		}
	}
}
