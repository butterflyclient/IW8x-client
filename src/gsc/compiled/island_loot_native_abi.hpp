#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gsc::island_loot_native_abi
{
	struct callsite
	{
		std::size_t offset;
		std::uint8_t opcode;
		std::uint8_t argument_count;
		std::uint16_t source_id;
		std::uint16_t replay_id;
	};

	inline constexpr std::array<callsite, 2> cache_calls{{
		{ 2220, 0x1F, 2, 0x3F0, 0x3EF }, // GetScriptableLootCacheContents
		{ 3961, 0x21, 4, 0x3F1, 0x3F0 }, // PickScriptableLootItem
	}};

	inline constexpr std::array<callsite, 5> pickup_calls{{
		{ 4161, 0x20, 3, 0x40F, 0x40E }, // Vehicle_GetArrayInRadius
		{ 4575, 0x1F, 2, 0x3D4, 0x3D3 }, // CalcScriptablePayloadGravityArc
		{ 15631, 0x22, 5, 0x3DA, 0x3D9 }, // SpawnScriptableWithCustomWeapon
		{ 15691, 0x21, 4, 0x3AF, 0x3AE }, // SpawnScriptable
		{ 28863, 0x1D, 0, 0x3D5, 0x3D4 }, // GetScriptableReservedRemaining
	}};

	struct module_profile
	{
		std::string_view name;
		std::size_t length;
		std::uint64_t source_fingerprint;
		std::uint64_t replay_fingerprint;
		std::span<const callsite> calls;
	};

	// These are the installed companion profiles after relative-operand
	// conversion, before the native linker relocates the compiler's copy.
	inline constexpr std::array<module_profile, 2> source_profiles{{
		{ "scripts/mp/gametypes/br_lootcache.gsc", 4832,
			0xC0665D9B59072881ULL, 0x40A219E3A9E0B5D1ULL, cache_calls },
		{ "scripts/mp/gametypes/br_pickups.gsc", 29808,
			0x71D088A53F0BAD8AULL, 0x3C73267FB4BA5EABULL, pickup_calls },
	}};

	inline const module_profile* find_profile(std::string_view module) noexcept
	{
		for (const auto& profile : source_profiles)
		{
			if (module == profile.name)
				return &profile;
		}
		return nullptr;
	}

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
		const auto* profile = find_profile(module);
		if (!profile || code.size() != profile->length || fingerprint(code) != profile->source_fingerprint)
			return false;

		// All seven native argument/result contracts were compared across
		// Source 1.64 and Replay 1.20. The shared pickup descriptor normalizes
		// optional angles and payload before either spawn call. Keep the
		// source cache-undefined fallback and total-capacity ID 0x434 intact;
		// the latter has no Replay builtin and is not the remaining count.
		// Validate every fixed-argc opcode and ID before the first mutation.
		for (const auto& site : profile->calls)
		{
			if (site.offset + 2 >= code.size() || site.opcode != 0x1D + site.argument_count ||
				code[site.offset] != site.opcode ||
				code[site.offset + 1] != (site.source_id & 0xFF) ||
				code[site.offset + 2] != (site.source_id >> 8))
				return false;
		}
		for (const auto& site : profile->calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.replay_id & 0xFF);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.replay_id >> 8);
		}
		if (fingerprint(code) == profile->replay_fingerprint)
			return true;
		for (const auto& site : profile->calls)
		{
			code[site.offset + 1] = static_cast<std::uint8_t>(site.source_id & 0xFF);
			code[site.offset + 2] = static_cast<std::uint8_t>(site.source_id >> 8);
		}
		return false;
	}
}
