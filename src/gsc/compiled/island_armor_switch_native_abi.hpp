#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_armor_switch_native_abi
{
	// Installed numeric2245 scripts/cp_mp/utility/inventory_utility.gsc,
	// 1.64.5 -> Replay1.20.4 conversion: iscurrentweapon's 3-argument
	// weapon comparison invokes Source ID0x364, which is *IsNullWeapon* in
	// Replay. Replay's real IsSameWeapon method ID is0x363. This prevents
	// domonitoredweaponswitch from returning1, and its caller never starts
	// the delayed armor-plate health commit. The other one-arg 0x364 callsites
	// in this same GSC module are deliberately not changed.
	inline constexpr std::string_view source_module = "2245";
	inline constexpr std::size_t source_length = 1379;
	inline constexpr std::uint64_t source_fingerprint = 0xFB5F560D3E566486ULL;
	inline constexpr std::uint64_t replay_fingerprint = 0x70BA4A39E4B08B2DULL;
	inline constexpr std::size_t method_opcode = 1312;
	inline constexpr std::uint8_t source_opcode = 0x20; // 3-argument native function
	inline constexpr std::uint16_t source_id = 0x364;
	inline constexpr std::uint16_t replay_id = 0x363;

	std::uint64_t fingerprint(std::span<const std::uint8_t> bytes) noexcept;
	bool translate_exact_profile(std::string_view module, std::span<std::uint8_t> code,
		std::uint64_t expected_source, std::uint64_t expected_target) noexcept;
	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept;
}
