#pragma once
#include "engine/addresses.hpp"

#include "network/services/types.hpp"
#include <string_view>
namespace online_services::detail
{
	extern protocol::Session g_joinedDedicatedSession;
	extern bool g_joinedDedicatedSessionValid;
	extern bool g_lanJoin, g_lanSawGame;

	inline constexpr uintptr_t kPartyJoinStateRVA = game::addresses::runtime::join_join_info;

	// Replay PartyJoinInfo; see join diagnostics/1608270.
	inline constexpr uintptr_t kPartyJoinAbortRVA = game::addresses::runtime::join_route_party_join_abort;

	inline constexpr uint8_t kPartyJoinAbortBytes[] = { 0x48, 0x89, 0x6C, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x01, 0x0F, 0xB6,
		0xEA };

	inline constexpr uintptr_t kStartPrivatePartyRVA = game::addresses::runtime::join_route_start_private_party;

	inline constexpr uint8_t kStartPrivatePartyBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83,
		0xEC, 0x20 };

	inline constexpr uintptr_t kGamePartyRVA = game::addresses::runtime::join_route_game_party,
							   kPartySleepRVA = game::addresses::runtime::join_route_party_sleep;

	inline constexpr uint8_t kPartySleepBytes[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xB9, 0x50, 0xBF, 0x04, 0x00, 0x00, 0x48,
		0x8B, 0xD9 };

	extern std::atomic<DialogStateFn> g_joinDialogState, g_pendingDialogState;

	inline constexpr uint8_t kJoinDialogBytes[] = { 0x83, 0x3D, 0x19, 0xA3, 0x1E, 0x0B, 0x00, 0x74, 0x13, 0x8B, 0x05, 0x15, 0xA3, 0x1E,
		0x0B, 0xFF };

	inline constexpr uint8_t kPendingDialogBytes[] = { 0x48, 0x83, 0xEC, 0x28, 0x83, 0x3D, 0x11, 0xE5, 0x58, 0x0A, 0x00, 0x74, 0x35, 0xE8,
		0xDE, 0x49 };

	uintptr_t JoinDialogState();

	uintptr_t PendingDialogState();

	extern std::atomic<bool (*)()> g_areBotsAllowed;

	extern std::atomic<bool (*)()> g_botsEnabled;

	bool AreBotsAllowed();

	bool BotsEnabled();

	bool ReadBoolDvar(uintptr_t rva, bool& value);

	void SetBoolDvar(uintptr_t rva, bool value);

	void RestoreJoinRoute();

	void MaintainFrontendParty();

	void CancelAssignedConnection();

		bool PrepareJoinRoute(std::string_view assignedMap = {}, std::string_view assignedMode = {});

	void MaintainAssignedConnection();

	void MaintainJoinRoute();

	void ResetMatchmaking();
}
