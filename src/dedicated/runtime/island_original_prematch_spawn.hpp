#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dedicated::detail::island_original_prematch_spawn
{
	struct position { float x, y, z; };
	struct prematch_location { position center; float min_radius, radius; };

	// IW8 1.64.5 scripts/mp/maps/mp_sm_island_1/mp_sm_island_1.gsc,
	// brinit(): level.delete_script_object / br.gsc::createspawnlocation.
	// Important: these are *player prematch* positions, not world loot
	// SpawnGroups, and not the different map scripts/mp/maps/mp_wz_island.
	inline constexpr std::array<prematch_location, 4> source_164_locations{{
		{{1486.14f, 2135.65f, 1000.0f}, 0.0f, 5500.0f},
		{{6048.26f, 237.274f, 1000.0f}, 0.0f, 5700.0f},
		{{-646.339f, 452.373f, 1000.0f}, 0.0f, 5000.0f},
		{{-5709.13f, -242.515f, 1000.0f}, 0.0f, 4000.0f}
	}};

	// 1.64.5 scripts/mp/gametypes/br_public.gsc::getinfilspawnoffset.
	// Infiltration mode defaults to 12000; map-selection mode uses 2000.
	inline constexpr float infiltration_offset = 12000.0f;
	inline constexpr float map_selection_offset = 2000.0f;

	constexpr position source_spawn(const std::size_t slot, const bool map_selection = false)
	{
		const auto p = source_164_locations[slot % source_164_locations.size()].center;
		return {p.x, p.y, p.z + (map_selection ? map_selection_offset : infiltration_offset)};
	}

		// This is a conservative detector for Replay's documented GSC fallback
		// origin (0,0,0) or demonstrably far below the Island map. The October
		// 20 native ClientSpawn trace additionally captured a fallback bot at
		// (31.6, 10.7, -245.4) repeatedly falling to Z=-295.9 during respawn,
		// not at any of the four original prematch centers (Z=1000). Recognize
		// only this narrow near-world-origin/subterrain failure. Do not
		// arbitrarily relocate ordinary ground spawns or parachuting players.
	inline bool invalid_fallback_position(const position p)
	{
		if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
		const bool zero = std::fabs(p.x) < 0.5f && std::fabs(p.y) < 0.5f && std::fabs(p.z) < 0.5f;
		const bool under_origin_fallback = std::fabs(p.x) < 64.0f && std::fabs(p.y) < 64.0f &&
			p.z <= -128.0f;
		const bool deeply_below_island = p.z < -2500.0f &&
			std::fabs(p.x) < 16000.0f && std::fabs(p.y) < 16000.0f;
		return zero || under_origin_fallback || deeply_below_island;
	}
}
