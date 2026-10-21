#pragma once
#include <cstdint>
#include <span>
namespace playlists
{
	struct WeightedEntry
	{
		int index;
		uint8_t weight;
		bool available;
	};

	inline uint32_t EntryWeight(std::span<const WeightedEntry> entries)
	{
		uint32_t total = 0;
		for (const auto& entry : entries)
			if (entry.available) total += entry.weight;
		return total;
	}

	inline int PickEntry(std::span<const WeightedEntry> entries, uint32_t ticket)
	{
		const auto total = EntryWeight(entries);
		if (!total || ticket >= total) return -1;
		for (const auto& entry : entries)
			if (entry.available)
			{
				if (ticket < entry.weight) return entry.index;
				ticket -= entry.weight;
			}
		return -1;
	}
}
