#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_vehicle_native_abi
{
	struct callsite
	{
		std::size_t offset;
		std::uint8_t opcode;
		std::uint8_t argument_count;
		std::uint16_t source_id;
		std::uint16_t replay_id;
	};

	struct rewrite
	{
		std::size_t offset;
		std::array<std::uint8_t, 3> source;
		std::array<std::uint8_t, 3> replay;
	};

	struct profile
	{
		std::string_view module;
		std::size_t length;
		std::uint64_t source_fingerprint;
		std::uint64_t replay_fingerprint;
		std::span<const callsite> calls;
		std::span<const rewrite> rewrites;
	};

	inline constexpr std::string_view source_module = "2273";
	inline constexpr std::size_t source_length = 20860;
	inline constexpr std::uint64_t source_fingerprint = 0x64B5A38DFD635FDFULL;
	inline constexpr std::uint64_t replay_fingerprint = 0x486C749524D304F3ULL;
	inline constexpr std::array<callsite, 19> calls{{
		{ 2419, 0x7E, 0, 0x8725, 0x8723 },
		{ 3350, 0x7E, 0, 0x875E, 0x875B },
		{ 3357, 0x7F, 1, 0x8760, 0x875D },
		{ 3392, 0x7F, 1, 0x8785, 0x8782 },
		{ 4004, 0x7E, 0, 0x8725, 0x8723 },
		{ 9689, 0x7E, 0, 0x870F, 0x870D },
		{ 9700, 0x7E, 0, 0x870F, 0x870D },
		{ 10100, 0x7E, 0, 0x8721, 0x871F },
		{ 10147, 0x7E, 0, 0x8724, 0x8722 },
		{ 10153, 0x7E, 0, 0x8727, 0x8725 },
		{ 10304, 0x7E, 0, 0x8725, 0x8723 },
		{ 10310, 0x7E, 0, 0x8726, 0x8724 },
		{ 10367, 0x7E, 0, 0x8725, 0x8723 },
		{ 10373, 0x7E, 0, 0x8726, 0x8724 },
		{ 10766, 0x7F, 1, 0x8715, 0x8713 },
		{ 10775, 0x7F, 1, 0x8715, 0x8713 },
		{ 10784, 0x7F, 1, 0x8715, 0x8713 },
		{ 10793, 0x7F, 1, 0x8715, 0x8713 },
		{ 10858, 0x7F, 1, 0x8715, 0x8713 },
	}};
	inline constexpr std::array<rewrite, 2> rewrites{{
		{ 3322, { 0x7F, 0x8C, 0x87 }, { 0x58, 0x58, 0x5C } },
		{ 10177, { 0x7E, 0x8A, 0x87 }, { 0x58, 0x16, 0x00 } },
	}};

	const profile* find_profile(std::string_view module) noexcept;
	std::uint64_t fingerprint(std::span<const std::uint8_t> bytes) noexcept;
	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept;
}
