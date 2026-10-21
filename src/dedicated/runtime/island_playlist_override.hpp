#pragma once

#include <string_view>

namespace dedicated::detail
{
	// Island has no native entry in Replay's original playlist table. During
	// explicit, opt-in Island compatibility testing, reuse a real BR playlist's
	// mode/rules/category while selecting the converted map in the dedicated
	// session. Never modify normal playlist rotation or another map's recipe.
	constexpr bool AllowIslandPlaylistMapOverride(const std::string_view requestedMap,
		const std::string_view selectedMap, const std::string_view playlistMode,
		const std::string_view requestedMode, const bool explicitMap, const bool explicitMode,
		const bool islandCompatEnabled) noexcept
	{
		return islandCompatEnabled && explicitMap && requestedMap == "mp_sm_island_1" &&
			selectedMap != requestedMap && playlistMode == "br" &&
			(!explicitMode || requestedMode == playlistMode);
	}
}
