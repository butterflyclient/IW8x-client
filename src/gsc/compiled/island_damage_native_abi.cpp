#include "island_damage_native_abi.hpp"

namespace gsc::island_damage_native_abi
{
	bool timed_protection_active(const std::int32_t active_flag, const std::int32_t expiry,
		const std::int32_t now) noexcept
	{
		return active_flag != 0 && expiry > now;
	}

	std::uint64_t fingerprint(std::span<const std::uint8_t> bytes) noexcept
	{
		std::uint64_t state = 0xCBF29CE484222325ULL;
		for (const auto byte : bytes)
		{
			state ^= byte;
			state *= 0x100000001B3ULL;
		}
		return state;
	}

	bool translate_exact_profile(std::string_view module, std::span<std::uint8_t> code,
		const std::uint64_t expected_source, const std::uint64_t expected_target) noexcept
	{
		if (module != source_module || code.size() != source_length || fingerprint(code) != expected_source)
			return false;
		for (const auto& site : calls)
		{
			if (site.offset + 2 >= code.size() || code[site.offset] != site.opcode ||
				code[site.offset + 1] != static_cast<std::uint8_t>(site.source_id) ||
				code[site.offset + 2] != static_cast<std::uint8_t>(site.source_id >> 8))
				return false;
		}
		for (const auto& site : calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.replay_id);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.replay_id >> 8);
		}
		if (fingerprint(code) == expected_target)
			return true;
		for (const auto& site : calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.source_id);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.source_id >> 8);
		}
		return false;
	}

	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept
	{
		return translate_exact_profile(module, code, source_fingerprint, replay_fingerprint);
	}
}
