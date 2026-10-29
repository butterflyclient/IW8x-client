#include "island_armor_switch_native_abi.hpp"

namespace gsc::island_armor_switch_native_abi
{
	std::uint64_t fingerprint(const std::span<const std::uint8_t> bytes) noexcept
	{
		std::uint64_t state = 0xCBF29CE484222325ULL;
		for (const auto byte : bytes)
		{
			state ^= byte;
			state *= 0x100000001B3ULL;
		}
		return state;
	}

	bool translate_exact_profile(const std::string_view module, const std::span<std::uint8_t> code,
		const std::uint64_t expected_source, const std::uint64_t expected_target) noexcept
	{
		if (module != source_module || code.size() != source_length || fingerprint(code) != expected_source ||
			method_opcode + 3 > code.size() || code[method_opcode] != source_opcode ||
			code[method_opcode + 1] != static_cast<std::uint8_t>(source_id) ||
			code[method_opcode + 2] != static_cast<std::uint8_t>(source_id >> 8))
			return false;
		code[method_opcode + 1] = static_cast<std::uint8_t>(replay_id);
		if (fingerprint(code) == expected_target)
			return true;
		code[method_opcode + 1] = static_cast<std::uint8_t>(source_id);
		return false;
	}

	bool translate_private_compiler_copy(const std::string_view module,
		const std::span<std::uint8_t> code) noexcept
	{
		return translate_exact_profile(module, code, source_fingerprint, replay_fingerprint);
	}
}
