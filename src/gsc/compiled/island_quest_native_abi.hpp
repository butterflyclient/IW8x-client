#pragma once

// Original IW8 1.64.5 Island quest calls have seven native GSC function IDs
// whose numeric entries moved by one in Replay 1.20.4. This is a strictly
// verified, opt-in translation of the compiler's private bytecode COPY.
// Never edit archived ScriptFiles, import/export metadata or DB-owned storage.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gsc::island_quest_native_abi
{
	struct callsite
	{
		std::size_t opcode_offset;
		std::uint8_t opcode;
		std::uint16_t source_id;
		std::uint16_t replay_id;
	};

	inline constexpr std::array<callsite, 7> source_calls{{
		{ 1176, 0x21, 0x3E0, 0x3DF }, // get unused caches in radius
		{ 1233, 0x1E, 0x3E3, 0x3E2 }, // disable loot cache index
		{ 8530, 0x21, 0x3E0, 0x3DF },
		{ 8561, 0x22, 0x3E2, 0x3E1 }, // quest point type/origin/radius query
		{ 8611, 0x22, 0x3E2, 0x3E1 },
		{ 20566, 0x21, 0x3E0, 0x3DF },
		{ 20599, 0x1E, 0x3E3, 0x3E2 },
	}};

	inline constexpr std::size_t source_length = 20617;
	inline constexpr std::uint64_t source_fingerprint = 0xDDB72D915D1E8916ULL;
	inline constexpr std::uint64_t replay_fingerprint = 0xA2F8D6FAAE2B60A5ULL;

	inline std::uint64_t fingerprint(std::span<const std::uint8_t> bytes) noexcept
	{
		std::uint64_t state = 0xCBF29CE484222325ULL;
		for (const auto byte : bytes)
		{
			state ^= byte;
			state *= 0x100000001B3ULL;
		}
		return state;
	}

	inline bool translate_private_compiler_copy(std::span<std::uint8_t> code) noexcept
	{
		if (code.size() != source_length || fingerprint(code) != source_fingerprint)
			return false;
		// Validate the entire owner fingerprint and all seven exact opcodes and
		// original numeric IDs BEFORE the first mutation. Unknown source code
		// does not get translated merely because it shares a module name.
		for (const auto& site : source_calls)
		{
			const auto at = site.opcode_offset;
			if (at + 2 >= code.size() || code[at] != site.opcode ||
				code[at + 1] != (site.source_id & 0xFF) || code[at + 2] != (site.source_id >> 8))
				return false;
		}
		for (const auto& site : source_calls)
		{
			code[site.opcode_offset + 1] = static_cast<std::uint8_t>(site.replay_id & 0xFF);
			code[site.opcode_offset + 2] = static_cast<std::uint8_t>(site.replay_id >> 8);
		}
		return fingerprint(code) == replay_fingerprint;
	}
}
