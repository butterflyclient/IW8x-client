#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_plunder_validation_native_abi
{
	struct callsite
	{
		std::size_t offset;
		std::uint8_t opcode;
		std::uint16_t source_id;
		std::uint16_t replay_id;
	};

	struct profile
	{
		std::string_view module;
		std::size_t source_length;
		std::uint64_t source_fingerprint;
		std::uint64_t replay_fingerprint;
		std::span<const callsite> calls;
	};

	// The imported Source 1.64 restriction functions are one slot higher than
	// Replay 1.20.4. Source validation::validateloadout calls 0x401 with a
	// weapon basename, but Replay 0x401 interprets the string as an attachment
	// name, then raises MENU/MATCHRULES_DATAREADERROR when the DDL key is absent.
	// The stock Replay validation ScriptFile uses 0x400/0x401/0x402/0x403/0x404
	// for weapon, attachment, perk, equipment and killstreak restrictions. The
	// Source original is compiled as numeric module 1922, not a named .gsc.
	inline constexpr std::array<callsite, 17> validation_calls{{
		{ 343, 0x1E, 0x401, 0x400 }, // primary weapon
		{ 705, 0x1F, 0x402, 0x401 }, // primary attachment, source supplied weapon too
		{ 815, 0x1E, 0x403, 0x402 }, // perk
		{ 869, 0x1E, 0x401, 0x400 }, // secondary weapon
		{ 1231, 0x1F, 0x402, 0x401 }, // secondary attachment
		{ 1397, 0x1E, 0x403, 0x402 }, // perk
		{ 1720, 0x1E, 0x403, 0x402 },
		{ 1792, 0x1E, 0x403, 0x402 },
		{ 1948, 0x1E, 0x405, 0x404 }, // killstreak/specialist
		{ 1959, 0x1E, 0x405, 0x404 },
		{ 1970, 0x1E, 0x405, 0x404 },
		{ 2036, 0x1E, 0x404, 0x403 }, // lethal equipment
		{ 2140, 0x1E, 0x404, 0x403 }, // tactical equipment
		{ 2239, 0x1E, 0x405, 0x404 },
		{ 2549, 0x1E, 0x405, 0x404 },
		{ 2577, 0x1E, 0x405, 0x404 },
		{ 2606, 0x1E, 0x405, 0x404 }
	}};

	// The imported Source perks module also uses 0x403 for a perk check.
	// Replay 0x403 is an equipment check. Preserve legitimate Plunder perks.
	inline constexpr std::array<callsite, 2> perk_calls{{
		{ 7241, 0x1E, 0x403, 0x402 },
		{ 7476, 0x1E, 0x403, 0x402 }
	}};

	inline constexpr std::array<profile, 2> source_profiles{{
		{ "1922", 6672, 0xE45A0F3778C4B50EULL, 0xB759B72BB74C8E55ULL, validation_calls },
		{ "2378", 10175, 0x3D586989F28BE8D4ULL, 0xF7A0D95510ECB882ULL, perk_calls }
	}};

	const profile* find_profile(std::string_view module) noexcept;
	std::uint64_t fingerprint(std::span<const std::uint8_t> code) noexcept;
	bool translate_exact_profile(const profile& selected, std::span<std::uint8_t> code,
		std::uint64_t expected_source, std::uint64_t expected_target) noexcept;
	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept;
}
