#pragma once
#include "client/weapons/weapon_perk_pair_policy.h"

#include <algorithm>
#include <array>
#include <span>
namespace weapon_perk_pair
{
	using Weapon = std::array<uint8_t, 60>;

	struct Part
	{
		const void* asset{};
		uint8_t category{};
	};

	struct Lists
	{
		Plan perks;
		std::array<uint32_t, 15> original{};

		bool Bound(unsigned& maximum) const noexcept
		{
			if (!perks.Valid() || original[11] != perks.firstCount || original[13] != perks.kitCount || original[14] > 16) return false;
			unsigned upper = original[14];
			for (unsigned i = 0; i < 14; ++i)
			{
				if (original[i] > (i == 12 ? 511u : 255u)) return false;
				upper += original[i] != 0;
			}
			if (upper > 29) return false;
			maximum = upper;
			return true;
		}

		bool Matches(const std::array<uint32_t, 14>& converted) const noexcept
		{
			unsigned bound{};
			if (!Bound(bound)) return false;
			for (unsigned i = 0; i < 14; ++i)
				if (converted[i] != (i == 11 ? perks.Count() : original[i == 13 ? 14 : i])) return false;
			return true;
		}

		bool Selection(const Weapon& weapon) const noexcept
		{
			unsigned bound{};
			if (!Bound(bound) || weapon[38] || weapon[59] > 1) return false;
			for (unsigned i = 0; i < 13; ++i)
			{
				unsigned choice = weapon[14 + i];
				if (i == 12) choice |= unsigned(weapon[59]) << 8;
				if (choice > (i == 11 ? perks.Count() : original[i])) return false;
			}
			const unsigned mask = unsigned(weapon[12]) | (unsigned(weapon[13]) << 8);
			return (mask >> original[14]) == 0;
		}
	};

	// Use the same compound list for both views. The first-only and kit-only
	// entries point directly to the two original assets. Keep every other byte,
	// including underbarrel selection and the cosmetic category's ninth bit.
	inline bool DecodeViews(const Plan& plan, const Weapon& weapon, Weapon& first, Weapon& kit) noexcept
	{
		unsigned firstChoice{}, kitChoice{};
		if (weapon[38] || !plan.Decode(weapon[25], firstChoice, kitChoice)) return false;
		uint8_t firstEncoded{}, kitEncoded{};
		if (!plan.Encode(firstChoice, 0, 0, 0, firstEncoded) || !plan.Encode(0, kitChoice, 0, 0, kitEncoded)) return false;
		auto firstView = weapon, kitView = weapon;
		firstView[25] = firstEncoded;
		kitView[25] = kitEncoded;
		first = firstView;
		kit = kitView;
		return true;
	}

	inline unsigned Rank(uint8_t category, bool priority) noexcept
	{
		constexpr uint8_t normal[]{ 4, 9, 11, 12, 13, 8, 7, 6, 5, 10, 3, 2, 1, 0 };
		constexpr uint8_t special[]{ 4, 3, 9, 11, 12, 13, 8, 7, 6, 5, 10, 2, 1, 0 };
		const auto value = std::min<unsigned>(category, 13);
		const auto order = priority ? special : normal;
		for (unsigned i = 0; i < 14; ++i)
			if (order[i] == value) return i;
		return 14;
	}

	// Component API for filter=0 only. The original native enumerator performs
	// alternate inheritance filtering on BOTH complete views. Never enumerate an
	// isolated kit: its underbarrel context controls the original early return.
	// Enabling this for the engine still requires admitting complete lists and
	// auditing native caller output capacities, parsing, and Gunsmith consumers.
	template <class Native>
	bool Enumerate(const Plan& plan, const Weapon& weapon, bool alt, bool priority, bool leadingUnderbarrel, Native&& native,
		std::span<Part> output, unsigned& count)
	{
		Weapon first{}, kit{};
		if (!DecodeViews(plan, weapon, first, kit)) return false;
		std::array<const void*, 64> assets{};
		std::array<uint8_t, 64> categories{};
		auto size = native(first.data(), alt, assets.data(), categories.data(), 0);
		if (size > 29) return false;
		std::array<Part, 30> pending{};
		unsigned firstParts{};
		for (unsigned i = 0; i < size; ++i)
		{
			if (!assets[i] || categories[i] > 28) return false;
			if (categories[i] == 11) ++firstParts;
			pending[i] = { assets[i], categories[i] };
		}
		if (firstParts > 1 || (firstParts && !first[25])) return false;
		if (kit[25])
		{
			const auto second = native(kit.data(), alt, assets.data(), categories.data(), 0);
			if (second > 29) return false;
			unsigned kits{};
			for (unsigned i = 0; i < second; ++i)
			{
				if (!assets[i] || categories[i] > 28) return false;
				if (categories[i] != 11) continue;
				if (++kits > 1) return false;
				pending[size++] = { assets[i], 11 };
			}
		}
		if (size > output.size()) return false;
		const unsigned begin = leadingUnderbarrel && size && pending[0].category == 10 ? 1 : 0;
		for (unsigned i = begin + 1; i < size; ++i)
		{
			const auto value = pending[i];
			auto at = i;
			while (at > begin && Rank(pending[at - 1].category, priority) > Rank(value.category, priority))
			{
				pending[at] = pending[at - 1];
				--at;
			}
			pending[at] = value;
		}
		std::copy_n(pending.begin(), size, output.begin());
		count = size;
		return true;
	}
}
