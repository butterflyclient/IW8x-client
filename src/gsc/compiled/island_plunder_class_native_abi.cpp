#include "island_plunder_class_native_abi.hpp"

namespace gsc::island_plunder_class_native_abi
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

	bool translate_exact_profile(const std::string_view module, std::span<std::uint8_t> code,
		const std::uint64_t expected_source, const std::uint64_t expected_target) noexcept
	{
		if (module != source_module || code.size() != source_length || fingerprint(code) != expected_source)
			return false;
		for (const auto& call : read_loops)
		{
			if (call.offset + 1 >= code.size() || code[call.offset] != call.opcode ||
				code[call.offset + 1] != call.source_count)
				return false;
		}
		for (const auto& call : untouched_cleanup_loops)
		{
			if (call.offset + 1 >= code.size() || code[call.offset] != call.opcode ||
				code[call.offset + 1] != call.source_count)
				return false;
		}
		for (const auto& call : read_loops)
			code[call.offset + 1] = call.replay_count;
		if (fingerprint(code) == expected_target)
			return true;
		for (const auto& call : read_loops)
			code[call.offset + 1] = call.source_count;
		return false;
	}

	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept
	{
		return translate_exact_profile(module, code, source_fingerprint, replay_fingerprint);
	}
}
