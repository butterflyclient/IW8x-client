#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_trace_native_abi
{
	inline constexpr std::string_view source_module = "1805";
	inline constexpr std::size_t source_length = 3454;
	inline constexpr std::size_t call_offset = 144;
	inline constexpr std::uint64_t source_fingerprint = 0xDDACFBDEF493BF6AULL;
	inline constexpr std::uint64_t replay_fingerprint = 0x47B2E74478213597ULL;

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

	inline bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept
	{
		// Original Island engine/trace::ray_trace_ents uses Source 1.64 ID 0x389
		// (0x3065FE0). Its matching Replay callback is ID 0x388 (0x109C2C0).
		// Both consume the same six arguments and ignore the seventh; Replay
		// 0x389 instead saves replay data and supplies no script return value.
		// Admit the installed companion's complete converted profile, including
		// its relative operands, before changing the private compiler copy.
		// Stock DB assets and dispatch tables stay intact.
		if (module != source_module || code.size() != source_length || fingerprint(code) != source_fingerprint)
			return false;
		if (code[call_offset] != 0x23 || code[call_offset + 1] != 7 ||
			code[call_offset + 2] != 0x89 || code[call_offset + 3] != 0x03)
			return false;

		code[call_offset + 2] = 0x88;
		if (fingerprint(code) == replay_fingerprint)
			return true;
		code[call_offset + 2] = 0x89;
		return false;
	}
}
