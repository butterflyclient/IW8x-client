#pragma once
#include <array>
#include <cstdint>
namespace weapon_perk_pair
{
	// A component for admitted original perk lists, not a global Weapon layout
	// change. The compound lists and native/menu/string adapters must share it.
	struct Plan
	{
		uint32_t firstCount{}, kitCount{};

		bool Valid() const noexcept
		{
			return kitCount && firstCount < 256 && kitCount < 256 && (firstCount + 1) * (kitCount + 1) <= 256;
		}

		unsigned Count() const noexcept
		{
			return Valid() ? unsigned((uint64_t(firstCount) + 1) * (uint64_t(kitCount) + 1) - 1) : 0;
		}

		bool Encode(unsigned first, unsigned kit, unsigned firstVariation, unsigned kitVariation, uint8_t& output) const noexcept
		{
			if (!Valid() || first > firstCount || kit > kitCount || firstVariation || kitVariation) return false;
			output = uint8_t(first * (kitCount + 1) + kit);
			return true;
		}

		bool Decode(unsigned encoded, unsigned& first, unsigned& kit) const noexcept
		{
			if (!Valid() || encoded > Count()) return false;
			first = encoded / (kitCount + 1);
			kit = encoded % (kitCount + 1);
			return true;
		}

		bool Condition(unsigned category, const std::array<uint8_t, 64>& source, std::array<uint8_t, 64>& output) const noexcept
		{
			if (!Valid() || (category != 11 && category != 13)) return false;
			std::array<uint8_t, 64> projected{};
			for (unsigned encoded = 1; encoded <= Count(); ++encoded)
			{
				unsigned first{}, kit{};
				Decode(encoded, first, kit);
				const auto choice = category == 11 ? first : kit;
				if (choice && (source[(choice - 1) / 8] & (1u << ((choice - 1) % 8))))
					projected[(encoded - 1) / 8] |= uint8_t(1u << ((encoded - 1) % 8));
			}
			output = projected;
			return true;
		}
	};
}
