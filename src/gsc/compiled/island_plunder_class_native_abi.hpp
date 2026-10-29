#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_plunder_class_native_abi
{
	struct loop_bound
	{
		std::size_t offset;
		std::uint8_t opcode;
		std::uint8_t source_count;
		std::uint8_t replay_count;
	};

	// Exact installed 1.64.5 Source numeric class.gsc (ScriptFile 1827).
	// Source class::loadout_updateclasscustom export 0x7DD3 offset 6015
	// reads attachmentSetup[0..9], but Replay's rankedloadouts DDL exposes
	// only [0..4]. Preserve ten native classstruct slots/cleanup loops: only
	// the TWO DDL read-loop bounds are changed. Unknown ScriptFile revisions
	// are rejected before the private compiler buffer can be modified.
	inline constexpr std::string_view source_module = "1827";
	inline constexpr std::size_t source_length = 32325;
	inline constexpr std::uint64_t source_fingerprint = 0xCEF89C68ABD6B6ADULL;
	inline constexpr std::uint64_t replay_fingerprint = 0xE70752727D406927ULL;
	inline constexpr std::array<loop_bound, 2> read_loops{{
		{ 6130, 0x16, 10, 5 }, // primary weapon attachments
		{ 6307, 0x16, 10, 5 }, // secondary weapon attachments
	}};
	inline constexpr std::array<loop_bound, 2> untouched_cleanup_loops{{
		{ 6776, 0x16, 10, 10 },
		{ 6851, 0x16, 10, 10 },
	}};

	std::uint64_t fingerprint(std::span<const std::uint8_t> code) noexcept;
	bool translate_exact_profile(std::string_view module, std::span<std::uint8_t> code,
		std::uint64_t expected_source, std::uint64_t expected_target) noexcept;
	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept;
}
