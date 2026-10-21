#pragma once

#include <cstdint>

namespace join_cleanup_gate
{
	// The Replay native cleanup at EFC980 reads a field from the singleton at
	// F270FA0 when its frontend state byte (EEB0B20+6B0) is not 1. A null
	// singleton is an uninitialized/absent cleanup target, not a live object.
	// This also applies after matchmaking shutdown: the cleanup can run on a
	// subsequent MP -> Warzone transition when "delivered" is already false.
	constexpr bool skip(std::uint8_t state, std::uintptr_t instance) noexcept
	{
		return state != 1 && instance == 0;
	}

	// Replay EFC980 stores its one-entry cleanup list at F24EB10, with
	// count at F24EB18 and state at F24EB1C. A count greater than one would
	// interpret the count/state metadata as an additional object pointer.
	// All three native mode branches use this same list and dispatch vtable+70.
	// An invalid entry is unsafe in any native mode transition, even after
	// matchmaking shutdown. Valid destructor calls still use the stock path.
	constexpr bool reset_invalid_list(std::uint8_t listState, std::int32_t count, bool entryValid) noexcept
	{
		return listState == 2 && (count > 1 || (count == 1 && !entryValid));
	}
}
