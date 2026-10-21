#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace xp_tokens
{
	// TU19 loot/consumable_ids.csv achievement IDs and duration localization tiers.
	// Timers run in real time, including menus and time spent offline.
	struct Definition
	{
		int item, achievement;
		std::string_view kind;
		uint32_t seconds;
	};

	inline constexpr std::array<Definition, 13> Definitions{ { { 200002, 1000020, "player", 900 }, { 200032, 1000024, "player", 1800 },
		{ 200035, 1000027, "player", 2700 }, { 200038, 1000030, "player", 3600 }, { 200041, 1000033, "player", 31536000 },
		{ 200030, 1000021, "weapon", 900 }, { 200033, 1000025, "weapon", 1800 }, { 200036, 1000028, "weapon", 2700 },
		{ 200039, 1000031, "weapon", 3600 }, { 200043, 1000041, "battle", 900 }, { 200044, 1000042, "battle", 1800 },
		{ 200045, 1000043, "battle", 3600 }, { 200046, 1000044, "battle", 2700 } } };

	inline const Definition* Find(int item)
	{
		for (const auto& d : Definitions)
			if (d.item == item) return &d;
		return nullptr;
	}

	inline constexpr int TrialTicket = 200005; // TU19 loot/consumable_ids.csv

	inline bool Counted(int item)
	{
		return item == TrialTicket || Find(item);
	}

	inline const Definition* Achievement(int id)
	{
		for (const auto& d : Definitions)
			if (d.achievement == id) return &d;
		return nullptr;
	}
}
