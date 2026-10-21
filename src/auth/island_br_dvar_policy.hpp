#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace dvars::island_br_policy
{
	// Exactly eight source BR script settings, each reported twice (server and
	// engine log) by stock SetDvar as a 0x100 external string in Replay 1.20.4.
	// There is no permission bypass for native typed dvars or for other maps.
	inline constexpr std::array<std::string_view, 8> source_script_settings{
		"scr_br_magcount", "scr_br_loadout_option", "scr_game_enableMinimap",
		"scr_br_radarHidePings", "scr_br_navBarHidePings", "scr_br_navBarHideEnemy",
		"scr_br_spawnProtectionTimer", "scr_br_allowkillstreaks"
	};

	inline constexpr std::uint32_t external_flag = 0x100;
	// Replay 1.20.4 Scr_SetDvar's native permission check at RVA 0x125A2AC
	// tests Dvar.flags against the caller's 0x80488 mask. The old 0x10
	// (scriptinfo/network) bit was NOT in that mask and did not permit the
	// actual native setter; it left all eight source BR GSC SetDvar calls
	// failing with flags=272. Use the minimal native accepted 0x8 bit
	// (replicated/CODINFO) for only these explicitly named BR settings.
	inline constexpr std::uint32_t script_permission_flag = 0x8;
	inline constexpr std::uint32_t replay_native_script_set_mask = 0x80488;
	inline constexpr std::uint8_t external_string_type = 9;

	constexpr bool name_allowed(std::string_view name) noexcept
	{
		for (const auto item : source_script_settings)
			if (item == name) return true;
		return false;
	}

	constexpr bool restore_allowed(const std::string_view name, const std::uint32_t flags,
		const std::uint8_t type) noexcept
	{
		// Refuse all typed dvars and external strings with any unexpected flags.
		// The engine's native script registration/type conversion is still used.
		return name_allowed(name) && flags == external_flag && type == external_string_type;
	}

	static_assert(name_allowed("scr_br_magcount"));
	static_assert(!name_allowed("scr_br_use_custom_loot"));
	static_assert(!restore_allowed("scr_br_magcount", 0x110, external_string_type));
	static_assert(!restore_allowed("scr_br_magcount", external_flag, 7));
	static_assert((external_flag & replay_native_script_set_mask) == 0);
	static_assert(((external_flag | script_permission_flag) & replay_native_script_set_mask) != 0);
	static_assert(((external_flag | 0x10) & replay_native_script_set_mask) == 0);
}
