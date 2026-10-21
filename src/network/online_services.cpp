#include <winsock2.h>

#include "engine/addresses.hpp"

#include <ws2tcpip.h>

#include "online_services.h"
#include "client/component/scheduler.hpp"
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
namespace online_services
{
	std::atomic<bool> MM_IsThreadActive{ false }, MM_IsSearchingForMatch{ false }, MM_IsInMMLobby{ false };
	std::atomic<int> MM_ActivePlaylistID{ 0 };
	std::atomic<bool> ForceDLLUpdateRequired{ false };
}

namespace online_services::detail
{
	uintptr_t g_base = 0;

	std::atomic<bool> g_installed{ false }, g_running{ false }, g_stop{ false };

	std::mutex g_lock;

	protocol::Session g_hostSession{}, g_pendingSession{};

	bool g_hasSession = false, g_pending = false;

	// Guarded with g_pending by g_lock. Native selection runs without the socket
	// lock; cancellation/errors invalidate its generation before it can publish.
	uint64_t g_matchmakingGeneration = 0;

	std::vector<int> g_searchPlaylists;

	std::array<bool, 129> g_rejectedPlaylists{};

	std::atomic<ULONGLONG> g_received{ 0 };

	std::atomic<unsigned> g_packets{ 0 }, g_sendErrors{ 0 };

	std::atomic<int> g_banStatus{ 0 };

	char g_build[96] = __DATE__ " " __TIME__;

	bool g_directDedicated = false, g_listening = false;

	// Immutable while worker is running.
	bool g_httpEnabled = false;

	std::wstring g_httpOrigin;

	uint16_t g_httpPort = 27128;

	bool g_httpOriginExplicit = false;

	matchmaking::Assignment g_pendingAssignment;

	protocol::Session g_pendingPublic{}, g_pendingLan{}, g_joinLan{};

	bool g_pendingHasLan = false;

	matchmaking::Connection g_connection;

	// Main thread only.
	std::vector<std::pair<std::wstring, match_queue::Packet>> g_httpCancels;

	std::string g_dedicatedMap, g_dedicatedMode, g_assignmentId;

	int g_dedicatedPlaylist = -1, g_requestedPlaylist = -1;

	std::array<bool, 129> g_unavailablePlaylists{};

#ifndef IW8X_CLIENT_ONLY
	match_queue::Queue g_waitingQueue;
#endif

#ifndef IW8X_CLIENT_ONLY
	tournament120::Reservation g_tournament;
#endif
#ifndef IW8X_CLIENT_ONLY
	std::atomic<std::shared_ptr<const tournament120::Reservation>> g_tournamentView;
#endif
	match_queue::Player g_queuePlayer;

	std::vector<match_queue::Player> g_queueParty;

	// Main-thread native snapshot; guarded by g_lock.
	match_queue::Packet g_queueView;

	ULONGLONG g_queueReplyAt = 0;

	bool g_dedicatedReady = false;

	bool g_dedicatedResetting = false;

	// protected by g_lock, including publication
	ULONGLONG g_dedicatedFrame = 0, g_nextSearch = 0;

	bool ConfigurationPath(wchar_t (&path)[MAX_PATH])
	{
		return native_network::ConfigurationPath(path, MAX_PATH);
	}

	static std::atomic<StartFn> g_start{ nullptr };

	static std::atomic<CallbackFn> g_callback{ nullptr };

	static std::atomic<NetErrorFn> g_netError{ nullptr };

	static std::atomic<SessionFn> g_session{ nullptr };

