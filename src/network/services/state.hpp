#pragma once
#include "network/backend/tournament_backend.h"

#include <memory>

#include "network/services/types.hpp"
namespace online_services::detail
{
	extern tournament120::Reservation g_tournament;
	extern std::atomic<std::shared_ptr<const tournament120::Reservation>> g_tournamentView;
	extern uintptr_t g_base;

	extern std::atomic<bool> g_installed, g_running, g_stop;

	extern std::mutex g_lock;

	extern protocol::Session g_hostSession, g_pendingSession;

	extern bool g_hasSession, g_pending;

	extern uint64_t g_matchmakingGeneration;

	extern std::vector<int> g_searchPlaylists;

	extern std::array<bool, 129> g_rejectedPlaylists;

	extern std::atomic<ULONGLONG> g_received;

	extern std::atomic<unsigned> g_packets, g_sendErrors;

	extern std::atomic<int> g_banStatus;

	extern char g_build[96];

	extern bool g_directDedicated, g_listening;

	extern bool g_httpEnabled;

	extern std::wstring g_httpOrigin;

	extern uint16_t g_httpPort;

	extern bool g_httpOriginExplicit;

	extern matchmaking::Assignment g_pendingAssignment;

	extern protocol::Session g_pendingPublic, g_pendingLan, g_joinLan;

	extern bool g_pendingHasLan;

	extern matchmaking::Connection g_connection;

	extern std::vector<std::pair<std::wstring, match_queue::Packet>> g_httpCancels;

	extern std::string g_dedicatedMap, g_dedicatedMode, g_assignmentId;

	extern int g_dedicatedPlaylist, g_requestedPlaylist;

	extern std::array<bool, 129> g_unavailablePlaylists;

	extern match_queue::Queue g_waitingQueue;

	extern match_queue::Player g_queuePlayer;

	extern std::vector<match_queue::Player> g_queueParty;

	extern match_queue::Packet g_queueView;

	extern ULONGLONG g_queueReplyAt;

	extern bool g_dedicatedReady;

	extern bool g_dedicatedResetting;

	extern ULONGLONG g_dedicatedFrame, g_nextSearch;

	bool ConfigurationPath(wchar_t (&path)[MAX_PATH]);

	int ReadNet(uintptr_t rva);

	std::vector<match_queue::Player> ReadQueueParty(uint64_t ownXuid);

	void StartNetwork(const NetConfig* config, void* listener);

	bool NetworkComplete(bool success, int mode, int* nextMode);

	void NetworkError(int error);

	void SessionCreated(void* session);

	void ResetMatchmakingLocked();

	bool AvailableLocked();
}
