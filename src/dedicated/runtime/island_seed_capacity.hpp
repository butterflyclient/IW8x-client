#pragma once

#include <cstdint>

namespace dedicated::detail::island_seed_capacity
{
	// Real Replay 1.20.4 ScriptableSv reserved instances are a shared pool:
	// players, ordinary GSC gameplay and world loot compete for these slots.
	// Do not force-spawn a 2676-item catalog into a smaller *live* pool.
	// Retain headroom for ordinary engine gameplay and pause (not discard)
	// the remaining authentic source placement cursor until reclaimed.
	inline constexpr std::int32_t player_gameplay_headroom = 64;

	constexpr bool can_spawn(const std::int32_t native_remaining) noexcept
	{
		return native_remaining > player_gameplay_headroom;
	}

	constexpr bool paused(const std::int32_t native_remaining) noexcept
	{
		return !can_spawn(native_remaining);
	}

	// Replay consumes reserved ScriptableSv slots outside this generator too.
	// If the pool falls below its safe budget *after* the real 1.20.4 loot
	// draw, pause while retaining that exact native item name; a resumed
	// placement must not reroll it or create a duplicate instance.
	constexpr std::uint32_t resume_world_phase(const bool drawn_item_pending) noexcept
	{
		return drawn_item_pending ? 3u : 2u;
	}

	// The capacity-paused native generation cursor is not actively invoking
	// the VM: opening real ScriptableSv objects / reclaiming inventory can
	// proceed before an operator explicitly resumes the source catalog.
	constexpr bool work_active(const std::uint32_t phase) noexcept
	{
		return phase == 1 || phase == 2 || phase == 3;
	}
}
