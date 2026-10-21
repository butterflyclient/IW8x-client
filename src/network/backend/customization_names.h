#pragma once
#include <string_view>
#include <cstdint>
namespace customization_names
{
	// TU19 runtime DDL capture: Loadout.name / WeaponSetup.blueprintName=160 bits.
	// TU19 clan OSK=5; native setter uses six-byte terminated buffer.
	inline constexpr size_t LoadoutBytes = 20, ClanBytes = 5, ModBytes = 17;

	inline bool Valid(std::string_view text, size_t limit, bool allowEmpty = true)
	{
		if (text.size() > limit || (!allowEmpty && text.empty())) return false;
		for (size_t i = 0; i < text.size();)
		{
			const auto c = static_cast<unsigned char>(text[i++]);
			uint32_t code = c;
			unsigned count = 0;
			if (c >= 0xC2 && c <= 0xDF)
			{
				code = c & 31;
				count = 1;
			}
			else if (c >= 0xE0 && c <= 0xEF)
			{
				code = c & 15;
				count = 2;
			}
			else if (c >= 0xF0 && c <= 0xF4)
			{
				code = c & 7;
				count = 3;
			}
			else if (c >= 128)
				return false;
			if (count > text.size() - i) return false;
			for (unsigned k = 0; k < count; ++k)
			{
				const auto b = static_cast<unsigned char>(text[i++]);
				if ((b & 0xC0) != 0x80) return false;
				code = (code << 6) | (b & 63);
			}
			if ((count == 1 && code < 128) || (count == 2 && code < 2048) || (count == 3 && code < 65536) || code > 0x10FFFF ||
				(code >= 0xD800 && code <= 0xDFFF) || code < 32 || code == 127 || code == 0x2028 || code == 0x2029)
				return false;
		}
		return true;
	}
}
