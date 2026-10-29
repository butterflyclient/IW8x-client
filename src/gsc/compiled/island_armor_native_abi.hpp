#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_armor_native_abi
{
	// Installed original Island Source 1.64.5 numeric ScriptFile 2300 is the
	// armor_plate GSC module. Replay's legitimate 1.20 method checks the input
	// state bit in this continue-plating helper. The original 1.64 method ID
	// erroneously queries an unrelated client field in Replay.
	inline constexpr std::string_view source_module = "2300";
	inline constexpr std::size_t source_length = 1557;
	inline constexpr std::uint64_t source_fingerprint = 0xBA44D570AC52A923ULL;
	inline constexpr std::uint64_t replay_fingerprint = 0xEB3F037FE72B77A2ULL;
	inline constexpr std::size_t source_method_opcode = 1248;
	inline constexpr std::uint16_t source_method_id = 0x8746;
	inline constexpr std::uint16_t replay_method_id = 0x8743;

	std::uint64_t fingerprint(std::span<const std::uint8_t> bytes) noexcept;
	bool translate_exact_profile(std::string_view module, std::span<std::uint8_t> code,
		std::uint64_t expected_source, std::uint64_t expected_target) noexcept;
	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept;
}
