#include "island_plunder_menus_native_abi.hpp"

namespace gsc::island_plunder_menus_native_abi
{
	std::uint64_t fingerprint(std::span<const std::uint8_t> code) noexcept
	{
		std::uint64_t state = 0xCBF29CE484222325ULL;
		for (const auto byte : code)
		{
			state ^= byte;
			state *= 0x100000001B3ULL;
		}
		return state;
	}

	bool translate_exact_profile(std::string_view module, std::span<std::uint8_t> code,
		std::uint64_t expected_source, std::uint64_t expected_target) noexcept
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
		if (fingerprint(code) == expected_target) return true;
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
