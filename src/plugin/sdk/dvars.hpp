#pragma once
#include "dvar_names_replay.hpp"
namespace iw8x::dvar_names
{
	// Allocation-free, exact and case-sensitive. Returned strings have static
	// lifetime and are NUL-terminated. Unknown names never silently pass through.
	inline constexpr std::string_view resolve(std::string_view name, bool allow_hashed = true) noexcept
	{
		size_t first = 0, last = std::size(replay);
		while (first < last)
		{
			const auto middle = first + (last - first) / 2;
			if (replay[middle].readable < name)
				first = middle + 1;
			else
				last = middle;
		}
		if (first < std::size(replay) && replay[first].readable == name) return replay[first].retail;
		if (!allow_hashed) return {};
		first = 0;
		last = std::size(retail_names);
		while (first < last)
		{
			const auto middle = first + (last - first) / 2;
			if (retail_names[middle] < name)
				first = middle + 1;
			else
				last = middle;
		}
		return first < std::size(retail_names) && retail_names[first] == name ? retail_names[first] : std::string_view{};
	}
}
