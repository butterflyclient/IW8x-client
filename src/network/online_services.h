#pragma once
#include "engine/inline_hook.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "network/match_queue.h"
#include "network/backend/tournament_service.h"
#include "network/match_assignment.h"
struct sockaddr_in;

namespace online_services
{
	uint64_t MatchmakingHostId();
	// Atomic because the UI/game and socket worker access these states concurrently.
	extern std::atomic<bool> MM_IsThreadActive, MM_IsSearchingForMatch, MM_IsInMMLobby;
	extern std::atomic<int> MM_ActivePlaylistID;
	extern std::atomic<bool> ForceDLLUpdateRequired;
	hook::Status Install(uintptr_t base);
	bool Configured(); // Embedded defaults, optional INI override; never starts the worker.
	int Component_Stage_Unpack(); // Starts embedded direct-dedicated configuration (optional INI overrides).
	void NET_SendPacket(const char* data, int size);
	void Stop(); // Call outside DllMain; worker is joined before closing its socket.
	void Pump(); // Main thread: dispatch queued session joins.
	// Main thread, before LUIStartup/Main selects a menu. Restore only a finished
	// proxy-owned dedicated route; the ordinary LAN browser never acquires it.
	void OnLuaStartup();
	// Native failed-join cleanup, game thread. Restore frontend flags before its
	// menu rebuild; suppress its error only while a public-to-LAN retry is pending.
	bool OnNativeJoinFailure(std::string_view reason);
	void CaptureSession(const void* session, size_t size);
	// Dedicated game thread publishes readiness. Worker serves only a recent ready
	// snapshot with a genuine PartyHost_StartParty session and matching playlist.
	void PublishDedicated(int playlistId, bool ready, const char* map, const char* mode);
	bool TakeDedicatedRequest(int& playlistId); // main thread only; consumes bounded worker request
	void RejectDedicatedPlaylist(int playlistId);
	// Main-thread host lifecycle: withdraw admission during native teardown, then
	// clear old tickets/session before accepting a fresh automatic allocation.
	void BeginDedicatedReset();
	void FinishDedicatedReset();
	// Main-thread entry points shared by the console and LUI. A successful start
	// means the local request was accepted; it does not assert a master reply/join.
	bool StartMatchmaking(int playlistId);
	bool StartMatchmaking(const std::vector<int>& playlistIds);
	void CancelMatchmaking();
	bool Running(); // Socket worker alive; not evidence of a master connection.
	bool IsSearching();
	match_queue::Packet GetQueueView(); // game-thread UI; empty/invalid outside this search
	bool SilentMatchJoin(); // hide only the proxy-owned native joining-player dialog
	bool RestoringFrontendParty(); // retain online UI while the completed server party is being replaced
	bool InDedicatedMatch(); // main-thread UI presentation scope; does not alter LAN transport
	void SetQueueCard(std::string_view clan, int rank, int prestige, int emblem, int background);

	struct BackendStatus
	{
		bool running;
		int accountStatus; // 0 unknown, 1 allowed, 2 banned; supplied by the master.
		bool updateRequired;
		unsigned packetsReceived;
		// UINT64_MAX means no packet has arrived during this worker's lifetime.
		uint64_t millisecondsSinceLastPacket;
	};

	BackendStatus GetStatus();
	bool HandleCommand(std::string_view command);
	bool SetMatchHost(const sockaddr_in& host);
	bool CanConnectPeer(); // main thread; frontend/profile readiness guard
	// Authenticated social resolution only. Prepare the existing dedicated route,
	// returning a public-first native session for the caller's atomic join HSM.
	bool PrepareSocialGameJoin(int playlist, void* session108);
	// Dedicated HTTP thread: same bounded allocation queue, no client-side join.
	tournament120::Host TournamentHost();
	bool ReserveTournament(const tournament120::Reservation& reservation);
	int TournamentTeam(uint64_t xuid);
	matchmaking::Assignment AllocateNativeMatch(const match_queue::Packet& query, uint32_t peer);
}
