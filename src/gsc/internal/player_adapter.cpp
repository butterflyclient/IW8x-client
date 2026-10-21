#include "players.hpp"
#include "runtime.hpp"

#include "engine/image.hpp"

#include <cstring>
namespace iw8x::gsc::internal
{
	namespace native = game::addresses::gsc_api;

	bool gameplay_server() noexcept
	{
		const auto globals = *reinterpret_cast<const uint8_t* const*>(game::at(native::persistent_globals));
		return globals && globals[native::persistent_frontend] == 0;
	}

	namespace
	{
		constexpr uint32_t max_clients = native::client_capacity;
		constexpr uintptr_t clients_rva = native::clients;
		constexpr uintptr_t client_count_rva = native::client_count;

		uint32_t client_index(void* client) noexcept
		{
			if (!client) return UINT32_MAX;
			const auto count = *reinterpret_cast<const uint32_t*>(game::at(client_count_rva));
			if (count > max_clients) return UINT32_MAX;
			const auto table = reinterpret_cast<void* const*>(game::at(clients_rva));
			for (uint32_t i = 0; i < count; ++i)
				if (table[i] == client) return i;
			return UINT32_MAX;
		}
	}

	void* client_at(uint32_t index) noexcept
	{
		if (index >= max_clients) return nullptr;
		const auto count = *reinterpret_cast<const uint32_t*>(game::at(client_count_rva));
		if (count > max_clients || index >= count) return nullptr;
		return reinterpret_cast<void* const*>(game::at(clients_rva))[index];
	}

	uint8_t client_state(void* client) noexcept
	{
		return client ? *(static_cast<const uint8_t*>(client) + native::client_state) : 0;
	}

	std::string read_client_name(void* client)
	{
		if (!client) return {};
		const auto* name = static_cast<const char*>(client) + native::client_name;
		return { name, strnlen_s(name, native::client_name_capacity - 1) };
	}

	player_snapshot prepare_client_player(void* client)
	{
		const auto index = client_index(client);
		return index == UINT32_MAX ? player_snapshot{} : prepare_player(index, read_client_name(client));
	}

	player_snapshot find_client_player(void* client, bool include_prepared)
	{
		const auto index = client_index(client);
		const auto existing = find_player(index, true);
		if (!existing) return {};
		prepare_player(index, read_client_name(client), existing.player.xuid);
		return find_player(index, include_prepared);
	}

	uint32_t player_entity_index(void* entity) noexcept
	{
		if (!entity) return UINT32_MAX;
		const auto index = *static_cast<const int16_t*>(entity);
		if (index < 0 || index >= max_clients) return UINT32_MAX;
		return static_cast<uint32_t>(index);
	}

	player_snapshot associate_entity_player(void* entity)
	{
		const auto index = player_entity_index(entity);
		if (index == UINT32_MAX) return {};
		const auto ref = capture_entity(index);
		return ref.generation ? associate_player_entity(index, ref) : player_snapshot{};
	}
}
