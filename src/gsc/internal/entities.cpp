#include "runtime.hpp"
#include "players.hpp"

#include "engine/image.hpp"

#include <array>
#include <atomic>
namespace iw8x::gsc::internal
{
	namespace
	{
		std::array<std::atomic<uint64_t>, game::addresses::gsc_api::entity_capacity> generations{};
		std::atomic<uint64_t> sequence{ 1 };

		bool live(uint32_t index)
		{
			if (index >= generations.size()) return false;
			// Fixed Replay engine state, read only while servicing the server/game
			// boundary. Avoid VirtualQuery on every entity validation: methods already
			// validate frequently and this check sits on the hottest plugin path.
			const auto in_use = *reinterpret_cast<const uint8_t* const*>(game::at(game::addresses::gsc_api::entity_in_use_pointer));
			return in_use && in_use[index] == 1;
		}
	}

	entity_ref capture_entity(uint64_t packed)
	{
		const auto index = static_cast<uint32_t>(packed);
		// Native scr_entref_t initializes its uint32 number and uint8 class only;
		// the upper three padding bytes of the eight-byte return are unspecified.
		if (((packed >> 32) & 0xFF) || !live(index)) return {};
		auto generation = generations[index].load();
		if (!generation)
		{
			const auto next = sequence.fetch_add(1);
			if (!next) return {};
			generations[index].compare_exchange_strong(generation, next);
			generation = generations[index].load();
		}
		return { index, 0, generation };
	}

	iw8x_status validate_entity(entity_ref entity)
	{
		if (!in_game_scope()) return IW8X_WRONG_THREAD;
		if (entity.reserved || !entity.generation || entity.index >= generations.size() ||
			entity.generation != generations[entity.index].load() || !live(entity.index))
			return IW8X_STALE;
		return IW8X_OK;
	}

	void invalidate_entity(uint32_t index) noexcept
	{
		if (index < generations.size()) generations[index].store(0);
		clear_player_entity(index);
	}

	void invalidate_entities() noexcept
	{
		for (auto& generation : generations)
			generation.store(0);
		invalidate_player_entities();
	}
}
