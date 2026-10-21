#include "asset_mark_guard.h"
#include "auto_server.h"
#include "dedicated.h"

#include "window.h"

#include "bap.h"
#include "bot_command_cache.h"
#include "bot_goal_wait.h"
#include "bot_profiler.h"
#include "bot_movement_batch.h"
#include "bot_nav_gpu.h"
#include "commands.h"
#include "console.h"

#include "game.h"

#include "join.h"
#include "noclip.h"
#include "online_services.h"
#include "playlists.h"

#include "safemem.h"

#include "thirdparty/minhook/include/MinHook.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <string_view>

#include <intrin.h>

#include "dedicated/runtime/internal.hpp"
#include "dedicated/runtime/island_playlist_override.hpp"
#include "network/backend/native_matchmaking.h"
#include "dedicated/assets/infil_transients.h"
#include "auth/profile/local_progression.h"
#include "auth/dvar_patches.h"
namespace dedicated::detail
{
	uintptr_t g_base = 0;

	std::atomic<FrameFn> g_frame{ nullptr };

	std::atomic<SetTransientModeFn> g_setTransientMode{ nullptr };

	std::atomic<bool> g_armed{ false };

	std::atomic<bool> g_startRequested{ false };

	std::atomic<bool> g_endRequested{ false };

	static bool g_ending = false;

	// main-thread only; console merely sets g_endRequested
	static bool g_recycleAfterStop = false;

	bool g_installed = false;

	static bool g_configProcessed = false;

	static bool g_commandEntered = false;

	Stage g_stage = Stage::SelectMP;

	// Only accessed by the main thread / native command callbacks.
	static ULONGLONG g_deadline = 0;

	static ULONGLONG g_restartAt = 0;

	static ULONGLONG g_lastConfigQueueWarning = 0;

	static unsigned g_restartCount = 0;

	const char* StageName(Stage stage)
	{
		switch (stage)
		{
		case Stage::SelectMP:
			return "selecting-mode";
		case Stage::WaitNone:
			return "switching-mode";
		case Stage::WaitMP:
			return "starting-mp";
		case Stage::QueueConfig:
			return "configuring";
		case Stage::WaitConfig:
			return "configuring";
		case Stage::WaitServer:
			return "loading";
		case Stage::Running:
			return "running";
		case Stage::RestartDelay:
			return "restarting";
		case Stage::Failed:
			return "failed";
		case Stage::Idle:
			return "waiting-for-playlist";
		case Stage::Stopping:
			return "stopping";
		default:
			return "unknown";
		}
	}

	char g_mapName[64] = "mp_shipment";

	char g_gameType[32] = "war";

	int g_playlistId = -1, g_playlistEntry = 0, g_minPlayers = -1;

	bool g_optionsValid = true, g_explicitMap = false, g_explicitMode = false, g_planPrepared = false;

	bool g_rotatePlaylist = false;

	static unsigned g_entryGamesRemaining = 0;

	int g_resumedEntryGames = 0;

	static int g_autoRequestedPlaylist = -1;

	playlists::DedicatedPlan g_playlistPlan;

	void Advance(Stage next, ULONGLONG timeout)
	{
		g_stage = next;
		g_deadline = GetTickCount64() + timeout;
		if (next == Stage::QueueConfig) g_lastConfigQueueWarning = 0;
	}

	void Fail(const char* reason)
	{
		LOG_ERR("Dedicated", "startup stopped: %s", reason);
		g_stage = Stage::Failed;
	}

	uint8_t ActiveMode()
	{
		return reinterpret_cast<GetModeFn>(g_base + game::kComGameModeGetActiveGameModeRVA)();
	}

	bool IsPlunderPreset()
	{
		return _stricmp(g_gameType, "plunder") == 0;
	}

	const char* NativeGameType()
	{
		// Plunder is a BR preset, not a distinct stock MP gametype token.  Keep the
		// operator-facing selection as "plunder", but feed the proven BR token into
		// Replay's normal devmap/settings path.
		return IsPlunderPreset() ? "br" : g_gameType;
	}

