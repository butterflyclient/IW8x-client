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
#ifndef IW8X_CLIENT_ONLY
	// Both transports use the same queue, lease, allocation and native-session
	// snapshot. Merely polling /v1/server never reaches this mutation path.
	match_queue::Packet ProcessQueueLocked(const match_queue::Packet& query, const sockaddr_in& from, bool authenticated)
	{
		match_queue::Packet reply;
		reply.kind = match_queue::kReply;
		reply.request = query.request;
		const auto now = GetTickCount64();
		if (query.kind == match_queue::kCancel)
		{
			g_waitingQueue.Cancel(query.player.xuid, query.request, from.sin_addr.s_addr, from.sin_port);
			return reply;
		}
		if (g_tournament.id)
		{
			bool allowed = authenticated && query.playlists == std::vector<int>{ tournament120::Playlist } &&
				g_tournament.teams.contains(query.player.xuid);
			for (const auto& p : query.players)
				allowed = allowed && g_tournament.teams.contains(p.xuid) &&
					g_tournament.teams.at(p.xuid) == g_tournament.teams.at(query.player.xuid);
			if (!allowed)
			{
				reply.phase = match_queue::Phase::Unavailable;
				return reply;
			}
		}
		g_waitingQueue.Expire(now);
		int playlist = g_dedicatedPlaylist >= 0 ? g_dedicatedPlaylist : g_waitingQueue.Playlist();
		if (playlist < 0)
			for (int id : query.playlists)
				if (!g_unavailablePlaylists[id])
				{
					playlist = id;
					break;
				}
		reply.playlist = playlist >= 0 ? playlist : query.playlists.front();
		if (g_dedicatedResetting || playlist < 0 || (!dedicated::AutoPlaylist() && g_dedicatedPlaylist < 0))
			reply.phase = match_queue::Phase::Unavailable;
		else if (std::find(query.playlists.begin(), query.playlists.end(), playlist) == query.playlists.end())
			reply.phase = match_queue::Phase::WrongPlaylist;
		else
		{
			const bool added =
				g_waitingQueue.AddGroup(playlist, { query.player, query.request, now, from.sin_addr.s_addr, from.sin_port }, query.players);
			const bool ready = g_dedicatedReady && g_hasSession && now - g_dedicatedFrame < 5000;
			reply.phase = ready ? match_queue::Phase::Ready
				: g_dedicatedPlaylist >= 0 ? match_queue::Phase::Loading
				: match_queue::Phase::Waiting;
			// A rejected/duplicate roster is never entitled to a session, even if
			// a previous match has become ready in the meantime.
			if (!added) reply.phase = match_queue::Phase::Unavailable;
			reply.seconds = g_waitingQueue.Seconds(now);
			reply.session = g_hostSession;
			for (const auto& ticket : g_waitingQueue.Tickets())
				reply.players.push_back(ticket.player);
		}
		return reply;
	}
#endif


	matchmaking::Endpoint SessionEndpoint(const protocol::Session& session, size_t offset)
	{
		uint16_t port = 0;
		memcpy(&port, session.data() + offset + 4, 2);
		in_addr address{};
		memcpy(&address, session.data() + offset, 4);
		char ip[INET_ADDRSTRLEN]{};
		if (!port || !address.s_addr) return {};
		inet_ntop(AF_INET, &address, ip, sizeof(ip));
		return { ip, port };
	}
#ifndef IW8X_CLIENT_ONLY

	match_http::Reply HttpRequest(std::string_view path, std::string_view body, uint32_t ip)
	{

		if (path == "/v1/frontend") return frontend_backend::Request(body, ip);
		if (path == "/v1/playlists") return playlist_backend::Request(body);
		if (path == "/v1/publisher-variables") return publisher_variables::Request(body);
		if (path == "/v1/native-mm") return native_matchmaking::Request(body, ip);
		if (path.starts_with("/v1/chat/")) return chat_runtime::Request(path, body, ip);
		if (path.starts_with("/v1/social/") || path == "/v1/party-relay") return social_runtime::Request(path, body, ip);
		if (path == "/v1/server")
		{
			std::lock_guard lock(g_lock);
			return { 200,
				matchmaking::Json{ { "protocol", matchmaking::kProtocol },
					{ "state",
						g_dedicatedResetting ? "resetting"
							: g_dedicatedReady ? "ready"
							: "available" },
					{ "playlist", g_dedicatedPlaylist }, { "map", g_dedicatedMap }, { "mode", g_dedicatedMode },
					{ "build", "1.20.4.7623265" }, { "heartbeat_age_ms", GetTickCount64() - g_dedicatedFrame } }
					.dump() };
		}
		if (path != "/v1/matchmaking/search" && path != "/v1/matchmaking/cancel") return { 404, "{}" };
		const auto query = matchmaking::ReadRequest(body);
		if (!query.valid || (path.ends_with("cancel") != (query.kind == match_queue::kCancel)))
			return { 400, "{\"error\":\"invalid queue request\"}" };
		sockaddr_in from{};
		from.sin_family = AF_INET;
		from.sin_addr.s_addr = ip;
		std::lock_guard lock(g_lock);
		matchmaking::Assignment a;
		a.queue = ProcessQueueLocked(query, from);
		if (query.kind == match_queue::kCancel) return { 200, "{}" };
		if (a.queue.phase == match_queue::Phase::Ready)
		{
			a.id = g_assignmentId;
			a.map = g_dedicatedMap;
			a.mode = g_dedicatedMode;
			// Exact TU19 CommonAddr serializer: 5 local endpoints, then public.
			a.publicEndpoint = SessionEndpoint(a.queue.session, 30);
			for (size_t i = 0; i < 30; i += 6)
			{
				const auto candidate = SessionEndpoint(a.queue.session, i);
				const auto* p = a.queue.session.data() + i;
				if (candidate.port && (p[0] == 10 || (p[0] == 192 && p[1] == 168) || (p[0] == 172 && p[1] >= 16 && p[1] <= 31)))
				{
					a.lanEndpoint = candidate;
					break;
				}
			}
			if (!a.publicEndpoint.port || !matchmaking::Token(a.map) || !matchmaking::Token(a.mode))
				return { 503, "{\"error\":\"native endpoint/map not ready\"}" };
		}
		return { 200, matchmaking::Response(a) };
	}
