#include <winsock2.h>
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
namespace online_services
{
	using namespace detail;

	void CaptureSession(const void* session, size_t size)
	{
		if (size != protocol::kSessionBytes) return;
		protocol::Session copy{};
		if (!safemem::ReadBytes(session, copy.data(), copy.size())) return;
		std::lock_guard<std::mutex> lock(g_lock);
		g_hostSession = copy;
		g_hasSession = native_network::PublicationReady();
	}
#ifndef IW8X_CLIENT_ONLY

	void PublishDedicated(int playlistId, bool ready, const char* map, const char* mode)
	{
		// The dedicated frame pump can run while later startup components are
		// still verifying content. Native service bindings are published by Install.
		if (!g_installed.load(std::memory_order_acquire)) return;
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_dedicatedResetting) return;
		if (g_dedicatedReady != ready || g_dedicatedPlaylist != playlistId)
			LOG_INFO("OnlineServices", "dedicated publication: playlist=%d ready=%d session=%d", playlistId, ready, g_hasSession);
		g_dedicatedPlaylist = playlistId;
		if (ready && !g_dedicatedReady)
			g_assignmentId = "match_" + std::to_string(GetTickCount64()) + "_" + std::to_string(GetCurrentProcessId());
		g_dedicatedMap = map ? map : "";
		g_dedicatedMode = mode ? mode : "";
		g_dedicatedReady = ready;
		g_dedicatedFrame = GetTickCount64();
		chat_runtime::PublishDedicated(ready && g_hasSession
			? matchmaking::Hex(std::string_view(reinterpret_cast<const char*>(g_hostSession.data() + 84), 8))
			: std::string{});
		const auto socialEndpoint = SessionEndpoint(g_hostSession, 30);
		social_runtime::PublishDedicated(ready && g_hasSession ? protocol::SessionHex(g_hostSession) : std::string{}, playlistId, map, mode,
			socialEndpoint.host + ":" + std::to_string(socialEndpoint.port), ReadNet(0xE17FCD0));
		if (backend_agent::Enabled())
			backend_agent::Publish(nlohmann::json{ { "host_id", std::to_string(offlineauth::GetIdentity().xuid) },
				{ "playlist", playlistId }, { "ready", ready && g_hasSession }, { "resetting", false },
				{ "session", ready && g_hasSession ? protocol::SessionHex(g_hostSession) : std::string{} }, { "map", g_dedicatedMap },
				{ "mode", g_dedicatedMode }, { "capacity", ReadNet(0xE17FCD0) },
				{ "game_mode", reinterpret_cast<uint8_t (*)()>(g_base + game::kComGameModeGetActiveGameModeRVA)() },
				{ "endpoint", socialEndpoint.host + ":" + std::to_string(socialEndpoint.port) }, { "assignment", g_assignmentId },
				{ "players", nlohmann::json::array() } });
		static ULONGLONG peerAt = 0;
		if (g_dedicatedFrame >= peerAt)
		{
			peers::Publish(offlineauth::GetIdentity().xuid,
				ready && g_hasSession ? peers::wire::State::Dedicated : peers::wire::State::Idle, playlistId, g_hostSession);
			peerAt = g_dedicatedFrame + 500;
		}
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	bool TakeDedicatedRequest(int& playlistId)
	{

		if (backend_agent::Enabled())
		{
			if (!backend_agent::TakePlaylist(playlistId)) return false;
			std::lock_guard lock(g_lock);
			g_dedicatedPlaylist = playlistId;
			g_dedicatedReady = false;
			g_hasSession = false;
			return true;
		}
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_dedicatedResetting) return false;
		// Cancellation can arrive after the worker reserves a request but before
		// the main thread consumes it. Recheck live leases under the same lock.
		g_waitingQueue.Expire(GetTickCount64());
		if (g_waitingQueue.Tickets().empty()) g_requestedPlaylist = -1;
		if (g_requestedPlaylist < 0 || (g_tournament.id && g_waitingQueue.Tickets().size() != 4)) return false;
		playlistId = g_requestedPlaylist;
		g_requestedPlaylist = -1;
		// Reserve this host before the game thread validates the map/recipe. A
		// second search cannot replace a match that is already being prepared.
		g_dedicatedPlaylist = playlistId;
		g_dedicatedReady = false;
		g_hasSession = false;
		return true;
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	void RejectDedicatedPlaylist(int playlistId)
	{

		if (backend_agent::Enabled()) backend_agent::Reject(playlistId);
		std::lock_guard<std::mutex> lock(g_lock);
		if (playlistId >= 0 && playlistId <= 128) g_unavailablePlaylists[playlistId] = true;
		g_dedicatedPlaylist = -1;
		g_dedicatedReady = false;
		g_hasSession = false;
		g_waitingQueue.Clear();
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	void BeginDedicatedReset()
	{
		if (backend_agent::Enabled()) backend_agent::Reset();
		std::lock_guard lock(g_lock);
		g_dedicatedResetting = true;
		g_dedicatedReady = false;
		g_hasSession = false;
		g_hostSession = {};
		g_requestedPlaylist = -1;
		g_waitingQueue.Clear();
		peers::Publish(offlineauth::GetIdentity().xuid, peers::wire::State::Unavailable, 0, {});
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	void FinishDedicatedReset()
	{
		std::lock_guard lock(g_lock);
		g_dedicatedPlaylist = -1;
		g_requestedPlaylist = -1;
		g_dedicatedReady = false;
		g_hasSession = false;
		g_hostSession = {};
		g_waitingQueue.Clear();
		g_dedicatedResetting = false;
	}
#endif


	bool PrepareSocialGameJoin(int playlist, void* session108)
	{
		if (!g_base || dedicated::Enabled() || !session108 || !g_directDedicated || !playlists::SelectForMatchmaking(playlist))
			return false;
		protocol::Session raw{};
		memcpy(raw.data(), session108, raw.size());
		const auto endpoint = SessionEndpoint(raw, 30);
		if (!endpoint.port) return false;
		matchmaking::WireEndpoint publicAddress{}, lanAddress{};
		std::copy_n(raw.begin() + 30, 6, publicAddress.begin());
		bool hasLan = false;
		for (size_t i = 0; i < 30; i += 6)
		{
			const auto* p = raw.data() + i;
			if (SessionEndpoint(raw, i).port && (p[0] == 10 || (p[0] == 192 && p[1] == 168) || (p[0] == 172 && p[1] >= 16 && p[1] <= 31)))
			{
				std::copy_n(raw.begin() + i, 6, lanAddress.begin());
				hasLan = lanAddress != publicAddress;
				break;
			}
		}
		MM_ActivePlaylistID = playlist;
		{
			std::lock_guard lock(g_lock);
			ResetMatchmakingLocked();
		}
		if (!PrepareJoinRoute()) return false;
		g_connection.Start(GetTickCount64(), joindiag::TransportFailureSequence(), hasLan);
		g_joinLan = matchmaking::RouteSession(raw, lanAddress);
		const auto routed = matchmaking::RouteSession(raw, publicAddress);
		memcpy(session108, routed.data(), routed.size());
		MM_IsInMMLobby = true;
		LOG_INFO("Social", "native game admission: playlist=%d backend endpoint=%s:%u; LAN retry only after native failure", playlist,
			endpoint.host.c_str(), endpoint.port);
		return true;
	}

	bool StartMatchmaking(int playlistId)
	{
		return StartMatchmaking(std::vector<int>{ playlistId });
	}

	bool StartMatchmaking(const std::vector<int>& playlistIds)
	{
		if (dedicated::Enabled())
		{
			OP_ERR("Dedicated hosts use --playlist; mm_start is client-only");
			return false;
		}
		if (online_frontend::local_play() || playlistIds.empty() || playlistIds.size() > 20) return false;
		peers::Cancel();
		std::vector<int> candidates;
		for (const int id : playlistIds)
		{
			if (id < 0 || id > 128 || !playlists::HasPlaylist(id)) return false;
			if (std::find(candidates.begin(), candidates.end(), id) == candidates.end()) candidates.push_back(id);
		}
		if (!g_directDedicated && candidates.size() != 1)
		{
			OP_ERR("Multiple Quick Play filters require dedicated matchmaking mode");
			return false;
		}
		const int playlistId = candidates.front();
		MaintainFrontendParty();
		uint64_t generation = 0;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			if (!AvailableLocked())
			{
				OP_ERR("Matchmaking client unavailable; use online_status");
				return false;
			}
			generation = g_matchmakingGeneration;
		}
		if (!playlists::SelectForMatchmaking(playlistId))
		{
			OP_ERR("Playlist %d cannot be selected; return to MP frontend and use playlist_status or playlist_load", playlistId);
			return false;
		}

		if (native_matchmaking::Enabled())
		{
			MM_ActivePlaylistID = playlistId;
			OP_INFO("Native matchmaking selected playlist %d; stock xstartlobby owns search", playlistId);
			return true;
		}
		CancelAssignedConnection();
		auto members = ReadQueueParty(offlineauth::GetIdentity().xuid);
		{
			std::lock_guard<std::mutex> lock(g_lock);
			if (generation != g_matchmakingGeneration || !AvailableLocked()) return false;
			ResetMatchmakingLocked();
			const auto identity = offlineauth::GetIdentity();
			g_queuePlayer.xuid = identity.xuid;
			g_queuePlayer.name = identity.name;
			g_queueParty = std::move(members);
			g_searchPlaylists = candidates;
			MM_ActivePlaylistID = playlistId;
			g_nextSearch = 0;
			MM_IsSearchingForMatch = true;
			MM_IsThreadActive = true;
		}
		OP_INFO("Matchmaking enabled for %zu registered playlist(s), first=%d", candidates.size(), playlistId);
		return true;
	}

	void CancelMatchmaking()
	{

		if (native_matchmaking::Enabled()) native_matchmaking::Cancel();
		CancelAssignedConnection();
		{
			std::lock_guard lock(g_lock);
			if (g_directDedicated && MM_IsThreadActive)
			{
				match_queue::Packet cancel;
				cancel.kind = match_queue::kCancel;
				cancel.request = g_matchmakingGeneration;
				cancel.player = g_queuePlayer;
				const auto wire = match_queue::Encode(cancel);
				if (g_httpEnabled)
				{
					if (g_httpCancels.size() < 8) g_httpCancels.emplace_back(g_httpOrigin, cancel);
				}
				else
					SendPacketLocked(wire.data(), static_cast<int>(wire.size()));
			}
			ResetMatchmakingLocked();
		}
		LOG_INFO("OnlineServices", "matchmaking stopped locally");
	}

	bool CanConnectPeer()
	{
		if (dedicated::Enabled() || !g_installed || !Running() || !g_directDedicated || !offlineauth::IsReady() ||
			!reinterpret_cast<bool (*)()>(g_base + game::kIsInFrontEndRVA)())
		{
			OP_WARN("Return to the online Multiplayer/Warzone menu before using connect");
			return false;
		}
		// Atomic admission can finish before map preload. A second connect in that
		// gap replaces the live join/session and sends a duplicate join request.
		if (ReadNet(kPartyJoinStateRVA) != 0 || (g_lanJoin && !g_lanSawGame))
		{
			OP_WARN("A dedicated/native join is already in progress; wait for map loading");
			return false;
		}
		return true;
	}

	bool SetMatchHost(const sockaddr_in& host)
	{
		if (dedicated::Enabled() || !g_directDedicated || !Running() || host.sin_family != AF_INET || !host.sin_port ||
			!host.sin_addr.s_addr || host.sin_addr.s_addr == INADDR_BROADCAST || IN_MULTICAST(ntohl(host.sin_addr.s_addr)))
		{
			OP_WARN("mm_host requires a running client and a valid IPv4 endpoint");
			return false;
		}
		CancelMatchmaking();
		char ip[INET_ADDRSTRLEN]{};
		inet_ntop(AF_INET, &host.sin_addr, ip, sizeof(ip));

		std::wstring publisherOrigin;
		{
			std::lock_guard guard(g_lock);
			g_server = host;
			g_received = 0;
			g_banStatus = 0;
			g_packets = 0;
			if (g_httpEnabled && !g_httpOriginExplicit)
				g_httpOrigin = L"http://" + std::wstring(ip, ip + strlen(ip)) + L":" + std::to_wstring(g_httpPort);

			publisherOrigin = g_httpOrigin;
		}
		if (g_httpEnabled)
		{
			publisher_variables::Configure(publisherOrigin);
			playlist_backend::Configure(false, publisherOrigin);
			frontend_backend::Configure(false, publisherOrigin);
		}
		peers::SetDirectory(host);
		OP_INFO("Matchmaking host: %s:%u (current launch)", ip, unsigned(ntohs(host.sin_port)));
		return true;
	}

	bool IsSearching()
	{
		return native_matchmaking::Enabled() ? native_matchmaking::Searching() : MM_IsSearchingForMatch.load(std::memory_order_acquire);
	}

	BackendStatus GetStatus()
	{
		std::lock_guard<std::mutex> lock(g_lock);
		const auto received = g_received.load();
		const auto now = GetTickCount64();
		return { Running(), g_banStatus.load(), ForceDLLUpdateRequired.load(), g_packets.load(),
			received ? now - received : std::numeric_limits<uint64_t>::max() };
	}

	match_queue::Packet GetQueueView()
	{
		std::lock_guard lock(g_lock);
		auto view = g_queueView;
		view.valid = g_directDedicated && (MM_IsThreadActive || (g_lanJoin && !g_lanSawGame));
		if (!view.valid)
		{
			view.players.clear();
			return view;
		}
		if (MM_IsThreadActive && g_queueReplyAt && GetTickCount64() - g_queueReplyAt > 7000)
		{
			view.players.clear();
			view.phase = match_queue::Phase::Waiting;
		}
		if (view.players.empty())
		{
			view.players.push_back(g_queuePlayer);
			view.players.insert(view.players.end(), g_queueParty.begin(), g_queueParty.end());
			view.seconds = 60;
		}
		return view;
	}

	void SetQueueCard(std::string_view clan, int rank, int prestige, int emblem, int background)
	{
		std::lock_guard lock(g_lock);
		if (match_queue::Text(clan, 5)) g_queuePlayer.clan = clan;
		g_queuePlayer.rank = uint16_t(std::clamp(rank, 0, 10000));
		g_queuePlayer.prestige = uint16_t(std::clamp(prestige, 0, 1000));
		g_queuePlayer.emblem = unsigned(std::max(0, emblem));
		g_queuePlayer.background = unsigned(std::max(0, background));
	}

	bool HandleCommand(std::string_view command)
	{

		if (frontend_backend::HandleCommand(command)) return true;
		if (publisher_variables::HandleCommand(command)) return true;
		if (command == "playlist_backend_reload" || command == "playlist_status") return playlists::HandleCommand(command);
		if (native_matchmaking::Command(command)) return true;
		if (chat_runtime::HandleCommand(command)) return true;
		if (social_runtime::HandleCommand(command)) return true;
		if (command == "net_status")
		{
			native_network::PrintStatus();
			return true;
		}
		if (peers::HandleCommand(command)) return true;
#ifndef IW8X_CLIENT_ONLY
		if (command == "force_start")
		{
			if (!dedicated::Enabled())
			{
				OP_WARN("force_start is available only in the dedicated console");
				return true;
			}
			std::lock_guard lock(g_lock);
			if (g_dedicatedResetting || !dedicated::AutoPlaylist() || g_dedicatedPlaylist >= 0 || !g_waitingQueue.Force())
				OP_WARN("No waiting lobby to start");
			else
				OP_INFO("Waiting lobby force-start requested");
			return true;
		}
#endif

#ifndef IW8X_CLIENT_ONLY
		if (command == "lobby_status" && dedicated::Enabled())
		{
			std::lock_guard lock(g_lock);
			OP_INFO("Waiting lobby | playlist=%d players=%zu start_in=%us", g_waitingQueue.Playlist(), g_waitingQueue.Tickets().size(),
				g_waitingQueue.Seconds(GetTickCount64()));
			for (const auto& t : g_waitingQueue.Tickets())
				OP_INFO("  %s [%llu]", t.player.name.c_str(), static_cast<unsigned long long>(t.player.xuid));
			return true;
		}
#endif

		if (command == "online_stop")
		{
			Stop();
			LOG_INFO("OnlineServices", "custom master stop requested; worker_running=%d", int(g_running.load()));
			return true;
		}
		if (command == "online_start")
		{
			Component_Stage_Unpack();
			return true;
		}
		if (command == "online_status")
		{
			{
				std::lock_guard<std::mutex> lock(g_lock);
				LOG_INFO("OnlineServices", "mode=%s listener=%d hosted_playlist=%d ready=%d host_session=%d",
					g_directDedicated ? "dedicated" : "legacy", g_listening, g_dedicatedPlaylist, g_dedicatedReady, g_hasSession);
				LOG_INFO("OnlineServices",
					"worker=%d packets_received=%u account_status=%d searching=%d lobby_or_join_pending=%d playlist=%d update_required=%d",
					int(g_running.load()), g_packets.load(), g_banStatus.load(), int(MM_IsSearchingForMatch.load()),
					int(MM_IsInMMLobby.load()), MM_ActivePlaylistID.load(), int(ForceDLLUpdateRequired.load()));
			}
			joindiag::Snapshot("online_status");
			return true;
		}
		if (command == "mm_stop")
		{
			peers::Cancel();
			CancelMatchmaking();
			return true;
		}
		if (command == "mm_start" || command.starts_with("mm_start "))
		{
			int playlist = -1;
			auto value = command.size() > 9 ? command.substr(9) : std::string_view{};
			if (value.empty())
			{
				OP_ERR("Usage: mm_start <playlist 0..128>");
				return true;
			}
			const auto parsed = std::from_chars(value.data(), value.data() + value.size(), playlist);
			if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || playlist < 0 || playlist > 128)
			{
				OP_ERR("Usage: mm_start <playlist 0..128>");
				return true;
			}
			StartMatchmaking(playlist);
			return true;
		}
		return false;
	}