	template <size_t N>
	bool VerifyBinding(uintptr_t rva, const uint8_t (&expected)[N])
	{
		uint8_t bytes[N]{};
		return safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), bytes, N) && memcmp(bytes, expected, N) == 0;
	}

	int ReadNet(uintptr_t rva)
	{
		int value = -1;
		safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &value, sizeof(value));
		return value;
	}

	std::vector<match_queue::Player> ReadQueueParty(uint64_t ownXuid)
	{
		std::vector<match_queue::Player> members;
		constexpr uintptr_t party = game::addresses::runtime::private_party;
		if (ReadNet(party + 311112) != 1 || ReadNet(party + 311116) != 1) return members;
		// TU19 PartyData uses 200 slots at stride488. Read only registered members
		// (118E210); name/clan/XUID offsets match the native LUI writer1B22140.
		// Keep this on the game thread: the socket worker never reads engine state.
		alignas(8) std::array<uint8_t, 0xB0B8> card{};
		for (unsigned slot = 0; slot < 200 && members.size() + 1 < match_queue::kMaxPlayers; ++slot)
		{
			const auto at = g_base + party + 488 * slot;
			uint8_t status = 0;
			match_queue::Player player;
			char name[36]{}, clan[6]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(at + 2594), &status, 1) || (status != 4 && status != 5) ||
				!safemem::ReadBytes(reinterpret_cast<void*>(at + 2600), &player.xuid, 8) || !player.xuid || player.xuid == ownXuid ||
				!safemem::ReadBytes(reinterpret_cast<void*>(at + 2244), name, sizeof(name)) ||
				!safemem::ReadBytes(reinterpret_cast<void*>(at + 2280), clan, sizeof(clan)))
				continue;
			player.name.assign(name, strnlen_s(name, sizeof(name)));
			player.clan.assign(clan, strnlen_s(clan, sizeof(clan)));
			if (player.name.empty() || !match_queue::Text(player.name, 35) || !match_queue::Text(player.clan, 5) ||
				std::any_of(members.begin(), members.end(),
					[&](const auto& p)
					{
						return p.xuid == player.xuid;
					}))
				continue;
			// Same cached playercard decoder as the stock lobby, not guessed fields
			// in PartyMember. Missing cards get native defaults. TU19 active card=0.
			card.fill(0);
			reinterpret_cast<bool (*)(uint64_t, const char*, void*)>(g_base + game::addresses::runtime::rva_01b97230)(
				player.xuid, name, card.data());
			uint32_t rank = 0, prestige = 0;
			memcpy(&rank, card.data(), 4);
			memcpy(&prestige, card.data() + 4, 4);
			player.rank = uint16_t(std::min<uint64_t>(uint64_t(rank) + prestige, 10000));
			player.prestige = uint16_t(std::min(prestige, 1000u));
			memcpy(&player.emblem, card.data() + 68, 4);
			memcpy(&player.background, card.data() + 72, 4);
			members.push_back(std::move(player));
		}
		return members;
	}

	void StartNetwork(const NetConfig* config, void* listener)
	{
		NetConfig local = *config;
		local.flow = 0;
		local.relayDisabled = 1;
		LOG_INFO("Network", "client NET_Start flow=%d -> OFFLINE(0), relay_disabled=%u; using native LAN transport", config->flow,
			unsigned(local.relayDisabled));
		g_start.load(std::memory_order_acquire)(&local, listener);
	}

	bool NetworkComplete(bool success, int mode, int* nextMode)
	{
		const bool result = g_callback.load(std::memory_order_acquire)(success, mode, nextMode);
		LOG_INFO("Network", "client startup callback success=%d mode=%d state=%d error=%d attempt=%d", success, mode,
			ReadNet(game::kDWNetStateRVA), ReadNet(game::kDWNetErrorRVA), ReadNet(game::kDWNetAttemptsRVA));
		return result;
	}

	void NetworkError(int error)
	{
		LOG_ERR("Network", "client native startup error=%d mode=%d state=%d attempt=%d", error, ReadNet(game::kDWNetModeRVA),
			ReadNet(game::kDWNetStateRVA), ReadNet(game::kDWNetAttemptsRVA));
		g_netError.load(std::memory_order_acquire)(error);
	}

	void SessionCreated(void* session)
	{
		const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
		g_session.load(std::memory_order_acquire)(session);
		if (caller == game::addresses::runtime::private_party_session_call || caller == game::addresses::runtime::party_host_session_call)
			CaptureSession(session, protocol::kSessionBytes);
	}

	void ResetMatchmakingLocked()
	{
		MM_IsThreadActive = false;
		MM_IsSearchingForMatch = false;
		MM_IsInMMLobby = false;
		g_pending = false;
		g_pendingAssignment = {};
		g_pendingHasLan = false;
		g_queueView = {};
		g_queueReplyAt = 0;
		g_searchPlaylists.clear();
		g_rejectedPlaylists.fill(false);
		++g_matchmakingGeneration;
	}

	bool AvailableLocked()
	{
		return g_running.load() && !g_stop.load() && g_banStatus.load() != 2 && !ForceDLLUpdateRequired.load();
	}
}

