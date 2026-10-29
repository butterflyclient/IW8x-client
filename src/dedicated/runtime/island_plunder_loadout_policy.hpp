#pragma once

#include <string_view>

namespace dedicated::detail::island_plunder_loadout_policy
{
	// The 1.64.5 br.gsc::main reads scr_br_allowLoadout before its call to
	// br_gametypes::init. The DMZ subclass sets this dvar to 1 only inside that
	// later init; its cached level flag therefore remains 0, selecting the
	// naked-drop parachute callback. Correct this ordering ONLY for the
	// authenticated Island/Blood Money (playlist 104) compatibility path.
	constexpr bool early_class_loadout_required(const int playlist, const std::string_view map,
		const std::string_view gametype) noexcept
	{
		return playlist == 104 && map == "mp_sm_island_1" && gametype == "br";
	}

	// Append AFTER the selected playlist's original settings but BEFORE
	// iw8_ds_config_ready/devmap. Preserve Plunder's own `scr_br_loadout_option`
	// (the DMZ subclass sets it to "nothing"), native weapon ownership, DDL
	// selected class, and parachute class/swap logic.
	inline constexpr std::string_view early_class_loadout_commands =
		"set scr_br_allowLoadout 1\n"
		"set scr_br_allowLoadoutOnlyInPreLobby 0\n";

	// BAP hosts recycle the process after the previous game unloads. The
	// replacement process must receive the original explicit Island map/mode:
	// playlist 104's authentic map is Donetsk and silently falling back to it
	// changes both the GSC startup class policy and the requested map.
	// Playlist rotation intentionally excludes explicit map/mode overrides.
	constexpr bool preserve_explicit_island_recycle(const int playlist, const std::string_view map,
		const std::string_view gametype, const bool explicit_map, const bool rotate_playlist) noexcept
	{
		// Source launch --playlist 104 --map mp_sm_island_1 without --mode
		// is supported: the playlist resolves native "br" itself. Preserve
		// that case too, always handing the recycled child both explicit tokens.
		return early_class_loadout_required(playlist, map, gametype) && explicit_map && !rotate_playlist;
	}
}
