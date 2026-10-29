#include "island_armor_native_abi.hpp"

namespace gsc::island_armor_native_abi
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
			source_method_opcode + 3 > code.size() || code[source_method_opcode] != 0x7E ||
			code[source_method_opcode + 1] != static_cast<std::uint8_t>(source_method_id) ||
			code[source_method_opcode + 2] != static_cast<std::uint8_t>(source_method_id >> 8))
			return false;
		code[source_method_opcode + 1] = static_cast<std::uint8_t>(replay_method_id);
		if (fingerprint(code) == expected_target)
			return true;
		code[source_method_opcode + 1] = static_cast<std::uint8_t>(source_method_id);
		return false;
	}

	bool translate_private_compiler_copy(const std::string_view module,
		const std::span<std::uint8_t> code) noexcept
	{
		return translate_exact_profile(module, code, source_fingerprint, replay_fingerprint);
	}
}
