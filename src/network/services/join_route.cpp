#include <winsock2.h>

#include "engine/addresses.hpp"

#include <ws2tcpip.h>

#include "online_services.h"
#include "playlists.h"
#include "online_services_protocol.h"
#include "peer_services.h"
#include "native_network.h"
#include "match_http.h"
#include "match_assignment.h"
#include "match_connection.h"
#include "client_console.h"
#include "client/online_frontend.h"
#include "console.h"
#include "dedicated.h"

#include "game.h"

#include "join.h"
#include "offline_auth.h"

#include "safemem.h"

#include <charconv>
#include <algorithm>
#include <cstring>

#include <intrin.h>

#include <limits>
#include <mutex>

#include "network/services/internal.hpp"
#include "network/backend/backend_agent.h"
#include "network/social/party_transport.h"

#include "common/deployment_profile.h"

#include "network/social/chat_runtime.h"
#include "network/social/voice_runtime.h"
#include "network/social/discord_client.h"
#include "network/social/social_runtime.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/native_matchmaking.h"
#include "network/backend/publisher_variables.h"
#include "network/backend/playlist_backend.h"

#include <memory>
namespace online_services::detail
{
	// Only the client game thread owns this scoped transport/menu transition.
	bool g_lanJoin = false, g_lanSawGame = false;

	protocol::Session g_joinedDedicatedSession{};

	bool g_joinedDedicatedSessionValid = false;

	static bool g_restoreFrontendParty = false;

	static unsigned g_partyRestoreAttempts = 0;

	static ULONGLONG g_partyRestoreAt = 0;

	static bool g_previousSystemLink = false, g_previousSystemLinkHost = false;

	static ULONGLONG g_lanJoinAt = 0;

	std::atomic<DialogStateFn> g_joinDialogState{ nullptr }, g_pendingDialogState{ nullptr };

	uintptr_t JoinDialogState()
	{
		const auto state = g_joinDialogState.load(std::memory_order_acquire)();
		// Both getters feed only Live's presentation state in1511540. Suppress
		// AcceptingInvite before Live sets input catcher64 and queues addmenu.
		// Atomic join state, native timeouts/errors and ordinary peer joins remain
		// untouched. Keep the scope through map loading, across the two LUI VMs.
		return g_lanJoin && state == 1 ? 0 : state;
	}

	uintptr_t PendingDialogState()
	{
		const auto state = g_pendingDialogState.load(std::memory_order_acquire)();
		return g_lanJoin && state == 1 ? 0 : state;
	}

	std::atomic<bool (*)()> g_areBotsAllowed{ nullptr };

	std::atomic<bool (*)()> g_botsEnabled{ nullptr };

	bool AreBotsAllowed()
	{
		return true;
	}

	bool BotsEnabled()
	{
		// Replay inlines BG_AreBotsAllowed here. Keep its native allocation/update
		// guards and use the same permission as our Lobby hook (11095E0).
		return reinterpret_cast<bool (*)(int)>(g_base + game::kComGameModeSupportsFeatureRVA)(2) &&
			!reinterpret_cast<bool (*)()>(g_base + game::kIsFrontEndRVA)() &&
			reinterpret_cast<uint8_t (*)()>(g_base + game::kComGameModeGetActiveGameModeRVA)() != 3 && AreBotsAllowed();
	}

