#include "gsc/internal/players.hpp"
#include "api.hpp"
#include "gsc/internal/bindings.hpp"
#include "ownership.hpp"
#include "gsc/internal/diagnostics.hpp"
#ifdef IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE
#include "dedicated/runtime/commands.hpp"
#endif

#include <array>
#include <limits>
#include <mutex>
#include <string>
namespace iw8x::gsc::internal
{
	namespace
	{

		constexpr uint32_t kMaxClients = 200;

		struct slot
		{
			uint64_t generation{};
			uint64_t xuid{};
			bool present{};
			bool connected{};
			bool disconnecting{};
			std::string name;
			iw8x_entity entity{};
		};

		std::array<slot, kMaxClients> players{};
		runtime_mutex players_mutex{ "players" };
		uint64_t next_generation = 1;

		iw8x_player make_ref(const uint32_t index, const slot& state)
		{
			return { index, 0, state.generation, state.xuid };
		}

		player_snapshot snapshot(const uint32_t index, const slot& state)
		{
			return { make_ref(index, state), state.entity, state.connected };
		}

		void publish(uint32_t event, player_snapshot subject, player_snapshot other = {}, uint32_t reason = 0) noexcept
		{
			if (!subject) return;
			try
			{
				// These network/death transitions may run while the VM is active.
				// Copied player access is safe; VM work must be scheduled to RunFrame.
				game_scope scope(false);
				emit(event, subject.entity, subject.player, other.player, reason,
					event == IW8X_PLAYER_DIED || event == IW8X_PLAYER_KILLED ? IW8X_EVENT_HAS_REASON : 0);
			}
			catch (const std::exception& e)
			{
				report(nullptr, e.what());
			}
			catch (...)
			{
				report(nullptr, "player lifecycle dispatch failed");
			}
		}

	} // namespace

	player_snapshot prepare_player(const uint32_t client_num, const std::string_view name, const uint64_t xuid)
	{
		if (client_num >= kMaxClients) return {};
		std::lock_guard lock(players_mutex);
		auto& state = players[client_num];
		if (!state.present)
		{
			if (!next_generation || next_generation == std::numeric_limits<uint64_t>::max()) return {};
			state = {};
			state.generation = next_generation++;
			state.xuid = xuid;
			state.present = true;
		}
		// Identity is immutable for the connection, including while prepared.
		if (!name.empty()) state.name.assign(name);
		return snapshot(client_num, state);
	}

	player_snapshot activate_player(const uint32_t client_num)
	{
		if (client_num >= kMaxClients) return {};
		std::lock_guard lock(players_mutex);
		auto& state = players[client_num];
		if (!state.present || !state.generation || state.connected || state.disconnecting) return {};
		state.connected = true;
		return snapshot(client_num, state);
	}

	player_snapshot find_player(const uint32_t client_num, const bool include_prepared)
	{
		if (client_num >= kMaxClients) return {};
		std::lock_guard lock(players_mutex);
		const auto& state = players[client_num];
		if (!state.present || !state.generation || (!include_prepared && !state.connected)) return {};
		return snapshot(client_num, state);
	}

	player_snapshot associate_player_entity(const uint32_t client_num, const iw8x_entity entity)
	{
		if (client_num >= kMaxClients || !entity.generation) return {};
		std::lock_guard lock(players_mutex);
		auto& state = players[client_num];
		if (!state.present || !state.generation) return {};
		state.entity = entity;
		return snapshot(client_num, state);
	}

	player_snapshot disconnect_player(const uint32_t client_num)
	{
		if (client_num >= kMaxClients) return {};
		std::lock_guard lock(players_mutex);
		auto& state = players[client_num];
		if (!state.present || !state.generation) return {};
		const auto old = snapshot(client_num, state);
		state.present = false;
		state.connected = false;
		state.entity = {};
		state.xuid = 0;
		return old;
	}

	player_snapshot begin_player_disconnect(uint32_t client_num)
	{
		if (client_num >= kMaxClients) return {};
		std::lock_guard lock(players_mutex);
		auto& state = players[client_num];
		if (!state.present || state.disconnecting) return {};
		state.disconnecting = true;
		return snapshot(client_num, state);
	}

	void clear_player_entity(uint32_t entity_num) noexcept
	{
		std::lock_guard lock(players_mutex);
		for (auto& state : players)
			if (state.entity.generation && state.entity.index == entity_num) state.entity = {};
	}

