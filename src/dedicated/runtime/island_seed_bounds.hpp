#pragma once

#include <cstddef>
#include <cstdint>

namespace dedicated::detail::island_seed_bounds
{
	// Validate BEFORE subscripting the original Island placement array.
	// A completed cache batch may leave position == target == 866. It must
	// never evaluate placement[866], even if normal completion already drops
	// the phase flag. This also rejects inconsistent/malformed test state.
	constexpr bool valid(const std::uint32_t position, const std::uint32_t target,
		const std::size_t inventory_size) noexcept
	{
		return target > 0 && target <= inventory_size && position < target && position < inventory_size;
	}
}