	bool ReadBoolDvar(uintptr_t rva, bool& value)
	{
		uintptr_t pointer = 0;
		uint8_t type = 255, byte = 0;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &pointer, sizeof(pointer)) || !pointer ||
			!safemem::ReadBytes(reinterpret_cast<void*>(pointer + game::kDvarTypeOffset), &type, 1) || type != 0 ||
			!safemem::ReadBytes(reinterpret_cast<void*>(pointer + 40), &byte, 1))
			return false;
		value = byte != 0;
		return true;
	}

	void SetBoolDvar(uintptr_t rva, bool value)
	{
		uintptr_t pointer = 0;
		safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &pointer, sizeof(pointer));
		reinterpret_cast<void (*)(uintptr_t, bool)>(g_base + game::kDvarSetBoolInternalRVA)(pointer, value);
	}

	void RestoreJoinRoute()
	{
		if (!g_lanJoin) return;
		if (g_lanSawGame)
		{
			g_restoreFrontendParty = true;
			g_partyRestoreAttempts = 0;
			g_partyRestoreAt = 0;
		}
		SetBoolDvar(game::kSystemLinkDvarRVA, g_previousSystemLink);
		SetBoolDvar(game::kSystemLinkHostDvarRVA, g_previousSystemLinkHost);
		g_lanJoin = false;
		MM_IsInMMLobby = false;
		LOG_INFO("OnlineServices", "restored frontend LAN flags after dedicated join/game");
	}

	void MaintainFrontendParty()
	{
		if (!g_restoreFrontendParty || online_frontend::local_play() || g_lanJoin || g_connection.Pending() || MM_IsThreadActive || MM_IsInMMLobby ||
			GetTickCount64() < g_partyRestoreAt)
			return;
		if (!reinterpret_cast<bool (*)()>(g_base + game::kIsInFrontEndRVA)() ||
			!reinterpret_cast<bool (*)()>(g_base + game::kIsFrontEndRVA)() || ReadNet(kPartyJoinStateRVA) != 0)
			return;
		constexpr uintptr_t party = game::addresses::runtime::private_party;
		bool oldDedicatedParty = false;
		if (ReadNet(party + 311116) == 1)
		{
			uintptr_t data = 0;
			protocol::Session session{};
			oldDedicatedParty = g_joinedDedicatedSessionValid &&
				safemem::ReadBytes(reinterpret_cast<void*>(g_base + party), &data, sizeof(data)) && data &&
				safemem::ReadBytes(reinterpret_cast<void*>(data + 4), session.data(), session.size()) &&
				session == g_joinedDedicatedSession;
			if (!oldDedicatedParty)
			{
				g_restoreFrontendParty = false;
				g_joinedDedicatedSessionValid = false;
				return;
			}
		}
		// Disconnect tears down the dedicated PrivateParty. Restoring route dvars
		// alone fixes menu selection but leaves no party until a mode/tab change.
		// Re-enter the verified stock CL_Main_StartPrivateParty(local,controller)
		// lifecycle after frontend loading; it constructs the session and registers
		// the local member normally. A naturally ended dedicated session can still
		// report inParty=1 until its 25-second timeout; only replace that exact
		// recorded session, never a different remote/menu party.
		g_partyRestoreAt = GetTickCount64() + 1000;
		++g_partyRestoreAttempts;
		reinterpret_cast<uintptr_t (*)(unsigned, unsigned)>(g_base + kStartPrivatePartyRVA)(0, 0);
		const bool valid = ReadNet(party + 311116) == 1;
		LOG_INFO("Party", "frontend party restore through native lifecycle: valid=%d attempt=%u old_dedicated=%d", valid,
			g_partyRestoreAttempts, oldDedicatedParty);
		if (valid || g_partyRestoreAttempts >= 3)
		{
			g_restoreFrontendParty = false;
			g_joinedDedicatedSessionValid = false;
		}
	}

	void CancelAssignedConnection()
	{
		if (!g_connection.Pending())
		{
			g_connection.Cancel();
			return;
		}
		g_connection.Cancel(); // Invalidate before native callbacks can re-enter.
		RestoreJoinRoute();
		if (ReadNet(kPartyJoinStateRVA) != 0)
		{
			// Stock Party_Backout1189590 uses reason23. Abort1606A20 cancels member
			// messages and closes temporary join/session state, retaining PartyData.
			// A bare memset16080D0 would leak connections and reliable messages.
			reinterpret_cast<void* (*)(void*, uint8_t)>(g_base + kPartyJoinAbortRVA)(
				reinterpret_cast<void*>(g_base + kPartyJoinStateRVA), 23);
			LOG_INFO("Party", "assigned join cancelled through native abort; frontend party retained");
		}
	}

		bool PrepareJoinRoute(std::string_view assignedMap, std::string_view assignedMode)
	{
		if (!g_directDedicated) return true;
		RestoreJoinRoute();
		online_frontend::set_local_play(false);
			if (!playlists::PrepareClientJoin(MM_ActivePlaylistID.load(), assignedMap, assignedMode)) return false;
		if (!ReadBoolDvar(game::kSystemLinkDvarRVA, g_previousSystemLink) ||
			!ReadBoolDvar(game::kSystemLinkHostDvarRVA, g_previousSystemLinkHost))
			return false;
		// xstartlobby leaves a locally hosted GameParty awake. Enabling systemlink
		// lets its host frame start a local countdown/map while the assigned remote
		// join is still pending (observed on the second search after Disconnect).
		// Commit1607AB0 sleeps it only AFTER admission, which is too late. Use the
		// same native sleep before changing the route: preserve its members/session
		// and PrivateParty, but let only the assigned server start this match.
		if (ReadNet(kGamePartyRVA + 311112) == 1 && ReadNet(kGamePartyRVA + 311116) == 1 && ReadNet(kGamePartyRVA + 311120) == 1)
		{
			reinterpret_cast<void (*)(void*)>(g_base + kPartySleepRVA)(reinterpret_cast<void*>(g_base + kGamePartyRVA));
			LOG_INFO("Party", "local matchmaking lobby asleep before assigned server handoff");
		}
		// Native UI JoinServer (1B9E5A0) is entered from the LAN frontend and clears
		// systemlink_host before 1608270. The online menu must enter that same mode
		// for the subsequent party -> running game transition, not just the invite.
		SetBoolDvar(game::kSystemLinkDvarRVA, true);
		SetBoolDvar(game::kSystemLinkHostDvarRVA, false);
		g_lanJoin = true;
		g_lanSawGame = false;
		g_lanJoinAt = GetTickCount64();
		g_joinedDedicatedSessionValid = false;
		LOG_INFO("OnlineServices", "entering native LAN join from online frontend: systemlink=1 systemlink_host=0");
		return true;
	}

	void MaintainAssignedConnection()
	{
		if (!g_connection.Pending()) return;
		const bool frontend = reinterpret_cast<bool (*)()>(g_base + game::kIsInFrontEndRVA)();
		const bool sceneReady = reinterpret_cast<bool (*)()>(g_base + game::kIsFrontEndRVA)();
		const int state = ReadNet(game::kClient0ConnectionStateRVA);
		const auto action = g_connection.Advance(GetTickCount64(), joindiag::TransportFailureSequence(), state == 9 && !frontend,
			matchmaking::ConnectionIdle(frontend, sceneReady, state, ReadNet(kPartyJoinStateRVA)));
		if (action == matchmaking::ConnectionAction::Connected)
		{
			LOG_INFO("Connect", "assigned endpoint connected; native gameplay active");
			return;
		}
		if (action == matchmaking::ConnectionAction::TryLan)
		{
			LOG_WARN("Connect", "public endpoint failed after native cleanup; attempting LAN fallback once");
			RestoreJoinRoute();
			const bool accepted = PrepareJoinRoute() && clientconsole::JoinSession(protocol::SessionHex(g_joinLan), false);
			if (!accepted)
			{
				g_connection.phase = matchmaking::ConnectionPhase::Failed;
				RestoreJoinRoute();
			}
			MM_IsInMMLobby = accepted;
		}
		else if (action == matchmaking::ConnectionAction::Failed)
		{
			OP_WARN("Assigned server connection failed; start a new search to retry");
			RestoreJoinRoute();
		}
	}

	void MaintainJoinRoute()
	{
		if (!g_lanJoin) return;
		if (g_connection.Pending()) return; // Assigned route owns failure/timeout cleanup.
		// Scene readiness can cycle while the frontend is preloading a remote map.
		// Only actual frontend mode identifies a completed gameplay round trip.
		const bool frontend = reinterpret_cast<bool (*)()>(g_base + game::kIsInFrontEndRVA)();
		if (!frontend && !g_lanSawGame)
		{
			g_lanSawGame = true;
			uintptr_t data = 0;
			g_joinedDedicatedSessionValid =
				safemem::ReadBytes(reinterpret_cast<void*>(g_base + game::addresses::runtime::private_party), &data, sizeof(data)) &&
				data &&
				safemem::ReadBytes(reinterpret_cast<void*>(data + 4), g_joinedDedicatedSession.data(), g_joinedDedicatedSession.size());
		}
		constexpr uintptr_t privateParty = game::addresses::runtime::private_party;
		const bool remoteParty =
			ReadNet(privateParty + 311116) != 0 && ReadNet(privateParty + 311120) != 0 && ReadNet(privateParty + 311112) == 0;
		// Party join state 0 means no join in progress. Do not restore while loading
		// a real map; only upon frontend return, or a failed frontend-only attempt.
		if (frontend &&
			((g_lanSawGame && ReadNet(kPartyJoinStateRVA) == 0) ||
				(!g_lanSawGame && !remoteParty && GetTickCount64() - g_lanJoinAt >= 30000 && ReadNet(kPartyJoinStateRVA) == 0)))
			RestoreJoinRoute();
	}

	void ResetMatchmaking()
	{
		std::lock_guard<std::mutex> lock(g_lock);
		ResetMatchmakingLocked();
	}
}