	std::vector<iw8x_player> connected_players()
	{
		std::lock_guard lock(players_mutex);
		std::vector<iw8x_player> result;
		result.reserve(kMaxClients);
		for (uint32_t i = 0; i < kMaxClients; ++i)
			if (players[i].present && players[i].connected) result.push_back(make_ref(i, players[i]));
		return result;
	}

	void invalidate_player_entities() noexcept
	{
		try
		{
			std::lock_guard lock(players_mutex);
			for (auto& state : players)
				state.entity = {};
		}
		catch (...)
		{
		}
	}

	void invalidate_players() noexcept
	{
		try
		{
			std::lock_guard lock(players_mutex);
			for (auto& state : players)
			{
				state.present = false;
				state.connected = false;
				state.entity = {};
				state.xuid = 0;
			}
		}
		catch (...)
		{
		}
	}

	iw8x_status validate_player(const iw8x_player player)
	{
		if (player.reserved || !player.generation || player.client_num >= kMaxClients) return IW8X_STALE;
		std::lock_guard lock(players_mutex);
		const auto& state = players[player.client_num];
		return state.present && state.generation == player.generation && state.xuid == player.xuid ? IW8X_OK : IW8X_STALE;
	}

	iw8x_status read_player_name(const iw8x_player player, value& output)
	{
		if (player.reserved || !player.generation || player.client_num >= kMaxClients) return IW8X_STALE;
		std::lock_guard lock(players_mutex);
		const auto& state = players[player.client_num];
		if (!state.present || state.generation != player.generation || state.xuid != player.xuid) return IW8X_STALE;
		output = value(state.name);
		return IW8X_OK;
	}

	iw8x_status read_player_entity(const iw8x_player player, iw8x_entity& output)
	{
		output = {};
		if (player.reserved || !player.generation || player.client_num >= kMaxClients) return IW8X_STALE;
		std::lock_guard lock(players_mutex);
		const auto& state = players[player.client_num];
		if (!state.present || state.generation != player.generation || state.xuid != player.xuid) return IW8X_STALE;
		if (!state.entity.generation) return IW8X_NOT_FOUND;
		output = state.entity;
		return IW8X_OK;
	}

	player_snapshot connecting_player(uint32_t index, std::string_view name)
	{
		if (find_player(index, true)) return {}; // Map re-entry is not a new connection.
		auto candidate = prepare_player(index, name);
		publish(IW8X_PLAYER_CONNECTING, candidate);
		return candidate;
	}

	void connected_player(uint32_t index, std::string_view name)
	{
		if (!find_player(index, true)) return;
		prepare_player(index, name);
		publish(IW8X_PLAYER_CONNECTED, activate_player(index));
	}

	player_snapshot disconnecting_player(uint32_t index)
	{
		auto before = begin_player_disconnect(index);
		if (before.connected) publish(IW8X_PLAYER_DISCONNECTING, before);
		return before;
	}

	void disconnected_player(player_snapshot before)
	{
		if (!before) return;
		const auto current = find_player(before.player.client_num, true);
		if (!current || current.player.generation != before.player.generation) return;
		auto old = disconnect_player(before.player.client_num);
		old.entity = {};
		if (before.connected) publish(IW8X_PLAYER_DISCONNECTED, old);
	}

	void spawned_player(uint32_t index, iw8x_entity entity)
	{
		const auto spawned = associate_player_entity(index, entity);
	#ifdef IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE
			if (spawned.connected) dedicated::detail::NotifyIslandOriginalPlayerSpawned(index);
	#endif
		if (spawned.connected) publish(IW8X_PLAYER_SPAWNED, spawned);
	}

	void died_player(player_snapshot victim, player_snapshot killer, uint32_t reason)
	{
		if (!victim.connected) return;
		if (!killer.connected || killer.player.generation == victim.player.generation) killer = {};
		// Script death processing can free an entity before PlayerDie returns.
		(void)read_player_entity(victim.player, victim.entity);
		if (killer) (void)read_player_entity(killer.player, killer.entity);
		publish(IW8X_PLAYER_DIED, victim, killer, reason);
		if (killer) publish(IW8X_PLAYER_KILLED, killer, victim, reason);
	}

} // namespace iw8x::gsc::internal