namespace online_services
{
	using namespace detail;

	void Pump()
	{
		scheduler::pump_game();
		if (!g_installed.load(std::memory_order_acquire) || dedicated::Enabled() || !offlineauth::IsReady()) return;
		MaintainAssignedConnection();
		MaintainJoinRoute();
		MaintainFrontendParty();
		std::wstring chatOrigin;
		{
			std::lock_guard lock(g_lock);
			chatOrigin = g_httpOrigin;
		}
		const auto gameSession = native_matchmaking::Enabled()
			? native_matchmaking::GameSession()
			: (InDedicatedMatch() && g_joinedDedicatedSessionValid
			? matchmaking::Hex(std::string_view(reinterpret_cast<const char*>(g_joinedDedicatedSession.data() + 84), 8))
			: std::string{});
		chat_runtime::ClientPump(chatOrigin, gameSession);
		social_runtime::ClientPump(chatOrigin, gameSession, IsSearching());
		native_matchmaking::Pump();
		voice_runtime::Pump();
		discord120::Pump();
		static ULONGLONG peerAt = 0;
		if (GetTickCount64() >= peerAt)
		{
			party_transport::Retry();
			if (MM_IsThreadActive)
			{
				auto members = ReadQueueParty(offlineauth::GetIdentity().xuid);
				std::lock_guard lock(g_lock);
				g_queueParty = std::move(members);
			}
			// Read the actual current private-party SessionData, not an earlier
			// NET_CreateSession callback (which may belong to a different party).
			constexpr uintptr_t party = 0xC796480;
			protocol::Session info{};
			uintptr_t data = 0;
			bool publish = reinterpret_cast<bool (*)()>(g_base + game::kIsInFrontEndRVA)() && ReadNet(party + 311112) == 1 &&
				ReadNet(party + 311116) == 1 && ReadNet(kPartyJoinStateRVA) == 0;
			publish = publish && safemem::ReadBytes(reinterpret_cast<void*>(g_base + party), &data, sizeof(data)) && data &&
				safemem::ReadBytes(reinterpret_cast<void*>(data + 4), info.data(), info.size()) &&
				std::any_of(info.begin(), info.end(),
					[](uint8_t byte)
					{
						return byte != 0;
					});
			peers::Publish(offlineauth::GetIdentity().xuid, publish ? peers::wire::State::Party : peers::wire::State::Unavailable, 0, info);
			peerAt = GetTickCount64() + 500;
		}
		peers::Join peer;
		if (peers::TakeJoin(peer))
		{
			if (!CanConnectPeer()) return;
			auto endpoint = peer.endpoint;
			if (peer.packet.state == peers::wire::State::Party)
			{
				if (peer.packet.ip) endpoint.sin_addr.s_addr = peer.packet.ip;
				endpoint.sin_port = htons(peer.packet.port ? peer.packet.port : 27126);
			}
			if (!SetMatchHost(endpoint)) return;
			if (peer.packet.state == peers::wire::State::Idle)
			{
				OP_INFO("Dedicated host selected; choose a playlist to start matchmaking");
				return;
			}
			if (peer.packet.state == peers::wire::State::Party)
			{
				const bool dispatched = clientconsole::JoinSession(protocol::SessionHex(peer.packet.session), true, peer.packet.xuid);
				OP_INFO("Native party join dispatched=%d; check the party roster for completion", dispatched);
				return;
			}
			if (!playlists::SelectForMatchmaking(peer.packet.playlist))
			{
				OP_WARN("Connect requires the Multiplayer/Warzone frontend and a loaded playlist");
				return;
			}
			MM_ActivePlaylistID = peer.packet.playlist;
			// Direct connect skips the playlist button/search callback. Capture the
			// real menu party before native admission replaces it with the server
			// party, so the stock matchmaking grid has names during map preload.
			const auto identity = offlineauth::GetIdentity();
			auto members = ReadQueueParty(identity.xuid);
			{
				std::lock_guard lock(g_lock);
				g_queuePlayer.xuid = identity.xuid;
				g_queuePlayer.name = identity.name;
				g_queueParty = std::move(members);
				g_queueView = {};
				g_queueView.playlist = peer.packet.playlist;
				g_queueView.phase = match_queue::Phase::Ready;
				g_queueView.seconds = 0;
			}
			const bool accepted = PrepareJoinRoute() && clientconsole::JoinSession(protocol::SessionHex(peer.packet.session), false);
			if (!accepted) RestoreJoinRoute();
			MM_IsInMMLobby = accepted;
			OP_INFO("Native dedicated join dispatched=%d playlist=%u", accepted, unsigned(peer.packet.playlist));
			return;
		}
			protocol::Session session{};
			std::string assignedMap, assignedMode;
		uint64_t generation = 0;
		int playlist = 0;
		bool assigned = false, hasLan = false;
		protocol::Session lan{};
		{
			std::lock_guard<std::mutex> lock(g_lock);
			if (!g_pending || !MM_IsThreadActive.load() || !AvailableLocked()) return;
			session = g_pendingSession;
			assigned = !g_pendingAssignment.id.empty();
			if (assigned)
			{
				session = g_pendingPublic;
				lan = g_pendingLan;
				hasLan = g_pendingHasLan;
					assignedMap = g_pendingAssignment.map;
					assignedMode = g_pendingAssignment.mode;
			}
			generation = g_matchmakingGeneration;
			playlist = MM_ActivePlaylistID.load();
		}
		if (!playlists::SelectForMatchmaking(playlist))
		{
			{
				std::lock_guard<std::mutex> lock(g_lock);
				if (generation == g_matchmakingGeneration) ResetMatchmakingLocked();
			}
			OP_ERR("Master join cancelled: native playlist state is unavailable; use playlist_status");
			return;
		}
		{
			std::lock_guard<std::mutex> lock(g_lock);
			if (generation != g_matchmakingGeneration || !g_pending || !AvailableLocked()) return;
			g_pending = false;
			MM_IsSearchingForMatch = false;
			MM_IsThreadActive = false;
		}
		// No socket lock across engine calls: a native session callback can re-enter
		// CaptureSession. Cancellation uses the native temporary-join abort path.
		if (assigned)
		{
			g_connection.Start(GetTickCount64(), joindiag::TransportFailureSequence(), hasLan);
			g_joinLan = lan;
			LOG_INFO("Connect", "attempting assigned public endpoint; LAN candidates withheld until native failure");
		}
			const bool accepted = PrepareJoinRoute(assignedMap, assignedMode) &&
				clientconsole::JoinSession(protocol::SessionHex(session), false);
		if (!accepted) g_connection.Cancel();
		if (!accepted) RestoreJoinRoute();
		{
			std::lock_guard<std::mutex> lock(g_lock);
			if (generation == g_matchmakingGeneration) MM_IsInMMLobby = accepted;
		}
		LOG_INFO("OnlineServices", "session join dispatched=%d; native Join/EngineNet logs report its outcome", accepted);
	}

