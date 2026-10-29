#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_killstreak_native_abi
{
	struct callsite
	{
		std::size_t offset;
		std::uint8_t opcode;
		std::uint8_t argument_count;
		std::uint16_t source_id;
		std::uint16_t replay_id;
	};

	inline constexpr std::string_view source_module = "2356";
	inline constexpr std::size_t source_length = 10906;
	inline constexpr std::uint64_t source_fingerprint = 0xD794D364148C86B9ULL;
	inline constexpr std::uint64_t replay_fingerprint = 0x9D09B63C4ED0FA5FULL;
	inline constexpr std::array<callsite, 3> calls{{
		{ 1646, 0x7E, 0, 0x872D, 0x872B }, // ResetClientKillstreakIndexes
		{ 5135, 0x80, 2, 0x872C, 0x872A }, // SetClientKillstreakIndexes
		{ 5157, 0x80, 2, 0x872E, 0x872C }, // SetClientKillstreakAvailability
	}};

	std::uint64_t fingerprint(std::span<const std::uint8_t> bytes) noexcept;
	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept;
}