	bool MapSettingsReady()
	{
		const auto get = reinterpret_cast<game::Dvar_GetString_t>(g_base + game::kDvarGetStringRVA);
		char map[64]{}, mode[32]{};
		safemem::ReadString(get("NSQLTTMRMP"), map, sizeof(map));
		safemem::ReadString(get("MOLPOSLOMO"), mode, sizeof(mode));
		return std::strcmp(map, g_mapName) == 0 && std::strcmp(mode, NativeGameType()) == 0;
	}

	bool SetStartupBool(uintptr_t rva, bool requested)
	{
		uintptr_t dvar = 0;
		uint8_t type = 0xFF, value = 0;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &dvar, sizeof(dvar)) || !dvar ||
			!safemem::ReadBytes(reinterpret_cast<void*>(dvar + game::kDvarTypeOffset), &type, 1) || type != 0)
			return false;
		reinterpret_cast<game::Dvar_SetBool_Internal_t>(g_base + game::kDvarSetBoolInternalRVA)(reinterpret_cast<void*>(dvar), requested);
		return safemem::ReadBytes(reinterpret_cast<void*>(dvar + game::kDvarCurrentOffset), &value, 1) && (value != 0) == requested;
	}

	void ConfigProcessed()
	{
		if (g_stage == Stage::WaitConfig)
		{
			g_configProcessed = true;
			LOG_INFO("Dedicated", "native command buffer processed cfg/settings batch; checking dvar values");
		}
	}

	uintptr_t DevSetup()
	{
		const auto result = g_devSetup.load(std::memory_order_acquire)();
		if (g_playlistDevmapActive)
		{
			// Replay DevCmdSetup clears usingRecipe while creating PrivateParty.
			// Restore the selected settings here, before Devmap reads the mode and
			// hardcore flag into its server-start request. See native IDA evidence.
			std::string error;
			if (!playlists::ApplyDedicatedSettings(g_playlistPlan, error)) Fail(error.c_str());
		}
		return result;
	}

	void DevmapCommand()
	{
		if (!g_armed.load(std::memory_order_acquire) || g_stage != Stage::WaitServer || g_commandEntered) return;
		void* app = reinterpret_cast<GetServerFn>(g_base + game::kGetActiveServerApplicationRVA)();
		if (ActiveMode() != game::kGameModeMP || !app || !MapSettingsReady())
		{
			Fail("devmap dispatch lost MP application or map settings");
			return;
		}
		if (g_playlistId >= 0)
		{
			std::string error;
			if (!playlists::ApplyDedicatedSettings(g_playlistPlan, error))
			{
				Fail(error.c_str());
				return;
			}
		}
#ifdef IW8X_ISLAND_PORT_PROBE
		if (!_stricmp(g_mapName, "mp_sm_island_1") && !_stricmp(g_gameType, "br"))
		{
			const auto count = dvars::RestoreIslandBRScriptPermissions();
			if (count)
				LOG_INFO("Dedicated", "Island source 1.64.5 GSC script-write permission: %u exact Replay external dvars restored", count);
		}
#endif
		g_commandEntered = true;
		LOG_INFO("Dedicated", "starting %s / %s: entering stock SvGameModeAppMP::DevmapCmd (RVA 0x136BBB0)", g_mapName, g_gameType);
		// Native command dispatch has populated SV_Cmd argv. DevmapCmd constructs
		// its own settings and calls StartServer/WaitServer; no synthetic settings
		// or client allocation here. Its stock DevCmdSetup/PreSpawn still use CL.
		g_playlistDevmapActive = g_playlistId >= 0;
		reinterpret_cast<DevmapFn>(g_base + game::kSVDevmapCmdRVA)(app);
		g_playlistDevmapActive = false;
		LOG_INFO("Dedicated", "stock devmap handler returned; waiting for sv_running");
	}

	void ReturnToPlaylistWaiting()
	{
		g_playlistId = -1;
		g_autoRequestedPlaylist = -1;
		g_playlistEntry = 0;
		g_playlistPlan = {};
		g_planPrepared = false;
		g_configProcessed = false;
		g_commandEntered = false;
		g_startRequested = false;
		g_restartAt = 0;
		g_hasPendingBotClasses = false;
		g_pendingBotAdds = 0;
		g_botClassCommandQueued = false;
		g_ending = false;
		online_services::FinishDedicatedReset();
		Advance(Stage::Idle);
		console::SetDedicatedTitle("waiting for playlist");
		OP_INFO("Waiting for a playlist request");
	}

	void Pump()
	{
		if (!g_armed.load(std::memory_order_acquire)) return;
		if (g_endRequested.exchange(false, std::memory_order_acq_rel) && !g_ending)
		{
			if (g_stage == Stage::Failed)
			{
				OP_WARN("Host initialization failed; restart the dedicated process before accepting playlists");
				return;
			}
			g_ending = true;
			g_recycleAfterStop = g_stage != Stage::Idle || g_startRequested.load();
			online_services::BeginDedicatedReset();
			OP_INFO("Ending server | new joins suspended");
		}
		// Do not interrupt a cfg/devmap in flight. Finish native startup before
		// shutting it down, while the listener refuses new joins throughout.
		if (g_ending && (g_stage == Stage::Idle || g_stage == Stage::Running || g_stage == Stage::RestartDelay))
		{
			if (ActiveMode() != game::kGameModeMP || !reinterpret_cast<GetServerFn>(g_base + game::kGetActiveServerApplicationRVA)())
				return;
			Advance(Stage::Stopping, 30000);
			// Replay's main-thread request runs the stock synchronization, final
			// client messages and asset teardown. Calling SV's cleanup virtual
			// directly would omit that lifecycle (see RESEARCH.md).
			if (ServerRunningForTrace()) reinterpret_cast<void (*)()>(g_base + kRequestServerShutdownRVA)();
			g_restartAt = GetTickCount64() + 2000;
		}
		if (g_stage == Stage::Stopping)
		{
			if (GetTickCount64() >= g_deadline)
			{
				Fail("end_server timed out; matchmaking remains unavailable");
				g_ending = false;
				return;
			}
			if (GetTickCount64() >= g_restartAt && !ServerRunningForTrace() && ActiveMode() == game::kGameModeMP &&
				reinterpret_cast<GetServerFn>(g_base + game::kGetActiveServerApplicationRVA)())
			{
				if (g_recycleAfterStop)
				{
					// BAP's auxiliary DB entries currently outlive freed zone names.
					// A clean dedicated process avoids reusing those stale assets.
					OP_INFO("Server ended | restarting dedicated host for a fresh playlist request");
					online_services::Stop();
					if (!auto_server::RestartDedicated())
					{
						Fail("could not restart dedicated host; matchmaking remains unavailable");
						g_ending = false;
					}
				}
				else
					ReturnToPlaylistWaiting();
			}
			return;
		}
		if (g_stage == Stage::Failed) return;
		if (g_stage == Stage::Idle)
		{
			if (g_autoRequestedPlaylist < 0 && !online_services::TakeDedicatedRequest(g_autoRequestedPlaylist)) return;
			std::string error;
			if (!playlists::PrepareDedicated(
					g_autoRequestedPlaylist, 0, g_playlistPlan, error, playlists::EntrySelection::WeightedAvailable))
			{
				if (!error.empty())
				{
					OP_WARN("Playlist %d unavailable: %s", g_autoRequestedPlaylist, error.c_str());
					online_services::RejectDedicatedPlaylist(g_autoRequestedPlaylist);
					g_autoRequestedPlaylist = -1;
				}
				return;
			}
			g_playlistId = g_autoRequestedPlaylist;
			g_autoRequestedPlaylist = -1;
			g_playlistEntry = g_playlistPlan.entry;
			strcpy_s(g_mapName, g_playlistPlan.map.c_str());
			strcpy_s(g_gameType, g_playlistPlan.gametype.c_str());
			g_planPrepared = true;
			g_configProcessed = false;
			g_commandEntered = false;
			console::SetDedicatedTitle(g_mapName);
			Advance(Stage::QueueConfig);
			OP_INFO("Match requested | playlist=%d map=%s mode=%s", g_playlistId, g_mapName, g_gameType);
		}
		if (g_stage == Stage::Running)
		{
			// Replay normally latches sendGameStates from a local client message.
			// Headless dedicated mode deliberately suppresses that local client path,
			// so preserve the server-side invariant for remote joins and map restarts.
			if (ServerRunningForTrace())
			{

				const auto now = GetTickCount64();

				static ULONGLONG nextSendStateCheck = 0;
				if (now >= nextSendStateCheck)
				{
					nextSendStateCheck = now + 250;
					EnsureSendGameStates();
				}
				if ((g_hasPendingBotClasses.load(std::memory_order_acquire) || g_pendingBotAdds.load(std::memory_order_acquire)) &&
					now >= g_nextBotClassCheck && !g_botClassCommandQueued.exchange(true, std::memory_order_acq_rel))
				{
					g_nextBotClassCheck = now + 100;
					if (!commands::Queue(g_base, "iw8_ds_bot_setup\n")) g_botClassCommandQueued.store(false, std::memory_order_release);
				}
				return;
			}

			// The stock match-end path tears the gameplay server down. In our headless
			// flow there is no frontend/local-client transition to launch the next map,
			// leaving the private party present but no longer hosting a joinable game.
			// Give stock teardown a short grace window, then run the selected map back
			// through the same cfg -> devmap path used for initial startup.
			g_restartAt = GetTickCount64() + 2000;
			g_botClassCommandQueued.store(false, std::memory_order_release);
			g_pendingBotAdds = 0;
			Advance(Stage::RestartDelay, 30000);
			OP_INFO("Match ended | restarting %s (%s)", g_mapName, g_gameType);
			LOG_INFO("Dedicated", "sv_running transitioned 1->0; scheduling stock map restart");
			return;
		}

		if (g_stage == Stage::RestartDelay)
		{
			if (GetTickCount64() >= g_deadline)
			{
				Fail("match ended but the MP application did not become ready for restart");
				return;
			}

			// If stock brought the server back itself during the grace period, do not
			// compete with it. Just restore the headless sendGameStates invariant.
			if (ServerRunningForTrace())
			{
				EnsureSendGameStates();
				Advance(Stage::Running);
				g_restartAt = 0;
				LOG_INFO("Dedicated", "server resumed during restart grace period; automatic devmap cancelled");
				return;
			}

			if (GetTickCount64() < g_restartAt) return;

			if (ActiveMode() != game::kGameModeMP || !reinterpret_cast<GetServerFn>(g_base + game::kGetActiveServerApplicationRVA)())
				return;

			if (AutoPlaylist())
			{
				if (bap::Enabled())
				{
					// A natural match end frees zone-backed names just like
					// end_server. Returning to Idle in the same BAP process leaves
					// those names in auxiliary entries for the next DB mark pass.
					online_services::BeginDedicatedReset();
					online_services::Stop();
					OP_INFO("Match ended | recycling dedicated process to playlist waiting");
					if (!auto_server::RestartDedicated()) Fail("could not recycle automatic host after match end");
				}
				else
					ReturnToPlaylistWaiting();
				return;
			}
			g_configProcessed = false;
			g_commandEntered = false;
			if (g_rotatePlaylist)
			{
				if (g_entryGamesRemaining > 1)
					--g_entryGamesRemaining;
				else
				{
					const int next = playlists::NextDedicatedEntry(g_playlistId, g_playlistEntry);
					if (next < 0)
					{
						Fail("playlist rotation has no weighted map entries");
						return;
					}
					g_playlistEntry = next;
					g_planPrepared = false;
					OP_INFO("Playlist rotation | id=%d entry=%d", g_playlistId, g_playlistEntry);
				}
			}
			if (bap::Enabled())
			{
				std::wstring startup;
				if (g_playlistId >= 0)
				{
					startup = L"--playlist " + std::to_wstring(g_playlistId) + L" --playlist-entry " + std::to_wstring(g_playlistEntry);
					if (g_rotatePlaylist)
					{
						startup += L" --playlist-rotate";
						// A newly selected entry starts its full weight in the new
						// process; a repeated entry keeps its remaining games.
						if (g_planPrepared && g_entryGamesRemaining)
							startup += L" --playlist-games-remaining " + std::to_wstring(g_entryGamesRemaining);
					}
					if (g_minPlayers > 0) startup += L" --min-players " + std::to_wstring(g_minPlayers);
				}
				else
				{
					// Startup map/mode tokens were already validated by ReadOptions.
					const std::string map(g_mapName), mode(g_gameType);
					startup = L"--map " + std::wstring(map.begin(), map.end()) + L" --mode " + std::wstring(mode.begin(), mode.end());
				}
				online_services::BeginDedicatedReset();
				online_services::Stop();
				OP_INFO("Match ended | recycling dedicated process for the next map");
				if (!auto_server::RestartDedicated(startup)) Fail("could not recycle playlist host after match end");
				return;
			}
			g_restartAt = 0;
			++g_restartCount;
			Advance(Stage::QueueConfig);
			LOG_INFO("Dedicated", "restart #%u: MP application ready; re-entering cfg/devmap startup path", g_restartCount);
			return;
		}
		if (!g_deadline) g_deadline = GetTickCount64() + 120000;
		if (GetTickCount64() >= g_deadline)
		{
			LOG_ERR("Dedicated", "%s timed out; devmap_entered=%d (see preceding engine errors)", StageName(g_stage), g_commandEntered);
			Fail("native initialization or server startup did not complete; no automatic retry");
			return;
		}
		uint8_t initialized = 0;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + game::kComFullyInitializedRVA), &initialized, 1) || !initialized) return;

		if (g_stage == Stage::SelectMP)
		{
			// Replay rejects a direct non-NONE -> different non-NONE transition.
			const auto setMode = reinterpret_cast<ModeFn>(g_base + game::kComGameModeSetDesiredGameModeRVA);
			if (ActiveMode() != 0 && ActiveMode() != game::kGameModeMP)
			{
				setMode(0, true);
				Advance(Stage::WaitNone);
				LOG_INFO("Dedicated", "requested NONE before switching the active mode to MP");
			}
			else
			{
				setMode(game::kGameModeMP, true); // Same bool used by Replay's +mpMode startup parser.
				Advance(Stage::WaitMP);
				LOG_INFO("Dedicated", "desired game mode -> MP (2), wasGameRunning=true");
			}
			return; // Stock Com_Frame processes the requested transition.
		}
		if (g_stage == Stage::WaitNone)
		{
			if (ActiveMode() == 0)
			{
				reinterpret_cast<ModeFn>(g_base + game::kComGameModeSetDesiredGameModeRVA)(game::kGameModeMP, true);
				Advance(Stage::WaitMP);
				LOG_INFO("Dedicated", "desired game mode -> MP (2), wasGameRunning=true");
			}
			return;
		}
		if (g_stage == Stage::WaitMP)
		{
			if (ActiveMode() != game::kGameModeMP || !reinterpret_cast<GetServerFn>(g_base + game::kGetActiveServerApplicationRVA)())
				return;
			// The numbered-tile collision processor belongs to the MP transient
			// subsystem.  Installing it at the splash boundary is too early: the
			// core DB/lifecycle routes can be armed there, but this processor should
			// only be detoured once Replay has created the MP application and before
			// we queue cfg/devmap (the first point at which Donetsk can request it).
			if (bap::Enabled())
			{
				const auto bapCollision = bap::InstallCollision(g_base);
				if (bapCollision != hook::Status::Installed)
				{
					Fail("could not install the BAP MP collision processor after MP initialization");
					return;
				}
			}
			if (!RegisterCommands())
			{
				Fail("could not register native server commands");
				return;
			}
			LOG_INFO("Dedicated", "MP application ready");
			if (AutoPlaylist())
			{
				Advance(Stage::Idle);
				OP_INFO("Waiting for a playlist request");
				return;
			}
			Advance(Stage::QueueConfig);
		}
		if (g_stage == Stage::QueueConfig)
		{
			if (g_playlistId >= 0 && !g_planPrepared)
			{
				std::string error;
				if (!playlists::PrepareDedicated(g_playlistId, g_playlistEntry, g_playlistPlan, error,
						g_rotatePlaylist ? playlists::EntrySelection::FirstAvailable : playlists::EntrySelection::Exact))
				{
					if (!error.empty()) Fail(error.c_str());
					return;
				}
				#ifdef IW8X_ISLAND_PORT_PROBE
				char islandCompat[2]{};
				const bool islandCompatEnabled = GetEnvironmentVariableA("IW8X_ISLAND_NUMERIC_CANONICAL", islandCompat,
					sizeof(islandCompat)) == 1 && islandCompat[0] == '1';
				if (AllowIslandPlaylistMapOverride(g_mapName, g_playlistPlan.map, g_playlistPlan.gametype,
					NativeGameType(), g_explicitMap, g_explicitMode, islandCompatEnabled))
				{
					LOG_INFO("Dedicated", "Island BR compatibility selected: playlist=%d entry=%d native_map=%s override_map=%s",
						g_playlistId, g_playlistPlan.entry, g_playlistPlan.map.c_str(), g_mapName);
					g_playlistPlan.map = g_mapName;
				}
				#endif
				if ((g_explicitMap && g_playlistPlan.map != g_mapName) || (g_explicitMode && g_playlistPlan.gametype != NativeGameType()))
				{
					Fail("explicit --map/--mode conflicts with the selected playlist entry");
					return;
				}
				strcpy_s(g_mapName, g_playlistPlan.map.c_str());
				g_playlistEntry = g_playlistPlan.entry;
				strcpy_s(g_gameType, g_playlistPlan.gametype.c_str());
				console::SetDedicatedTitle(g_mapName);
				g_planPrepared = true;
				g_entryGamesRemaining = g_resumedEntryGames > 0 ? (std::min)(unsigned(g_resumedEntryGames), unsigned(g_playlistPlan.weight))
					: g_playlistPlan.weight;
				g_resumedEntryGames = 0;
			}
			char batch[1024]{};
			if (IsPlunderPreset())
			{
				// User-facing Plunder preset.  The requested BR/DMZ configuration is
				// queued verbatim through native Cbuf before devmap, with only the
				// existing dedicated startup map/LAN barrier appended afterwards.
				_snprintf_s(batch, sizeof(batch), _TRUNCATE,
					"exec default_mp.cfg\n"
					"exec br_core.cfg\n"
					"MLQNQTRRTK 1\n"
					"QTQRQPLNK 1\n"
					"scr_br_gametype dmz\n"
					"scr_br_timelimit 900\n"
					"set LLNLRTQRPO 150\n"
					"set scr_br_loadout_option \"standardammo\"\n"
					"set scr_br_allowLoadout 1\n"
					"set scr_br_use_set_loadouts 1\n"
					"set scr_br_allowLoadoutOnlyInPreLobby 0\n"
					"set NSQLTTMRMP %s\n"
					"set MOLPOSLOMO br\n"
					"set LPSPMQSNPQ 0\n"
					"set LLPNKKORPT 1\n"
					"set LPNMMPKRL 1\n"
					"iw8_ds_config_ready\n",
					g_mapName);
			}
			else if (_stricmp(g_gameType, "br") == 0)
			{
				// Warzone/BR startup needs its core cfg and BR-specific dvars established
				// before the stock devmap path begins loading the level/scripts.
				_snprintf_s(batch, sizeof(batch), _TRUNCATE,
					"exec default_mp.cfg\n"
					"exec br_core.cfg\n"
					"MLQNQTRRTK 1\n"
					"QTQRQPLNK 1\n"
					"scr_br_gametype br\n"
					"NSQLTTMRMP %s\n"
					"set LLNLRTQRPO 120\n"
					"set MOLPOSLOMO br\n"
					"set LPSPMQSNPQ 0\n"
					"set LLPNKKORPT 1\n"
					"set LPNMMPKRL 1\n"
					"iw8_ds_config_ready\n",
					g_mapName);
			}
			else
			{
				_snprintf_s(batch, sizeof(batch), _TRUNCATE,
					"exec default_mp.cfg\n"
					"set NSQLTTMRMP %s\n"
					"set MOLPOSLOMO %s\n"
					"set LPSPMQSNPQ 0\n"
					"set LLPNKKORPT 1\n"
					"set LPNMMPKRL 1\n"
					"iw8_ds_config_ready\n",
					g_mapName, g_gameType);
			}
			std::string config = batch;
			if (g_playlistId >= 0)
			{
				config = "exec default_mp.cfg\n";
				if (g_playlistPlan.gametype == "br") config += "exec br_core.cfg\nMLQNQTRRTK 1\nQTQRQPLNK 1\nscr_br_gametype br\n";
				config += g_playlistPlan.rules;
				if (g_minPlayers > 0)
				{
					// Explicit small-LAN override, after genuine playlist defaults.
					const auto minimum = std::to_string(g_minPlayers);
					config += "set br_minplayers " + minimum + "\nset party_minplayers " + minimum + "\nset live_lobby_minplayers_start " +
						minimum + "\n";
				}
				config += "set NSQLTTMRMP " + g_playlistPlan.map + "\nset MOLPOSLOMO " + g_playlistPlan.gametype +
					"\nset LPSPMQSNPQ 0\nset LLPNKKORPT 1\nset LPNMMPKRL 1\n"
					// Replay's GSC chooses actual weapons using level.rankedmatch.
					// The native stats-group hook only validates incoming deltas;
					// force_ranking makes the scripts read the same rankedloadouts
					// bank without changing the dedicated private-party transport.
					"set OSPNSPSKL 1\niw8_ds_config_ready\n";
			}
			commands::QueueStatus queueStatus{};
			if (commands::Queue(g_base, config.c_str(), &queueStatus))
			{
				Advance(Stage::WaitConfig);
				if (g_playlistId >= 0)
					LOG_INFO("Dedicated", "queued native playlist %d rules before devmap: bytes=%zu category=%d entry=%d", g_playlistId,
						config.size(), g_playlistPlan.category, g_playlistEntry);
				else if (IsPlunderPreset())
					LOG_INFO("Dedicated", "queued Plunder BR/DMZ preset before map load: map=%s native_gametype=br", g_mapName);
				else if (_stricmp(g_gameType, "br") == 0)
					LOG_INFO("Dedicated", "queued BR core cfg/dvars before map load: map=%s gametype=br", g_mapName);
				else
					LOG_INFO("Dedicated", "queued default_mp.cfg, map=%s gametype=%s and LAN settings through native Cbuf", g_mapName,
						g_gameType);
			}
			else
			{
				const ULONGLONG now = GetTickCount64();
				if (!g_lastConfigQueueWarning || now - g_lastConfigQueueWarning >= 5000)
				{
					const long long remaining = queueStatus.descriptorRead
						? static_cast<long long>(queueStatus.capacity) - static_cast<long long>(queueStatus.size)
						: -1;
					LOG_WARN("Dedicated",
						"cfg queue unavailable: reason=%s cbuf_rva=0x%llX read=%d data=%p capacity=%d size=%d len=%zu remaining=%lld data_write=%d size_write=%d",
						commands::QueueFailureName(queueStatus.failure), static_cast<unsigned long long>(game::kConsoleBufferRVA),
						queueStatus.descriptorRead ? 1 : 0, static_cast<void*>(queueStatus.data), queueStatus.capacity, queueStatus.size,
						queueStatus.length, remaining, queueStatus.dataWrite ? 1 : 0, queueStatus.sizeWrite ? 1 : 0);
					g_lastConfigQueueWarning = now;
				}
			}
			return;
		}
		if (g_stage == Stage::WaitConfig)
		{
			if (!g_configProcessed) return;
			if (!MapSettingsReady() || !SetStartupBool(game::kSystemLinkDvarRVA, false) ||
				!SetStartupBool(game::kSystemLinkHostDvarRVA, true) || !SetStartupBool(game::kLanLobbyEnabledDvarRVA, true))
			{
				Fail("cfg batch completed but map/gametype/LAN dvars did not verify");
				return;
			}
			if (g_playlistId >= 0 && !playlists::SelectDedicated(g_playlistId, g_playlistPlan.category))
			{
				Fail("could not select native dedicated playlist after cfg");
				return;
			}
			char devmapCommand[96]{};
			_snprintf_s(devmapCommand, sizeof(devmapCommand), _TRUNCATE, "devmap %s\n", g_mapName);
			if (commands::Queue(g_base, devmapCommand))
			{
				g_startRequested.store(true, std::memory_order_release);
				Advance(Stage::WaitServer, 300000);
				LOG_INFO("Dedicated", "stock server startup requested: devmap %s (%s), systemlink=0; direct party transport ready",
					g_mapName, g_gameType);
				OP_INFO("Loading %s (%s)", g_mapName, g_gameType);
			}
			return;
		}
		if (g_stage == Stage::WaitServer && g_commandEntered && reinterpret_cast<bool (*)()>(g_base + game::kIsAnyLocalServerRunningRVA)())
		{

			if (!local_progression::PrepareServer()) return;
			uintptr_t connection = 0;
			safemem::ReadBytes(reinterpret_cast<void*>(g_base + game::kClient0ConnectionRVA), &connection, sizeof(connection));
			if (!EnsureSendGameStates()) return;
			if (g_playlistId >= 0 && !playlists::SelectDedicated(g_playlistId, g_playlistPlan.category))
			{
				Fail("could not restore native dedicated playlist after devmap");
				return;
			}
			int sendGameStates = -1;
			uintptr_t svGlobals = 0;
			ReadSendGameStates(sendGameStates, svGlobals);
			LOG_INFO("Dedicated", "sv_running=1; stock local connection allocated=%d; sendGameStates=%d sv_globals=%p", connection != 0,
				sendGameStates, reinterpret_cast<void*>(svGlobals));
			Advance(Stage::Running);
			char ip[32] = "unknown";
			console::GetLocalIPv4(ip, sizeof(ip));
			OP_INFO("Server ready | %s | %s | %s", ip, g_mapName, g_gameType);
		}
	}

	void Frame()
	{

		native_matchmaking::ServerPump();
		dedicated_window::MaintainHidden();
		asset_mark_guard::Report();
		online_services::Pump();
		Pump();
		const bool serverReady = g_stage == Stage::Running && ServerRunningForTrace() && MapSettingsReady();
		SetMapRestartReady(serverReady);
		online_services::PublishDedicated(g_autoRequestedPlaylist >= 0 ? g_autoRequestedPlaylist : g_playlistId,
			serverReady, g_mapName, g_gameType);
		g_frame.load(std::memory_order_acquire)(); // Entire native frame, including SV/network/party and pacing.
	}

	uintptr_t EnterFrontend()
	{
		// Native match-end has already sent the results/exit and shut SV down.
		// Its synchronous frontend reload can touch BAP's freed zone names before
		// our next Com_Frame sees sv_running=0. Reuse the existing restart policy
		// here, before that reload; never shorten the GSC extraction/results waits.
		if (bap::Enabled() && !ServerRunningForTrace() &&
			(g_stage == Stage::Running || g_stage == Stage::RestartDelay || g_stage == Stage::Stopping))
		{
			if (g_stage == Stage::Running)
			{
				g_botClassCommandQueued.store(false, std::memory_order_release);
				g_pendingBotAdds = 0;
				Advance(Stage::RestartDelay, 30000);
				OP_INFO("Match ended | native shutdown complete; recycling before frontend assets");
			}
			g_restartAt = 0;
			Pump();
			return 0;
		}
		return g_enterFrontend.load(std::memory_order_acquire)();
	}
}