#endif


	void PollHttp()
	{
		match_queue::Packet query;
		std::wstring origin;
		{
			std::lock_guard lock(g_lock);
			if (!g_httpCancels.empty())
			{
				origin = std::move(g_httpCancels.front().first);
				query = std::move(g_httpCancels.front().second);
				g_httpCancels.erase(g_httpCancels.begin());
			}
			else if (!g_listening && MM_IsThreadActive && !MM_IsInMMLobby && !g_pending && GetTickCount64() >= g_nextSearch)
			{
				query.request = g_matchmakingGeneration;
				query.player = g_queuePlayer;
				query.playlists = g_searchPlaylists;
				query.players = g_queueParty;
				g_nextSearch = GetTickCount64() + 1000;
			}
			else
				return;
			if (origin.empty()) origin = g_httpOrigin;
		}
		const auto reply = match_http::Post(origin,
			query.kind == match_queue::kCancel ? L"/v1/matchmaking/cancel" : L"/v1/matchmaking/search", matchmaking::Request(query));
		if (query.kind == match_queue::kCancel) return;
		matchmaking::Assignment a;
		bool valid = reply.status == 200 && matchmaking::ReadResponse(reply.body, query.request, query.playlists, a);
		protocol::Session publicRoute{}, lanRoute{};
		bool hasLan = false;
		if (valid && a.queue.phase == match_queue::Phase::Ready)
		{
			auto resolve = [](const matchmaking::Endpoint& e, matchmaking::WireEndpoint& wire)
			{
				in_addr ip{};
				if (!e.port || !native_network::ResolveIPv4(e.host, ip)) return false;
				memcpy(wire.data(), &ip, 4);
				memcpy(wire.data() + 4, &e.port, 2);
				return true;
			};
			matchmaking::WireEndpoint publicAddress{}, lanAddress{};
			valid = resolve(a.publicEndpoint, publicAddress) &&
				std::equal(publicAddress.begin(), publicAddress.end(), a.queue.session.begin() + 30);
			if (valid)
			{
				publicRoute = matchmaking::RouteSession(a.queue.session, publicAddress);
				hasLan = a.lanEndpoint.port && resolve(a.lanEndpoint, lanAddress) && lanAddress != publicAddress;
				if (hasLan) lanRoute = matchmaking::RouteSession(a.queue.session, lanAddress);
			}
		}
		std::lock_guard lock(g_lock);
		if (query.request != g_matchmakingGeneration || !MM_IsThreadActive || g_pending || !AvailableLocked()) return;
		if (!valid)
		{
			static ULONGLONG warningAt = 0;
			if (GetTickCount64() >= warningAt)
			{
				warningAt = GetTickCount64() + 10000;
				LOG_WARN("Matchmaking", "HTTP assignment rejected: status=%u reason=%s", reply.status,
					reply.status == 200 ? "malformed/missing fields or endpoint disagrees with native session" : reply.body.c_str());
			}
			return;
		}
		++g_packets;
		g_received = GetTickCount64();
		g_banStatus = 1;
		if (a.queue.phase == match_queue::Phase::Unavailable || a.queue.phase == match_queue::Phase::WrongPlaylist)
		{
			OP_WARN("No selected playlist is available on this dedicated host");
			ResetMatchmakingLocked();
			return;
		}
		g_queueView = a.queue;
		g_queueReplyAt = GetTickCount64();
		if (a.queue.phase == match_queue::Phase::Ready)
		{
			g_pendingSession = a.queue.session;
			g_pendingAssignment = std::move(a);
			g_pending = true;
			g_pendingPublic = publicRoute;
			g_pendingLan = lanRoute;
			g_pendingHasLan = hasLan;
			MM_ActivePlaylistID = g_pendingAssignment.queue.playlist;
			MM_IsSearchingForMatch = false;
			LOG_INFO("Matchmaking", "HTTP assignment received: match=%s playlist=%d map=%s mode=%s public=%s:%u LAN=%s:%u",
				g_pendingAssignment.id.c_str(), MM_ActivePlaylistID.load(), g_pendingAssignment.map.c_str(),
				g_pendingAssignment.mode.c_str(), g_pendingAssignment.publicEndpoint.host.c_str(),
				unsigned(g_pendingAssignment.publicEndpoint.port), g_pendingAssignment.lanEndpoint.host.c_str(),
				unsigned(g_pendingAssignment.lanEndpoint.port));
		}
	}
}
