#pragma once
#include <algorithm>
#include <charconv>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace progression
{
	using Table = std::vector<std::vector<std::string>>;

	inline int Number(const std::string& text, int fallback = -1)
	{
		int value = 0;
		const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
		return r.ec == std::errc{} && r.ptr == text.data() + text.size() && value >= 0 ? value : fallback;
	}

	inline std::string Cell(const std::vector<std::string>& row, size_t column)
	{
		return column < row.size() ? row[column] : std::string{};
	}

	struct Weapon
	{
		int maximumRank = 0, capXP = 0;
	};

	struct WeaponUnlock
	{
		std::string weapon;
		int rank = 0;
	};

	// This catalog contains only grants explicitly present in TU19 progression CSVs.
	// Store/Battle Pass inventories are deliberately not sources of earned grants.
	struct Catalog
	{
		std::set<int> initial;
		std::map<int, int> military;
		std::map<std::string, Weapon> weapons;
		std::map<int, WeaponUnlock> weaponUnlocks;
		std::vector<int> weaponThresholds;

		bool Load(const std::function<Table(const std::string&)>& read)
		{
			const auto ranks = read("mp/progression/progressionitems.csv");
			const auto defaults = read("mp/progression/initialitems.csv");
			const auto xp = read("mp/weaponranktable.csv");
			const auto ids = read("loot/weapon_ids.csv");
			if (ranks.empty() || defaults.empty() || xp.empty() || ids.empty()) return false;
			Catalog next;
			for (const auto& row : defaults)
			{
				const int id = Number(Cell(row, 0));
				if (id > 0) next.initial.insert(id);
			}
			for (const auto& row : ranks)
			{
				const int rank = Number(Cell(row, 0));
				if (rank < 0 || rank > 54) continue;
				for (size_t c = 2; c < row.size(); ++c)
				{
					const int id = Number(row[c]);
					if (id > 0)
					{
						auto [at, inserted] = next.military.emplace(id, rank);
						if (!inserted) at->second = std::min(at->second, rank);
					}
				}
			}
			for (const auto& row : xp)
			{
				const int index = Number(Cell(row, 0)), threshold = Number(Cell(row, 1));
				if (index != int(next.weaponThresholds.size()) || threshold < 0 || (index && threshold <= next.weaponThresholds.back()))
					return false;
				next.weaponThresholds.push_back(threshold);
			}
			for (const auto& row : ids)
			{
				const auto ref = Cell(row, 1);
				if (Cell(row, 6) != ref + "_variant_0" || !ref.starts_with("iw8_") || next.weapons.contains(ref)) continue;
				const auto levels = read("mp/gunsmith/" + ref.substr(4) + "_progression.csv");
				if (levels.empty()) continue;
				int maxRank = 0;
				for (const auto& level : levels)
				{
					const int rank = Number(Cell(level, 0));
					if (rank < 0 || rank >= int(xp.size())) continue;
					maxRank = std::max(maxRank, rank);
					// Four-column groups match CSV.gunsmithProgressionTable. A
					// challenge ID gates its rewards; its level alone is insufficient.
					for (size_t c = 1; c < level.size(); c += 4)
					{
						const int id = Number(Cell(level, c));
						if (id > 0 && Cell(level, c + 1).empty() && Cell(level, c + 2).empty())
							next.weaponUnlocks.emplace(id, WeaponUnlock{ ref, rank });
					}
				}
				const int cap = Number(Cell(xp[maxRank], 3));
				if (cap <= 0) return false;
				next.weapons.emplace(ref, Weapon{ maxRank, cap });
			}
			if (next.weapons.empty() || next.military.empty()) return false;
			*this = std::move(next);
			return true;
		}

		int WeaponRank(const std::string& weapon, int xp) const
		{
			const auto it = weapons.find(weapon);
			if (it == weapons.end()) return -1;
			const int rank =
				std::max(0, int(std::upper_bound(weaponThresholds.begin(), weaponThresholds.end(), xp) - weaponThresholds.begin()) - 1);
			return std::min(rank, it->second.maximumRank);
		}

		bool Owns(int item, int militaryRank, const std::function<int(const std::string&)>& weaponXP) const
		{
			if (initial.contains(item)) return true;
			if (const auto it = military.find(item); it != military.end() && militaryRank >= it->second) return true;
			if (const auto it = weaponUnlocks.find(item); it != weaponUnlocks.end())
				return WeaponRank(it->second.weapon, weaponXP(it->second.weapon)) >= it->second.rank;
			return false;
		}
	};
}
