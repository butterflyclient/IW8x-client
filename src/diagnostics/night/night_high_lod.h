#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace night_builtins
{
	// The native collector stores at most 2,048 uint16 entity numbers. Keep its
	// ordering and original entries while merging explicitly forced entities.
	inline bool MergeHighLod(std::span<const std::uint16_t> native, std::span<const std::uint16_t> forced, unsigned entityCount,
		std::array<std::uint16_t, 2048>& output, unsigned& count)
	{
		if (entityCount > 2048 || native.size() > 2048 || forced.size() > 2048) return false;
		std::array<bool, 2048> present{};
		for (auto id : native)
		{
			if (id >= entityCount || present[id]) return false;
			present[id] = true;
		}
		for (auto id : forced)
		{
			if (id >= entityCount) return false;
			present[id] = true;
		}
		std::array<std::uint16_t, 2048> result{};
		unsigned size = 0;
		for (unsigned id = 0; id < entityCount; ++id)
			if (present[id]) result[size++] = static_cast<std::uint16_t>(id);
		output = result;
		count = size;
		return true;
	}
}
