#include "island_vehicle_native_abi.hpp"

namespace gsc::island_vehicle_native_abi
{
	namespace
	{
		constexpr std::array<callsite, 1> mp_occupancy_calls{{
			{ 1781, 0x1F, 2, 0x0364, 0x0363 },
		}};
		constexpr std::array<profile, 2> profiles{{
			{ source_module, source_length, source_fingerprint, replay_fingerprint, calls, rewrites },
			{ "2441", 2687, 0xFBD9694D41C8026BULL, 0xC7CB0F183793A7F2ULL, mp_occupancy_calls, {} },
		}};
	}

	const profile* find_profile(std::string_view module) noexcept
	{
		for (const auto& selected : profiles)
		{
			if (selected.module == module)
			{
				return &selected;
			}
		}
		return nullptr;
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

	bool translate_private_compiler_copy(std::string_view module, std::span<std::uint8_t> code) noexcept
	{
		// Source 1.64's shared vehicle occupancy module calls shifted player-input,
		// weapon-flag and customization methods. Each pair is matched to Replay's
		// native registry/body and archived occupancy callers, including the five
		// Vehicle_GetInputValue calls. The MP occupancy profile also restores the
		// two-weapon IsSameWeapon comparison used to retain a selected riot shield.
		// Source 0x364 and Replay 0x363 both compare weapon object IDs for two args;
		// Replay 0x364 instead tests for a null weapon. The installed one-argument
		// null checks are already converted from Source 0x365 and must stay intact.
		const auto* selected = find_profile(module);
		if (!selected || code.size() != selected->length || fingerprint(code) != selected->source_fingerprint)
		{
			return false;
		}

		for (const auto& site : selected->calls)
		{
			const auto method = site.source_id >= 0x8000;
			const auto opcode = (method ? 0x7E : 0x1D) + site.argument_count;
			if (site.offset + 2 >= code.size() || site.argument_count > 5 ||
				method != (site.replay_id >= 0x8000) || site.opcode != opcode ||
				code[site.offset] != site.opcode ||
				code[site.offset + 1] != (site.source_id & 0xFF) ||
				code[site.offset + 2] != (site.source_id >> 8))
			{
				return false;
			}
		}
		for (const auto& site : selected->rewrites)
		{
			if (site.offset + site.source.size() > code.size())
			{
				return false;
			}
			for (std::size_t i = 0; i < site.source.size(); ++i)
			{
				if (code[site.offset + i] != site.source[i])
				{
					return false;
				}
			}
		}

		for (const auto& site : selected->calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.replay_id & 0xFF);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.replay_id >> 8);
		}
		// Replay has no selected-horn setter (Source 0x878C). Discard its receiver
		// and argument, then produce the discarded integer result: the base horn
		// remains selected. The Source-only keyboard hold-use query (0x878A) returns
		// false when its feature is disabled; Replay's archived exit logic likewise
		// exits immediately for keyboard use. Keep the gamepad/reload policy intact.
		// Replay VM table 0x132B578 proves 0x58 releases/pops, 0x5C pushes integer0,
		// and 0x16 pushes a byte integer. These three-byte replacements preserve
		// stack effects and every branch, field operand and function boundary.
		for (const auto& site : selected->rewrites)
		{
			for (std::size_t i = 0; i < site.replay.size(); ++i)
			{
				code[site.offset + i] = site.replay[i];
			}
		}
		if (fingerprint(code) == selected->replay_fingerprint)
		{
			return true;
		}
		for (const auto& site : selected->calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.source_id & 0xFF);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.source_id >> 8);
		}
		for (const auto& site : selected->rewrites)
		{
			for (std::size_t i = 0; i < site.source.size(); ++i)
			{
				code[site.offset + i] = site.source[i];
			}
		}
		return false;
	}
}