	bool Running()
	{
		return g_running.load(std::memory_order_acquire) && !g_stop.load();
	}

	bool Configured()
	{
		wchar_t path[MAX_PATH]{};
		return ConfigurationPath(path) && GetPrivateProfileIntW(L"online_services", L"enabled", 1, path) != 0;
	}

	int Component_Stage_Unpack()
	{
		if (g_thread) return g_running.load() ? 0 : -1;
		wchar_t path[MAX_PATH]{};
		if (!ConfigurationPath(path)) return -1;
		if (!GetPrivateProfileIntW(L"online_services", L"enabled", 1, path))
		{
			LOG_INFO("OnlineServices", "matchmaking disabled by optional online configuration");
			return 0;
		}
		wchar_t buildW[96]{};
		wchar_t mode[32]{};
		GetPrivateProfileStringW(L"online_services", L"matchmaking", deployment::kMatchmaking, mode, 32, path);
		if (_wcsicmp(mode, L"dedicated") && _wcsicmp(mode, L"legacy") && _wcsicmp(mode, L"native"))
		{
			LOG_ERR("OnlineServices", "matchmaking must be legacy, dedicated or native");
			return -1;
		}

		native_matchmaking::Configure(_wcsicmp(mode, L"native") == 0);
		g_directDedicated = _wcsicmp(mode, L"legacy") != 0;
		#ifdef IW8X_CLIENT_ONLY
		g_listening = false;
#else
		g_listening = g_directDedicated && dedicated ::Enabled() && !backend_agent ::Enabled();
#endif
		wchar_t transport[16]{};
		GetPrivateProfileStringW(L"online_services", L"transport", L"http", transport, 16, path);
		if (_wcsicmp(transport, L"http") && _wcsicmp(transport, L"udp")) return -1;
		g_httpEnabled = g_directDedicated && _wcsicmp(transport, L"http") == 0;
		GetPrivateProfileStringW(L"online_services", L"build", L"", buildW, 96, path);
		strcpy_s(g_build, __DATE__ " " __TIME__);
		if (buildW[0])
		{
			char converted[384]{};
			if (!WideCharToMultiByte(CP_UTF8, 0, buildW, -1, converted, sizeof(converted), nullptr, nullptr) ||
				strlen(converted) >= sizeof(g_build))
			{
				LOG_ERR("OnlineServices", "configured build string is invalid or too long");
				return -1;
			}
			strcpy_s(g_build, converted);
		}
		wchar_t hostW[256]{};
		GetPrivateProfileStringW(L"online_services", L"host", deployment::kBackendHost, hostW, 256, path);
		char host[768]{};
		if (!WideCharToMultiByte(CP_UTF8, 0, hostW, -1, host, sizeof(host), nullptr, nullptr)) return -1;
		const UINT port = GetPrivateProfileIntW(L"online_services", L"port", 27126, path);
		g_server = {};
		g_server.sin_family = AF_INET;
		if (!port || port > 65535)
		{
			LOG_ERR("OnlineServices", "invalid configured matchmaking port");
			return -1;
		}
		g_server.sin_port = htons(static_cast<u_short>(port));
		const UINT httpPort = GetPrivateProfileIntW(L"online_services", L"http_port", 27128, path);
		if (!httpPort || httpPort > 65535) return -1;
		g_httpPort = uint16_t(httpPort);
		wchar_t origin[2048]{};
		const std::wstring defaultOrigin = L"http://" + std::wstring(hostW) + L":" + std::to_wstring(httpPort);
		GetPrivateProfileStringW(L"online_services", L"http_url", L"", origin, 2048, path);
		g_httpOriginExplicit = origin[0] != 0;
		g_httpOrigin = g_httpOriginExplicit ? std::wstring(origin) : defaultOrigin;
		WSADATA wsa{};
		const int startup = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (startup)
		{
			LOG_ERR("OnlineServices", "WSAStartup failed: %d", startup);
			return -1;
		}
		if (!g_listening && !native_network::ResolveIPv4(host, g_server.sin_addr))
		{
			LOG_ERR("OnlineServices", "cannot resolve matchmaking hostname/IPv4: %s", host);
			WSACleanup();
			return -1;
		}
		g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (g_socket == INVALID_SOCKET)
		{
			LOG_ERR("OnlineServices", "socket failed: WSA=%d", WSAGetLastError());
			WSACleanup();
			return -1;
		}
#ifndef IW8X_CLIENT_ONLY
		if (g_listening)
		{
			const BOOL exclusive = TRUE;
			if (setsockopt(g_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) ==
					SOCKET_ERROR ||
				bind(g_socket, reinterpret_cast<const sockaddr*>(&g_server), sizeof(g_server)) == SOCKET_ERROR)
			{
				LOG_ERR("OnlineServices", "dedicated matchmaking bind failed on UDP %u: WSA=%d", port, WSAGetLastError());
				closesocket(g_socket);
				g_socket = INVALID_SOCKET;
				WSACleanup();
				return -1;
			}
		}
#endif

		u_long nonblocking = 1;
		if (ioctlsocket(g_socket, FIONBIO, &nonblocking) == SOCKET_ERROR)
		{
			LOG_ERR("OnlineServices", "nonblocking socket setup failed: WSA=%d", WSAGetLastError());
			closesocket(g_socket);
			g_socket = INVALID_SOCKET;
			WSACleanup();
			return -1;
		}
		g_received = 0;
		g_packets = 0;
		g_sendErrors = 0;
		g_banStatus = 0;
		ForceDLLUpdateRequired = false;
		{
			std::lock_guard<std::mutex> lock(g_lock);
			g_matchmakingGeneration = (GetTickCount64() << 16) | (GetCurrentProcessId() & 0xFFFF);
			g_nextSearch = 0;
		}
		// A standalone backend owns matchmaking listeners, but this process is still a dedicated host.
		peers::Initialize(dedicated::Enabled(), g_server);
		g_stop = false;
		g_running = true;
#ifndef IW8X_CLIENT_ONLY
		if (g_httpEnabled && g_listening && !match_http::Start(uint16_t(httpPort), HttpRequest))
		{
			LOG_ERR("Matchmaking", "HTTP listener bind failed TCP %u WSA=%d", httpPort, WSAGetLastError());
			g_running = false;
			peers::Shutdown();
			closesocket(g_socket);
			g_socket = INVALID_SOCKET;
			WSACleanup();
			return -1;
		}
#endif


#ifndef IW8X_CLIENT_ONLY
		if (backend_agent::Enabled() && !backend_agent::Start(g_httpOrigin))
		{
			g_running = false;
			peers::Shutdown();
			closesocket(g_socket);
			g_socket = INVALID_SOCKET;
			WSACleanup();
			return -1;
		}
#endif

		g_thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
		if (!g_thread)
		{
			g_running = false;
	#ifndef IW8X_CLIENT_ONLY
		match_http::Stop();
#endif
			peers::Shutdown();
			closesocket(g_socket);
			g_socket = INVALID_SOCKET;
			WSACleanup();
			return -1;
		}
		if (g_httpEnabled)

		{
			publisher_variables::Configure(g_httpOrigin);
			playlist_backend::Configure(g_listening, g_httpOrigin);
			frontend_backend::Configure(dedicated::Enabled(), g_httpOrigin);
			LOG_INFO("Matchmaking", "%s HTTP API %ls; registration is separate from search", g_listening ? "dedicated" : "client",
				g_httpOrigin.c_str());
		}
		LOG_INFO("OnlineServices", "%s started at %s:%u protocol=%s", g_listening ? "dedicated matchmaking listener" : "matchmaking client",
			g_listening ? "0.0.0.0" : host, port, g_directDedicated ? "direct-dedicated-v1" : "legacy master");
		return 0;
	}

