#pragma once

#include <string_view>

namespace client_playlist_map
{
	// The native Replay 1.20.4 BR playlist maps to stock Donetsk. A *real*
	// matchmaking assignment can instead name the ported Island. Only in that
	// exact case may the client replace ui_mapname ahead of the native join.
	// The selected playlist alone is not sufficient: it can rotate maps.
	constexpr std::string_view AssignedIslandMap(std::string_view map, std::string_view mode,
		int playlist) noexcept
	{
		return map == "mp_sm_island_1" && mode == "br" && (playlist == 104 || playlist == 71)
			? std::string_view("mp_sm_island_1") : std::string_view{};
	}

	// The native lobby may apply a stock playlist snapshot after the initial
	// assignment. Reaffirm only during the native join/preload transition,
	// never while playing or for another map, mode or playlist.
	constexpr bool ShouldMaintainIslandLobbyMap(std::string_view map, std::string_view mode,
		int playlist, bool delivered, bool frontend, int matchmakingState, int lobbyState) noexcept
	{
		return delivered && frontend && matchmakingState == 7 && lobbyState >= 4 &&
			!AssignedIslandMap(map, mode, playlist).empty();
	}

	// This is the actual Replay registered name (not the unobfuscated label).
	inline constexpr const char* kReplayUiMapnameDvar = "NSQLTTMRMP";
}
