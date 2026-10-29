#include "island_killstreak_native_abi.hpp"

namespace gsc::island_killstreak_native_abi
{
	std::uint64_t fingerprint(std::span<const std::uint8_t> bytes) noexcept
	{
		std::uint64_t state = 0xCBF29CE484222325ULL;
		for (const auto byte : bytes)
		{
			state ^= byte;
			state *= 0x100000001B3ULL;
		}
		return state;
	}

	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept
	{
		// This installed Island module writes each streak's runtime table index
		// and availability to player state. Source 1.64 moved these three methods;
		// Replay's unmodified IDs instead reset availability, set availability,
		// and call SetPowerAmmo. Preserve Replay's native slot/index validation.
		if (module != source_module || code.size() != source_length || fingerprint(code) != source_fingerprint)
		{
			return false;
		}

		for (const auto& site : calls)
		{
			if (site.offset + 2 >= code.size() || site.opcode != 0x7E + site.argument_count ||
				code[site.offset] != site.opcode ||
				code[site.offset + 1] != (site.source_id & 0xFF) ||
				code[site.offset + 2] != (site.source_id >> 8))
			{
				return false;
			}
		}
		for (const auto& site : calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.replay_id & 0xFF);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.replay_id >> 8);
		}
		if (fingerprint(code) == replay_fingerprint)
		{
			return true;
		}
		for (const auto& site : calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.source_id & 0xFF);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.source_id >> 8);
		}
		return false;
	}
}
