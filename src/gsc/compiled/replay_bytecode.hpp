#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gsc::detail
{
	// IW8 PC instruction widths, including the opcode. Zero marks unsupported
	// debug/native-only instructions; variable operands are handled below.
	inline constexpr std::array<std::uint8_t, 0xA9> tool_instruction_sizes
	{
		1, 2, 1, 2, 5, 5, 1, 5, 5, 1, 1, 2, 1, 1, 3, 1,
		1, 1, 1, 13, 2, 1, 2, 5, 5, 3, 1, 3, 3, 3, 3, 3,
		3, 3, 3, 4, 1, 5, 1, 1, 2, 5, 1, 1, 5, 1, 1, 1,
		2, 1, 3, 4, 2, 5, 1, 1, 5, 1, 1, 1, 5, 1, 1, 1,
		1, 1, 1, 1, 2, 2, 2, 2, 5, 5, 1, 1, 5, 1, 1, 4,
		3, 2, 2, 1, 1, 3, 3, 1, 1, 1, 1, 2, 1, 1, 1, 5,
		4, 1, 2, 2, 1, 5, 3, 1, 2, 1, 1, 2, 0, 5, 5, 0,
		5, 2, 3, 1, 2, 4, 1, 1, 1, 1, 5, 5, 1, 1, 3, 3,
		3, 3, 3, 3, 4, 1, 5, 1, 1, 4, 5, 1, 2, 5, 2, 1,
		1, 9, 4, 2, 5, 1, 4, 1, 4, 1, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 1, 2, 1, 1
	};

	inline bool PackToolOffsets(std::span<std::uint8_t> code, std::size_t& converted)
	{
		converted = 0;
		struct relocation
		{
			std::size_t operand, target;
			std::uint32_t packed;
		};
		std::vector<relocation> relocations;
		std::vector<std::size_t> instructions;
		const auto relocate = [&](const std::size_t operand)
		{
			const auto raw = std::uint32_t{ code[operand] } | (std::uint32_t{ code[operand + 1] } << 8) |
				(std::uint32_t{ code[operand + 2] } << 16);
			const auto offset = static_cast<std::int32_t>(raw) - ((raw & 0x800000) ? 0x1000000 : 0);
			const auto target = static_cast<std::int64_t>(operand) + offset;
			if (offset < -0x400000 || offset > 0x3FFFFF || target < 0 || target >= static_cast<std::int64_t>(code.size()))
				return false;
			// Replay's VM decodes signed 24-bit operands with (value << 8) >> 9.
			// gsc-tool's IW8 PC assembler emits ordinary byte offsets (offs8).
			relocations.push_back({ operand, static_cast<std::size_t>(target), static_cast<std::uint32_t>(offset * 2) & 0xFFFFFF });
			return true;
		};
		for (std::size_t position = 0; position < code.size();)
		{
			const auto opcode = code[position];
			if (opcode >= tool_instruction_sizes.size()) return false;
			auto size = std::size_t{ tool_instruction_sizes[opcode] };
			if (!size || size > code.size() - position) return false;
			instructions.push_back(position);
			if (opcode == 0xA6) // FormalParams: count followed by local indexes.
				size += code[position + 1];
			if (opcode == 0x56) // endswitch: count followed by (key, relative target).
			{
				const auto count = std::uint16_t{ code[position + 1] } | (std::uint16_t{ code[position + 2] } << 8);
				size += static_cast<std::size_t>(count) * 7;
				if (size > code.size() - position) return false;
				for (std::size_t item = position + 3; item < position + size; item += 7)
					if (!relocate(item + 4)) return false;
			}
			switch (opcode)
			{
			case 0x29: case 0x33: case 0x35: case 0x48:
			case 0x4F: case 0x6E: case 0x89: case 0x98:
				if (!relocate(position + 1)) return false;
				break;
			}
			if (size > code.size() - position) return false;
			position += size;
		}
		for (const auto& entry : relocations)
			if (!std::binary_search(instructions.begin(), instructions.end(), entry.target)) return false;
		for (const auto& entry : relocations)
			for (unsigned byte = 0; byte < 3; ++byte)
				code[entry.operand + byte] = static_cast<std::uint8_t>(entry.packed >> (byte * 8));
		converted = relocations.size();
		return true;
	}
}
