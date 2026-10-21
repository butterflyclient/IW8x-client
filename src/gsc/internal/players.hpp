#pragma once
#include "plugin/sdk/abi.h"
#include "plugin/sdk/value.hpp"

#include <string_view>
#include <vector>
namespace iw8x::gsc::internal
{

	struct player_snapshot
	{
		iw8x_player player{};
		iw8x_entity entity{};
		bool connected{};

		explicit operator bool() const noexcept
		{
			return player.generation != 0;
		}
	};

	// Registry primitives are engine-pointer free and are also used by unit tests.
	player_snapshot prepare_player(uint32_t client_num, std::string_view name, uint64_t xuid = 0);
	player_snapshot activate_player(uint32_t client_num);
	player_snapshot find_player(uint32_t client_num, bool include_prepared = false);
	player_snapshot associate_player_entity(uint32_t client_num, iw8x_entity entity);
	player_snapshot disconnect_player(uint32_t client_num);
	player_snapshot begin_player_disconnect(uint32_t client_num);
	void clear_player_entity(uint32_t entity_num) noexcept;
	std::vector<iw8x_player> connected_players();
	void invalidate_player_entities() noexcept;
	void invalidate_players() noexcept;

	// Replay engine adapters copy all required data before returning and never
	// expose engine pointers through the public ABI.
	player_snapshot prepare_client_player(void* client);
	void* client_at(uint32_t index) noexcept;
	uint8_t client_state(void* client) noexcept;
	std::string read_client_name(void* client);
	player_snapshot find_client_player(void* client, bool include_prepared = false);
	player_snapshot associate_entity_player(void* entity);
	uint32_t player_entity_index(void* entity) noexcept;

	iw8x_status validate_player(iw8x_player player);
	iw8x_status read_player_name(iw8x_player player, value& output);
	iw8x_status read_player_entity(iw8x_player player, iw8x_entity& output);

	// Event transitions operate on copied identities; callbacks never run under
	// the player or plugin registry mutex. Engine wrappers own original-call timing.
	player_snapshot connecting_player(uint32_t index, std::string_view name);
	void connected_player(uint32_t index, std::string_view name);
	player_snapshot disconnecting_player(uint32_t index);
	void disconnected_player(player_snapshot before);
	void spawned_player(uint32_t index, iw8x_entity entity);
	void died_player(player_snapshot victim, player_snapshot killer, uint32_t reason);

} // namespace iw8x::gsc::internal