	uint64_t MatchmakingHostId()
	{
		return offlineauth::GetIdentity().xuid;
	}
#ifndef IW8X_CLIENT_ONLY

	matchmaking::Assignment AllocateNativeMatch(const match_queue::Packet& query, uint32_t peer)
	{
		sockaddr_in from{};
		from.sin_family = AF_INET;
		from.sin_addr.s_addr = peer;
		std::lock_guard lock(g_lock);
		matchmaking::Assignment a;
		a.queue = ProcessQueueLocked(query, from, true);
		a.id = g_assignmentId;
		a.map = g_dedicatedMap;
		a.mode = g_dedicatedMode;
		a.publicEndpoint = SessionEndpoint(g_hostSession, 30);
		if (a.queue.phase == match_queue::Phase::Ready)
		{
			std::array<uint8_t, 4> peerAddress{};
			memcpy(peerAddress.data(), &peer, 4);
			a.queue.session = matchmaking::NativeRouteSession(a.queue.session, peerAddress);
			if (!a.publicEndpoint.port) a.publicEndpoint = SessionEndpoint(a.queue.session, 0);
		}
		return a;
	}
#endif

#ifndef IW8X_CLIENT_ONLY

	tournament120::Host TournamentHost()
	{
		std::lock_guard lock(g_lock);
		tournament120::Host h;
		h.sampled = g_listening && g_dedicatedFrame != 0;
		h.frame = g_dedicatedFrame;
		h.waiting = !g_dedicatedResetting && g_dedicatedPlaylist < 0 && g_waitingQueue.Tickets().empty();
		h.running = g_dedicatedReady && g_hasSession;
		h.allocation = g_tournament.id;
		return h;
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	bool ReserveTournament(const tournament120::Reservation& r)
	{
		std::lock_guard lock(g_lock);
		if (r.id == g_tournament.id)
		{
			g_tournament = r;
			g_tournamentView.store(std::make_shared<const tournament120::Reservation>(r));
			return true;
		}
		if (g_dedicatedResetting || g_dedicatedPlaylist >= 0 || !g_waitingQueue.Tickets().empty()) return false;
		g_tournament = r;
		g_tournamentView.store(std::make_shared<const tournament120::Reservation>(r));
		return true;
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	int TournamentTeam(uint64_t id)
	{
		if (backend_agent::Enabled()) return backend_agent::Team(id);
		const auto r = g_tournamentView.load();
		if (!r || !r->id) return 0;
		const auto team = r->teams.find(id);
		return team == r->teams.end() ? 0 : team->second;
	}
#endif

}
