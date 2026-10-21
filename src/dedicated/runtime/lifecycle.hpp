#pragma once
#include "network/playlists.h"
#include "dedicated/runtime/types.hpp"
namespace dedicated::detail
{
	extern uintptr_t g_base;

	extern std::atomic<FrameFn> g_frame;

	extern std::atomic<SetTransientModeFn> g_setTransientMode;

	extern std::atomic<bool> g_armed;

	extern std::atomic<bool> g_startRequested;

	extern std::atomic<bool> g_endRequested;

	extern bool g_installed;

	extern Stage g_stage;

	const char* StageName(Stage stage);

	extern char g_mapName[64];

	extern char g_gameType[32];

	extern int g_playlistId, g_playlistEntry, g_minPlayers;

	extern bool g_optionsValid, g_explicitMap, g_explicitMode, g_planPrepared;

	extern bool g_rotatePlaylist;

	extern int g_resumedEntryGames;

	extern playlists::DedicatedPlan g_playlistPlan;

	void Advance(Stage next, ULONGLONG timeout = 120000);

	void Fail(const char* reason);

	uint8_t ActiveMode();

	bool IsPlunderPreset();

	const char* NativeGameType();

	bool MapSettingsReady();

	bool SetStartupBool(uintptr_t rva, bool requested);

	void ConfigProcessed();

	uintptr_t DevSetup();

	void DevmapCommand();

	void ReturnToPlaylistWaiting();

	void Pump();

	void Frame();
}
