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
	SOCKET g_socket = INVALID_SOCKET;

	HANDLE g_thread = nullptr;

	sockaddr_in g_server{};

	bool FatalSocketError(int error)
	{
		return error == WSAENOTSOCK || error == WSAENETDOWN || error == WSAEINVAL || error == WSAESHUTDOWN;
	}

	bool SendPacketLocked(const char* data, int size)
	{
		if (!data || size <= 0 || size > 2048 || g_socket == INVALID_SOCKET || g_stop.load()) return false;
		if (sendto(g_socket, data, size, 0, reinterpret_cast<const sockaddr*>(&g_server), sizeof(g_server)) != SOCKET_ERROR) return true;
		const int error = WSAGetLastError();
		if (++g_sendErrors <= 16)
			LOG_ERR("OnlineServices", "sendto failed: WSA=%d type=0x%02X size=%d", error, unsigned(uint8_t(data[0])), size);
		if (FatalSocketError(error))
		{
			g_stop = true;
			ResetMatchmakingLocked();
		}
		return false;
	}

	void ParsePacket(std::string_view bytes)
	{
		if (g_directDedicated && !bytes.empty() && uint8_t(bytes[0]) == match_queue::kReply)
		{
			const auto reply = match_queue::Parse(bytes);
			std::lock_guard lock(g_lock);
			if (!reply.valid || !MM_IsThreadActive || g_pending || !AvailableLocked() || reply.request != g_matchmakingGeneration ||
				std::find(g_searchPlaylists.begin(), g_searchPlaylists.end(), reply.playlist) == g_searchPlaylists.end())
				return;
			if (reply.phase == match_queue::Phase::Unavailable || reply.phase == match_queue::Phase::WrongPlaylist)
			{
				OP_WARN("No selected playlist is available on this dedicated host");
				ResetMatchmakingLocked();
				return;
			}
			g_queueView = reply;
			g_queueReplyAt = GetTickCount64();
			if (reply.phase == match_queue::Phase::Ready)
			{
				g_pendingSession = reply.session;
				g_pending = true;
				MM_ActivePlaylistID = reply.playlist;
				MM_IsSearchingForMatch = false;
				LOG_INFO("OnlineServices", "waiting lobby ready: playlist=%d players=%zu; native join queued", reply.playlist,
					reply.players.size());
			}
			return;
		}
		if (g_directDedicated && !bytes.empty() && uint8_t(bytes[0]) == protocol::kDedicatedReply)
		{
			const auto reply = protocol::ParseDedicated(bytes, true);
			std::lock_guard<std::mutex> lock(g_lock);
			if (!MM_IsThreadActive.load() || g_pending || !AvailableLocked() ||
				!protocol::MatchesSearch(reply, g_searchPlaylists, g_matchmakingGeneration))
				return;
			if (reply.state == protocol::DedicatedState::Ready)
			{
				g_pendingSession = reply.session;
				g_pending = true;
				MM_ActivePlaylistID = reply.playlist;
				MM_IsSearchingForMatch = false;
				LOG_INFO("OnlineServices", "dedicated matched playlist=%d request=%llu; native session join queued", reply.playlist,
					static_cast<unsigned long long>(reply.request));
			}
			else if (reply.state == protocol::DedicatedState::WrongPlaylist || reply.state == protocol::DedicatedState::NoPlaylist)
			{
				g_rejectedPlaylists[reply.playlist] = true;
				if (std::all_of(g_searchPlaylists.begin(), g_searchPlaylists.end(),
						[](int id)
						{
							return g_rejectedPlaylists[id];
						}))
				{
					OP_ERR("Dedicated server does not host any selected playlist; select its playlist/filter or start the matching .cmd");
					ResetMatchmakingLocked();
				}
			}
			return;
		}
		const auto packet = protocol::Parse(bytes);
		switch (packet.kind)
		{
		case protocol::Kind::BanStatus: {
			std::lock_guard<std::mutex> lock(g_lock);
			const bool changed = g_banStatus != packet.value;
			g_banStatus = packet.value;
			if (changed || console::TraceEnabled())
				LOG_INFO("OnlineServices", "master account status=%u (1=allowed, 2=banned)", unsigned(packet.value));
			if (packet.value == 2) ResetMatchmakingLocked();
			break;
		}
		case protocol::Kind::Searching: {
			std::lock_guard<std::mutex> lock(g_lock);
			if (MM_IsThreadActive.load() && !MM_IsInMMLobby.load() && !g_pending)
			{
				MM_IsSearchingForMatch = true;
				LOG_INFO("OnlineServices", "master acknowledged matchmaking search, playlist=%d", MM_ActivePlaylistID.load());
			}
			break;
		}
		case protocol::Kind::RequestSession: {
			if (g_directDedicated) break;
			std::lock_guard<std::mutex> lock(g_lock);
			if (!dedicated::Enabled() && (!MM_IsThreadActive.load() || !AvailableLocked())) break;
			if (!g_hasSession)
			{
				LOG_WARN("OnlineServices", "master requested session before PartyHost created one");
				break;
			}
			const auto response = std::string(1, char(0x12)) + protocol::SessionHex(g_hostSession);
			if (!SendPacketLocked(response.data(), static_cast<int>(response.size()))) break;
			MM_IsInMMLobby = true;
			MM_IsSearchingForMatch = false;
			LOG_INFO("OnlineServices", "sent current PartyHost XSESSION_INFO to configured master");
			break;
		}
		case protocol::Kind::JoinSession: {
			if (g_directDedicated) break; // Direct mode accepts only correlated v1 replies.
			std::lock_guard<std::mutex> lock(g_lock);
			if (!dedicated::Enabled() && MM_IsThreadActive.load() && !MM_IsInMMLobby.load() && AvailableLocked())
			{
				if (!g_pending)
				{
					g_pendingSession = packet.session;
					g_pending = true;
					MM_IsSearchingForMatch = false;
					LOG_INFO("OnlineServices", "master supplied a session; join queued for the game thread");
				}
			}
			break;
		}
		case protocol::Kind::PlayerOnline:
			LOG_INFO("OnlineServices", "player online: %s", packet.text.c_str());
			break;
		case protocol::Kind::Error:
			LOG_ERR("OnlineServices", "master rejected request: %s", packet.text.c_str());
			ResetMatchmaking();
			break;
		case protocol::Kind::UpdateRequired: {
			std::lock_guard<std::mutex> lock(g_lock);
			ForceDLLUpdateRequired = true;
			LOG_ERR("OnlineServices", "master requires a different DLL build; matchmaking stopped");
			ResetMatchmakingLocked();
			break;
		}
		default:
			LOG_WARN("OnlineServices", "ignored malformed/unknown master packet: type=0x%02X size=%zu",
				bytes.empty() ? 0 : unsigned(uint8_t(bytes.front())), bytes.size());
			break;
		}
	}

	DWORD WINAPI Worker(void*)
	{
		const auto login = std::string(1, char(0xF1)) + g_build;
		if (!g_listening) NET_SendPacket(login.data(), static_cast<int>(login.size()));
		ULONGLONG pingAt = 0, warningAt = GetTickCount64();
		ULONGLONG loginAt = warningAt + 10000;
		while (!g_stop.load(std::memory_order_acquire))
		{
			const auto now = GetTickCount64();
			peers::Tick(g_socket);
			if (g_httpEnabled && !g_listening) PollHttp();
			// UDP startup may precede the master. Retry the existing handshake until
			// it answers; opening a socket is not proof of account/service readiness.
			if (!g_listening && !g_banStatus.load() && !ForceDLLUpdateRequired.load() && now >= loginAt)
			{
				NET_SendPacket(login.data(), static_cast<int>(login.size()));
				loginAt = now + 10000;
			}
			if (!g_listening && now >= pingAt)
			{
				const char ping = 0;
				NET_SendPacket(&ping, 1);
				pingAt = now + 5000;
			}
			{
				std::lock_guard<std::mutex> lock(g_lock);
#ifndef IW8X_CLIENT_ONLY
				if (g_listening && dedicated::AutoPlaylist() && !g_dedicatedResetting)
				{
					g_waitingQueue.Expire(now);
					if (g_dedicatedPlaylist < 0 && g_waitingQueue.Tickets().empty()) g_requestedPlaylist = -1;
					if (g_dedicatedPlaylist < 0 && g_requestedPlaylist < 0 && g_waitingQueue.ShouldStart(now) &&
						(!g_tournament.id || g_waitingQueue.Tickets().size() == 4))
					{
						g_requestedPlaylist = g_waitingQueue.Playlist();
						OP_INFO("Starting queued match | playlist=%d players=%zu", g_requestedPlaylist, g_waitingQueue.Tickets().size());
					}
				}
#endif

				if (!g_httpEnabled && !g_listening && MM_IsThreadActive.load() && !MM_IsInMMLobby.load() && !g_pending &&
					now >= g_nextSearch)
				{
					if (g_directDedicated)
					{
						// Quick Play searches every enabled filter. Replies retain
						// ID+generation correlation; the first matching session wins.
						match_queue::Packet query;
						query.request = g_matchmakingGeneration;
						query.player = g_queuePlayer;
						query.playlists = g_searchPlaylists;
						query.players = g_queueParty;
						const auto request = match_queue::Encode(query);
						SendPacketLocked(request.data(), static_cast<int>(request.size()));
					}
					else
					{
						const char heartbeat[] = { char(0x10), char(MM_ActivePlaylistID.load()) };
						SendPacketLocked(heartbeat, sizeof(heartbeat));
					}
					g_nextSearch = now + (g_directDedicated ? 1000 : 10000);
				}
			}
			if (!g_listening && now - warningAt >= 30000)
			{
				const auto received = g_received.load();
				if (!received || now - received >= 30000)
					LOG_WARN("OnlineServices", "no master reply for 30 seconds; packets_received=%u send_errors=%u", g_packets.load(),
						g_sendErrors.load());
				warningAt = now;
			}
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(g_socket, &readSet);
			timeval timeout{ 0, 200000 };
			const int selected = select(0, &readSet, nullptr, nullptr, &timeout);
			if (selected == SOCKET_ERROR)
			{
				LOG_ERR("OnlineServices", "select failed: WSA=%d", WSAGetLastError());
				break;
			}
			if (!selected) continue;
			char bytes[2049]{};
			sockaddr_in from{};
			int fromSize = sizeof(from);
			const int count = recvfrom(g_socket, bytes, sizeof(bytes), 0, reinterpret_cast<sockaddr*>(&from), &fromSize);
			if (count == SOCKET_ERROR)
			{
				const int error = WSAGetLastError();
				if (error == WSAEWOULDBLOCK) continue;
				LOG_WARN("OnlineServices", "recvfrom failed: WSA=%d", error);
				if (FatalSocketError(error)) break;
				continue;
			}
			if (peers::Receive(g_socket, std::string_view(bytes, count), from)) continue;
#ifndef IW8X_CLIENT_ONLY
			if (g_listening && count > 0 && (uint8_t(bytes[0]) == match_queue::kSearch || uint8_t(bytes[0]) == match_queue::kCancel))
			{
				const auto query = match_queue::Parse(std::string_view(bytes, count));
				if (!query.valid) continue;
				match_queue::Packet reply;
				{
					std::lock_guard lock(g_lock);
					reply = ProcessQueueLocked(query, from);
				}
				if (query.kind == match_queue::kCancel) continue;
				const auto response = match_queue::Encode(reply);
				sendto(g_socket, response.data(), static_cast<int>(response.size()), 0, reinterpret_cast<const sockaddr*>(&from), fromSize);
				++g_packets;
				g_received = now;
				continue;
			}
#endif

#ifndef IW8X_CLIENT_ONLY
			if (g_listening)
			{
				// LAN listener answers only discovery/login or a bounded v1 search.
				// It never consumes remote sessions or executes client commands.
				std::string response;
				const auto request = protocol::ParseDedicated(std::string_view(bytes, count), false);
				{
					std::lock_guard<std::mutex> lock(g_lock);
					if ((count == 1 && bytes[0] == 0) || (count > 1 && count <= 96 && uint8_t(bytes[0]) == 0xF1))
						response.assign("\x01\x01", 2);
					else if (request.valid)
					{
						auto state = protocol::DedicatedState::Waiting;
						if (g_dedicatedResetting)
							state = protocol::DedicatedState::Waiting;
						else if (g_unavailablePlaylists[request.playlist])
							state = protocol::DedicatedState::NoPlaylist;
						else if (g_dedicatedPlaylist < 0 && dedicated::AutoPlaylist())
						{
							// Legacy clients cannot enter the identity-aware waiting
							// roster. Keep waiting until an updated client allocates it.
						}
						else if (g_dedicatedPlaylist < 0)
							state = protocol::DedicatedState::NoPlaylist;
						else if (g_dedicatedPlaylist != request.playlist)
							state = protocol::DedicatedState::WrongPlaylist;
						else if (g_hasSession && g_dedicatedReady && GetTickCount64() - g_dedicatedFrame < 5000)
							state = protocol::DedicatedState::Ready;
						response = protocol::DedicatedReply(request, state, g_hostSession);
						if (state == protocol::DedicatedState::Ready)
							LOG_INFO("OnlineServices", "serving dedicated playlist=%d request=%llu with current PartyHost session",
								request.playlist, static_cast<unsigned long long>(request.request));
					}
				}
				if (!response.empty())
				{
					sendto(g_socket, response.data(), static_cast<int>(response.size()), 0, reinterpret_cast<const sockaddr*>(&from),
						fromSize);
					++g_packets;
					g_received = GetTickCount64();
				}
				continue;
			}
#endif

			// Only the configured endpoint may supply a session or server status.
			{
				std::lock_guard<std::mutex> lock(g_lock);
				if (from.sin_family != AF_INET || from.sin_addr.s_addr != g_server.sin_addr.s_addr || from.sin_port != g_server.sin_port)
					continue;
				++g_packets;
				g_received = GetTickCount64();
			}
			// HTTP searches cannot be completed by a stale UDP assignment. Login
			// status and peer discovery still have their original bounded paths.
			if (g_httpEnabled && count > 0 &&
				(uint8_t(bytes[0]) == match_queue::kReply || uint8_t(bytes[0]) == protocol::kDedicatedReply || uint8_t(bytes[0]) == 0x13))
				continue;
			ParsePacket(std::string_view(bytes, count));
		}
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_running = false;
			ResetMatchmakingLocked();
		}
		return 0;
	}
}

namespace online_services
{
	using namespace detail;

	void NET_SendPacket(const char* data, int size)
	{
		std::lock_guard<std::mutex> lock(g_lock);
		SendPacketLocked(data, size);
	}
}
