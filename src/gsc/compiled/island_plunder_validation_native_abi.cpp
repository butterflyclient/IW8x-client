#include "island_plunder_validation_native_abi.hpp"

namespace gsc::island_plunder_validation_native_abi
{
	const profile* find_profile(const std::string_view module) noexcept
	{
		for (const auto& row : source_profiles)
			if (row.module == module) return &row;
		return nullptr;
	}

	std::uint64_t fingerprint(const std::span<const std::uint8_t> code) noexcept
	{
		std::uint64_t hash = 0xCBF29CE484222325ULL;
		for (const auto byte : code)
		{
			hash ^= byte;
			hash *= 0x100000001B3ULL;
		}
		return hash;
	}

	bool translate_exact_profile(const profile& selected, const std::span<std::uint8_t> code,
		const std::uint64_t expected_source, const std::uint64_t expected_target) noexcept
	{
		if (code.size() != selected.source_length || fingerprint(code) != expected_source)
			return false;
		for (const auto& site : selected.calls)
		{
			if (site.offset + 2 >= code.size() || code[site.offset] != site.opcode ||
				code[site.offset + 1] != static_cast<std::uint8_t>(site.source_id) ||
				code[site.offset + 2] != static_cast<std::uint8_t>(site.source_id >> 8))
				return false;
		}
		for (const auto& site : selected.calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.replay_id);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.replay_id >> 8);
		}
		if (fingerprint(code) == expected_target)
			return true;
		for (const auto& site : selected.calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.source_id);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.source_id >> 8);
		}
		return false;
	}

	bool translate_private_compiler_copy(const std::string_view module, const std::span<std::uint8_t> code) noexcept
	{
		const auto* selected = find_profile(module);
		return selected && translate_exact_profile(*selected, code,
			selected->source_fingerprint, selected->replay_fingerprint);
	}
}
