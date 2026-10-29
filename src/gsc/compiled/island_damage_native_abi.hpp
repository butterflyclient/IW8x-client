#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_damage_native_abi
{
	struct callsite
	{
		std::size_t offset;
		std::uint8_t opcode;
		std::uint16_t source_id;
		std::uint16_t replay_id;
	};

	// Installed Source 1.64.5 BR script, after the existing native block-6
	// conversion. The three method calls belong exclusively to the 105-byte
	// weapon damage-step helper, export 0xF016 at offset 22293. Replay's genuine
	// same-body BR helper uses IDs 0x8753/0x8752/0x8751; the original 1.64 IDs
	// instead call two *void* sticker-cleanup methods and a weapon-class getter.
	inline constexpr std::string_view source_module = "scripts/mp/gametypes/br.gsc";
	inline constexpr std::size_t source_length = 51286;
	inline constexpr std::uint64_t source_fingerprint = 0xEEE716E9EF7489BBULL;
	inline constexpr std::uint64_t replay_fingerprint = 0xA14967260A0E5A9AULL;
	inline constexpr std::array<callsite, 3> calls{{
		{ 22309, 0x7E, 0x8756, 0x8753 }, // Source tier3 -> Replay tier3
		{ 22326, 0x7E, 0x8755, 0x8752 }, // Source tier2 -> Replay tier2
		{ 22343, 0x7E, 0x8754, 0x8751 }, // Source tier1 -> Replay tier1
	}};

	std::uint64_t fingerprint(std::span<const std::uint8_t> bytes) noexcept;
	bool timed_protection_active(std::int32_t active_flag, std::int32_t expiry, std::int32_t now) noexcept;
	// Production always uses the pinned source/target hashes. Supplying
	// alternative hashes is exclusively for testing the atomicity of the exact
	// callsite translator on synthetic bytecode of the same size.
	bool translate_exact_profile(std::string_view module, std::span<std::uint8_t> code,
		std::uint64_t expected_source, std::uint64_t expected_target) noexcept;
	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept;
}
