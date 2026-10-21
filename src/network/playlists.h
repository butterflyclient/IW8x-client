#pragma once
#include "engine/inline_hook.h"

#include <string_view>
#include <string>
namespace playlists
{
	hook::Status Install(uintptr_t base);
	// Called only from the main-thread client frame, after the native MP profile is ready.
	void Pump();
	bool HandleCommand(std::string_view command);
	bool Ready();
	bool HasPlaylist(int id);
	// Native category/child mapping used by Playlist.DoAction; -1 is not selectable.
	int ResolveCategoryPlaylist(int category, int child);
	// Select the native ID/ordinal/profile state without applying host map rules.
	bool SelectForMatchmaking(int id);
	// Complete client BR config synchronously before entering a dedicated session.
	// An authenticated assignment may carry a map different from the stock
	// playlist entry (notably Island BR). Synchronize that actual map on the
	// client before joining; do not infer a map from a rotating playlist ID.
	bool PrepareClientJoin(int id, std::string_view assignedMap = {}, std::string_view assignedMode = {});
	// A native lobby snapshot can reapply the stock Donetsk playlist map after
	// PrepareClientJoin ran. Preserve ONLY a server-authenticated Island BR
	// assignment while still in the lobby/preload state, never during gameplay.
	bool MaintainAssignedClientLobbyMap(int id, std::string_view assignedMap, std::string_view assignedMode);

	struct DedicatedPlan
	{
		std::string map, gametype, rules, recipe;
		int entry = 0, category = 0, maxPlayers = 0, teamCount = 0;
		unsigned weight = 1;
		bool hardcore = false, tactical = false;
	};
	// Main-thread, before devmap. Uses the same native .aggr parser and its rule
	// strings; does not require the client-only LUI/profile initialization path.
	enum class EntrySelection
	{
		Exact,
		FirstAvailable,
		WeightedAvailable
	};
	bool PrepareDedicated(int id, int entry, DedicatedPlan& plan, std::string& error, EntrySelection selection = EntrySelection::Exact);
	// Restore the native recipe and mode flags after DevCmdSetup creates the party.
	bool ApplyDedicatedSettings(const DedicatedPlan& plan, std::string& error);
	int NextDedicatedEntry(int id, int entry);
	bool SelectDedicated(int id, int category);
	void LogStatus();
}
