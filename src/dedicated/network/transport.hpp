#pragma once
#include "dedicated/runtime/types.hpp"
namespace dedicated::detail
{
	extern std::atomic<NetStartFn> g_netStart;

	extern std::atomic<NetCallbackFn> g_netCallback;

	extern std::atomic<NetErrorFn> g_netError;

	extern std::atomic<NETCreateSessionFn> g_netCreateSession;

	extern std::atomic<ConnectionlessPacketFn> g_connectionlessPacket;

	extern std::atomic<AddressOnlyFn> g_getChallenge;

	extern std::atomic<AddressOnlyFn> g_directConnect;

	extern std::atomic<PartyJoinRequestFn> g_partyJoinRequest;

	extern std::atomic<PartySendAcceptFn> g_partySendAccept;

	extern std::atomic<PartySendFailedFn> g_partySendFailed;

	int ReadNetValue(uintptr_t rva);

	const char* NetErrorName(int error);

	void __fastcall StartLanNetwork(const NetConfig* config, void* listener);

	void __fastcall NetError(int error);

	bool __fastcall NetworkCompleted(bool success, int mode, int* nextMode);

	void __fastcall CaptureSession(XSESSION_INFO* outSessionInfo);

	void FormatRawAddress(const void* from, char (&out)[64]);

	void FormatShortAddress(const void* from, char (&out)[32]);

	bool ReadSVCmdArg(int index, char* out, size_t outSize);

	bool ServerRunningForTrace();

	bool ReadSendGameStates(int& value, uintptr_t& globals);

	bool EnsureSendGameStates();

	void ReadConnectionlessCommand(void* msg, char (&out)[1024]);

	void __fastcall TraceConnectionlessPacket(const void* from, void* msg, std::int64_t arg2);

	void __fastcall TraceGetChallenge(const void* from);

	void __fastcall TraceDirectConnect(const void* from);

	void __fastcall TracePartyJoinRequest(void* party, const void* mainActiveClient, const void* from, void* msg);

	void __fastcall TracePartySendAccept(void* party, const void* addrPtr, void* challenge, std::uint64_t xuid);

	void __fastcall TracePartySendFailed(void* party, const void* addrPtr, int response, std::uint64_t xuid);

	int __fastcall IsDedicated(const void*);

	// Direct matchmaking hosts a native PrivateParty, including its recipe deltas.
	// Keep that transport policy independent of the systemlink gameplay/script dvar.
	// Changing only the dvar routes MatchRulesChanged to the unused online lobby.
	bool UseDedicatedPartyTransport();

	extern std::atomic<LoadoutGroupFn> g_loadoutGroup;

	extern std::atomic<RequiredStatPacketsFn> g_requiredStatPackets;

	uint64_t DedicatedRequiredStatPackets();

	int DedicatedLoadoutGroup();

	void* DedicatedServerLobby();
}
