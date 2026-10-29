#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_plunder_menus_native_abi
{
	struct callsite
	{
		std::size_t offset;
		std::uint8_t opcode;
		std::uint16_t source_id;
		std::uint16_t replay_id;
	};

	// Exact installed Source 1.64.5 numeric menus.gsc compiler input.
	// Source 0x3C3 is IsTestClient: its Replay 1.20.4 equivalent is 0x3C2.
	// Replay 0x3C3 instead initializes a pathfinding table and cannot be used
	// as the human/test-client predicate. In executeclasschange this wrongly
	// writes raw menu integer indices into self.class instead of "customN".
	// The second predicate is in the same module's team-selection handler.
	inline constexpr std::string_view source_module = "1877";
	inline constexpr std::size_t source_length = 8731;
	inline constexpr std::uint64_t source_fingerprint = 0xF6BC035D3D92DB64ULL;
	inline constexpr std::uint64_t replay_fingerprint = 0x8A9AD682A5745B3EULL;
	inline constexpr std::array<callsite, 2> calls{{
		{ 833, 0x1E, 0x3C3, 0x3C2 }, // executeclasschange, export 0x48A9
		{ 8090, 0x1E, 0x3C3, 0x3C2 }, // addtoteam, export 0x0E00
	}};

	std::uint64_t fingerprint(std::span<const std::uint8_t> code) noexcept;
	bool translate_exact_profile(std::string_view module, std::span<std::uint8_t> code,
		std::uint64_t expected_source, std::uint64_t expected_target) noexcept;
	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept;
}