	void Stop()
	{

#ifndef IW8X_CLIENT_ONLY
		backend_agent::Stop();
#endif
		discord120::Stop();
		party_transport::Stop();
		g_stop = true;
		ResetMatchmaking();
		if (!g_thread) return;
		if (WaitForSingleObject(g_thread, 2000) != WAIT_OBJECT_0)
		{
			LOG_WARN("OnlineServices", "socket worker has not stopped yet; retry online_stop to finish cleanup");
			return;
		}
		CloseHandle(g_thread);
		g_thread = nullptr;
		chat_runtime::Shutdown();
		frontend_backend::Shutdown();
		social_runtime::Shutdown();
		publisher_variables::Shutdown();
		playlist_backend::Shutdown();
#ifndef IW8X_CLIENT_ONLY
		match_http::Stop();
#endif
		peers::Shutdown();
		std::lock_guard<std::mutex> lock(g_lock);
		closesocket(g_socket);
		g_socket = INVALID_SOCKET;
		WSACleanup();
		ResetMatchmakingLocked();
	}

	hook::Status Install(uintptr_t base)
	{
		if (g_installed.load(std::memory_order_acquire)) return hook::Status::Installed;
		g_base = base;
		if (!VerifyBinding(kPartyJoinAbortRVA, kPartyJoinAbortBytes) || !VerifyBinding(kStartPrivatePartyRVA, kStartPrivatePartyBytes) ||
			!VerifyBinding(kPartySleepRVA, kPartySleepBytes))
			return hook::Status::NotReady;
		uint8_t setter[sizeof(game::kDvarSetBoolInternalPrologue)]{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + game::kDvarSetBoolInternalRVA), setter, sizeof(setter)) ||
			memcmp(setter, game::kDvarSetBoolInternalPrologue, sizeof(setter)))
			return hook::Status::NotReady;
		uint8_t frontend[sizeof(game::kIsInFrontEndPrologue)]{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + game::kIsInFrontEndRVA), frontend, sizeof(frontend)) ||
			memcmp(frontend, game::kIsInFrontEndPrologue, sizeof(frontend)))
			return hook::Status::NotReady;
		const auto networkStatus = native_network::Install(base);
		if (networkStatus != hook::Status::Installed) return networkStatus;

		const auto chatStatus = chat_runtime::Install(base, dedicated::Enabled());
		if (chatStatus != hook::Status::Installed) return chatStatus;
		const auto socialStatus = social_runtime::Install(base, dedicated::Enabled());
		if (socialStatus != hook::Status::Installed) return socialStatus;
		const auto nativeMatchStatus = native_matchmaking::Install(base, dedicated::Enabled());
		if (nativeMatchStatus != hook::Status::Installed) return nativeMatchStatus;
		const auto publisherStatus = publisher_variables::Install(base, dedicated::Enabled());
		if (publisherStatus != hook::Status::Installed) return publisherStatus;
		const auto frontendStatus = frontend_backend::Install(base, dedicated::Enabled());
		if (frontendStatus != hook::Status::Installed) return frontendStatus;
		const auto voiceStatus = voice_runtime::Install(base, dedicated::Enabled());
		if (voiceStatus != hook::Status::Installed) return voiceStatus;
		// Headless hosting installs before Component_Stage_Unpack. Choose its party
		// type now so native matchmaking creates a real GameParty from the outset.
		wchar_t configPath[MAX_PATH]{}, matchMode[32]{};
		if (ConfigurationPath(configPath))
		{
			GetPrivateProfileStringW(L"online_services", L"matchmaking", deployment::kMatchmaking, matchMode, 32, configPath);
			native_matchmaking::Configure(Configured() && _wcsicmp(matchMode, L"native") == 0);
		}
		if (dedicated::Enabled() || Configured())
		{
			if (!VerifyBinding(game::kComGameModeSupportsFeatureRVA, game::kComGameModeSupportsFeaturePrologue) ||
				!VerifyBinding(game::kIsFrontEndRVA, game::kIsFrontEndPrologue) ||
				!VerifyBinding(game::kComGameModeGetActiveGameModeRVA, game::kComGameModeGetActiveGameModePrologue))
				return hook::Status::NotReady;
			const auto status = hook::Install(reinterpret_cast<void*>(base + game::kBGAreBotsAllowedRVA), &AreBotsAllowed,
				game::kBGAreBotsAllowedPrologue, sizeof(game::kBGAreBotsAllowedPrologue), g_areBotsAllowed);
			if (status != hook::Status::Installed) return status;
			const auto enabled = hook::Install(reinterpret_cast<void*>(base + game::kBGBotsEnabledRVA), &BotsEnabled,
				game::kBGBotsEnabledPrologue, sizeof(game::kBGBotsEnabledPrologue), g_botsEnabled);
			if (enabled != hook::Status::Installed) return enabled;
			LOG_INFO("OnlineServices", "BG_AreBotsAllowed -> true (Replay RVA 0x11095E0)");
			LOG_INFO("OnlineServices", "bot AI enabled with native feature/frontend/mode guards (Replay RVA 0x1109560)");
		}
		// Dedicated mode already owns these exact detours. The client must also use
		// native OFFLINE flow; OFFLINE_PRIMARY otherwise promotes into online startup.
		if (!dedicated::Enabled())
		{
						#define NET_HOOK(rva, fn, bytes, original) \
        { auto status = hook::Install(reinterpret_cast<void*>(base + rva), fn, bytes, sizeof(bytes), original); \
          if (status != hook::Status::Installed) return status; }
			NET_HOOK(game::kNETStartRVA, StartNetwork, game::kNETStartPrologue, g_start);
			NET_HOOK(game::kNetInitDemonwareCallbackRVA, NetworkComplete, game::kNetInitDemonwareCallbackPrologue, g_callback);
			NET_HOOK(game::kDWNetSetErrorRVA, NetworkError, game::kDWNetSetErrorPrologue, g_netError);
			NET_HOOK(game::kNETCreateSessionRVA, SessionCreated, game::kNETCreateSessionPrologue, g_session);
			NET_HOOK(game::addresses::runtime::rva_01607550, JoinDialogState, kJoinDialogBytes, g_joinDialogState);
			NET_HOOK(game::addresses::runtime::rva_00f21db0, PendingDialogState, kPendingDialogBytes, g_pendingDialogState);
			LOG_INFO("OnlineServices", "dedicated joins suppress native invite-progress presentation before LUI/input capture");
						#undef NET_HOOK
		}
		g_installed = true;
		return hook::Status::Installed;
	}
}