namespace online_services
{
	using namespace detail;

	void OnLuaStartup()
	{
		if (!g_installed.load(std::memory_order_acquire) || dedicated::Enabled() || !g_lanJoin || !g_lanSawGame ||
			ReadNet(kPartyJoinStateRVA) != 0)
			return;
		// LUI_CoD_Init loads startup before calling Main/OpenInitialMenu. The
		// scene-ready getter used by Pump is still false here, so waiting for that
		// frame would let Main choose MPSystemLinkMenu using our temporary flags.
		// Check actual frontend mode; ingame startup/recovery must retain the route.
		if (!reinterpret_cast<bool (*)()>(g_base + game::kIsInFrontEndRVA)()) return;
		LOG_INFO("OnlineServices", "restoring dedicated join route before frontend Lua startup");
		RestoreJoinRoute();
	}

	bool OnNativeJoinFailure(std::string_view reason)
	{
		if (!g_installed.load(std::memory_order_acquire) || dedicated::Enabled() || (!g_lanJoin && !g_connection.Pending())) return false;
		const bool retry = g_connection.Pending() && g_connection.phase == matchmaking::ConnectionPhase::TryPublic && g_connection.hasLan &&
			matchmaking::TransportJoinFailure(reason);
		// 1605780 invokes frontend recovery before the next Pump. Waiting for Pump
		// lets Main select the offline/system-link menu from our temporary dvars.
		RestoreJoinRoute();
		if (!matchmaking::TransportJoinFailure(reason)) g_connection.Cancel();
		return retry;
	}

	bool SilentMatchJoin()
	{
		return g_lanJoin && !g_lanSawGame;
	}

	bool RestoringFrontendParty()
	{
		return g_restoreFrontendParty;
	}

	bool InDedicatedMatch()
	{
		return native_matchmaking::InMatch() || (g_directDedicated && g_lanJoin && g_lanSawGame);
	}
}
