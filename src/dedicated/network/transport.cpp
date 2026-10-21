#include "asset_mark_guard.h"

#include "engine/addresses.hpp"

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
#include "network/backend/native_matchmaking.h"
#include "dedicated/assets/infil_transients.h"
#include "auth/profile/local_progression.h"
namespace dedicated::detail
{
	std::atomic<NetStartFn> g_netStart{ nullptr };

	std::atomic<NetCallbackFn> g_netCallback{ nullptr };

	std::atomic<NetErrorFn> g_netError{ nullptr };

	std::atomic<NETCreateSessionFn> g_netCreateSession{ nullptr };

	std::atomic<ConnectionlessPacketFn> g_connectionlessPacket{ nullptr };

	std::atomic<AddressOnlyFn> g_getChallenge{ nullptr };

	std::atomic<AddressOnlyFn> g_directConnect{ nullptr };

	std::atomic<PartyJoinRequestFn> g_partyJoinRequest{ nullptr };

	std::atomic<PartySendAcceptFn> g_partySendAccept{ nullptr };

	std::atomic<PartySendFailedFn> g_partySendFailed{ nullptr };

	static std::atomic<unsigned> g_sessionCreateCount{ 0 };

	static std::atomic<unsigned> g_connectionlessCount{ 0 };

	static std::atomic<unsigned> g_challengeCount{ 0 };

	static std::atomic<unsigned> g_directConnectCount{ 0 };

	static std::atomic<unsigned> g_partyJoinCount{ 0 };

	int ReadNetValue(uintptr_t rva)
	{
		int value = -1;
		safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &value, sizeof(value));
		return value;
	}

	const char* NetErrorName(int error)
	{
		switch (error)
		{
		case 0:
			return "NONE";
		case 1:
			return "START_FAILED";
		case 2:
			return "NO_LOCAL_IP";
		case 3:
			return "NO_MORE_RETRIES";
		case 4:
			return "NO_RELAY_SERVER";
		case 5:
			return "RELAY_DISCONNECTED";
		default:
			return "UNKNOWN";
		}
	}

	void __fastcall StartLanNetwork(const NetConfig* config, void* listener)
	{
		NetConfig lan = *config;
		// Stock devmap requests OFFLINE_PRIMARY (3). Its completion callback
		// promotes an offline success to ONLINE, adding STUN/online startup to LAN.
		// Select the stock OFFLINE flow instead; preserve flags and the listener.
		lan.flow = 0;
		lan.relayDisabled = 1; // LAN does not require a Demonware relay allocation.
		LOG_INFO("Network", "NET_Start flow=%d -> OFFLINE(0), relay_disabled=%u; native LAN initialization", config->flow,
			static_cast<unsigned>(lan.relayDisabled));
		g_netStart.load(std::memory_order_acquire)(&lan, listener);
	}

	void __fastcall NetError(int error)
	{
		// Preserve the earliest cause before retries replace it with NO_MORE_RETRIES.
		static std::atomic<unsigned> reports{ 0 };
		if (reports.fetch_add(1, std::memory_order_relaxed) < 16)
		{
			LOG_ERR("Network", "native startup error=%s(%d), mode=%d, state=%d, attempt=%d", NetErrorName(error), error,
				ReadNetValue(game::kDWNetModeRVA), ReadNetValue(game::kDWNetStateRVA), ReadNetValue(game::kDWNetAttemptsRVA));
		}
		g_netError.load(std::memory_order_acquire)(error);
	}

	bool __fastcall NetworkCompleted(bool success, int mode, int* nextMode)
	{
		const int error = ReadNetValue(game::kDWNetErrorRVA);
		LOG_INFO("Network", "startup callback: success=%d mode=%d (offline=2), state=%d error=%s(%d) attempt=%d", success, mode,
			ReadNetValue(game::kDWNetStateRVA), NetErrorName(error), error, ReadNetValue(game::kDWNetAttemptsRVA));
		// Keep the actual result, native listener, network thread startup and fatal
		// failure handling. Returning a made-up success leaves the socket unusable.
		const bool result = g_netCallback.load(std::memory_order_acquire)(success, mode, nextMode);
		if (nextMode && *nextMode) LOG_INFO("Network", "native callback requested restart in mode=%d", *nextMode);
		if (success && error == 0)
		{
			static std::atomic<bool> announced{ false };
			bool expected = false;
			if (announced.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) OP_INFO("Network ready | LAN");
		}
		return result;
	}

	void __fastcall CaptureSession(XSESSION_INFO* outSessionInfo)
	{
		const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
		g_netCreateSession.load(std::memory_order_acquire)(outSessionInfo);
		if (!outSessionInfo) return;

		static constexpr char digits[] = "0123456789ABCDEF";
		char hex[sizeof(XSESSION_INFO) * 2 + 1]{};
		const auto* bytes = reinterpret_cast<const uint8_t*>(outSessionInfo);
		for (size_t i = 0; i < sizeof(XSESSION_INFO); ++i)
		{
			hex[i * 2] = digits[bytes[i] >> 4];
			hex[i * 2 + 1] = digits[bytes[i] & 0xF];
		}
		const unsigned index = g_sessionCreateCount.fetch_add(1, std::memory_order_relaxed) + 1;
		const uintptr_t callerRva = caller >= g_base ? caller - g_base : 0;
		const char* role = "unknown";
		if (callerRva == game::addresses::runtime::qos_session_call)
			role = "QosPayload::Init";
		else if (callerRva == game::addresses::runtime::private_party_session_call)
			role = "PartyHost_StartPrivateParty";
		else if (callerRva == game::addresses::runtime::party_host_session_call)
			role = "PartyHost_StartParty";
		// Replay devmap hosts through PrivateParty: caller 119F163 creates the
		// session subsequently registered by PartyHost_StartParty. Retain both
		// stock host paths; QosPayload::Init is a separate, unjoinable session.
		if (callerRva == game::addresses::runtime::private_party_session_call ||
			callerRva == game::addresses::runtime::party_host_session_call)
			online_services::CaptureSession(outSessionInfo, sizeof(XSESSION_INFO));
		LOG_INFO("LAN", "NET_CreateSession #%u caller RVA=0x%llX role=%s XSESSION_INFO=%s", index,
			static_cast<unsigned long long>(callerRva), role, hex);
	}

	static constexpr size_t kNetAdrBytes = 20;

	static constexpr size_t kMsgBytes = 0x38;

	void FormatRawAddress(const void* from, char (&out)[64])
	{
		joindiag::FormatAddress(from, out, sizeof(out));
	}

	void FormatShortAddress(const void* from, char (&out)[32])
	{
		struct NetAddress
		{
			int type;
			uint8_t ip[4];
			uint16_t port, flags;
			int localNetId, handle;
		} value{};

		static_assert(sizeof(NetAddress) == 20);
		if (!from || !safemem::ReadBytes(from, &value, sizeof(value)))
		{
			strcpy_s(out, "unknown");
			return;
		}
		_snprintf_s(out, sizeof(out), _TRUNCATE, "%u.%u.%u.%u", value.ip[0], value.ip[1], value.ip[2], value.ip[3]);
	}

	bool ReadSVCmdArg(int index, char* out, size_t outSize)
	{
		if (!out || !outSize) return false;
		out[0] = '\0';
		const auto argv = reinterpret_cast<SVCmdArgvFn>(g_base + game::kSVCmdArgvRVA);
		const char* value = argv(index);
		return value && safemem::ReadString(value, out, outSize) > 0;
	}

	bool ServerRunningForTrace()
	{
		return reinterpret_cast<bool (*)()>(g_base + game::kIsAnyLocalServerRunningRVA)();
	}

	bool ReadSendGameStates(int& value, uintptr_t& globals)
	{
		globals = 0;
		value = -1;
		if (!safemem::ReadBytes(reinterpret_cast<const void*>(g_base + game::kSvGameGlobalsPtrRVA), &globals, sizeof(globals)) || !globals)
			return false;
		return safemem::ReadBytes(reinterpret_cast<const void*>(globals + game::kSvGameGlobalsSendGameStatesOffset), &value, sizeof(value));
	}

	bool EnsureSendGameStates()
	{
		uintptr_t globals = 0;
		int value = -1;
		if (!ReadSendGameStates(value, globals))
		{
			static std::atomic<unsigned> failures{ 0 };
			const unsigned count = failures.fetch_add(1, std::memory_order_relaxed) + 1;
			if (count <= 8 || (count % 600) == 0)
				LOG_WARN("Dedicated", "sv_running but SvGameGlobalsMP/sendGameStates is not readable yet (attempt %u)", count);
			return false;
		}
		if (value != 0) return true;

		const int enabled = 1;
		if (!safemem::WriteBytes(reinterpret_cast<void*>(globals + game::kSvGameGlobalsSendGameStatesOffset), &enabled, sizeof(enabled)))
		{
			LOG_ERR("Dedicated", "failed to enable SvGameGlobalsMP::sendGameStates at globals=%p + 0x%zX", reinterpret_cast<void*>(globals),
				game::kSvGameGlobalsSendGameStatesOffset);
			return false;
		}

		int verify = -1;
		uintptr_t verifyGlobals = 0;
		if (!ReadSendGameStates(verify, verifyGlobals) || verifyGlobals != globals || verify != 1)
		{
			LOG_ERR(
				"Dedicated", "sendGameStates write did not verify: globals=%p value=%d", reinterpret_cast<void*>(verifyGlobals), verify);
			return false;
		}

		LOG_WARN("Dedicated",
			"enabled SvGameGlobalsMP::sendGameStates 0->1 for headless server (globals=%p offset=0x%zX); stock Replay normally latches this from a local client connection",
			reinterpret_cast<void*>(globals), game::kSvGameGlobalsSendGameStatesOffset);
		return true;
	}

	void ReadConnectionlessCommand(void* msg, char (&out)[1024])
	{
		out[0] = '\0';
		if (!msg) return;
		alignas(16) std::uint8_t copy[kMsgBytes]{};
		if (!safemem::ReadBytes(msg, copy, sizeof(copy))) return;
		reinterpret_cast<MSGBeginReadingFn>(g_base + game::kMSGBeginReadingRVA)(copy);
		(void)reinterpret_cast<MSGReadLongFn>(g_base + game::kMSGReadLongRVA)(copy);
		reinterpret_cast<MSGReadStringLineFn>(g_base + game::kMSGReadStringLineRVA)(copy, out, sizeof(out));
		// The first token identifies the packet; the rest can contain userinfo/tickets.
		for (char* p = out; *p; ++p)
		{
			if (static_cast<unsigned char>(*p) <= 32)
			{
				*p = 0;
				break;
			}
		}
	}

	void __fastcall TraceConnectionlessPacket(const void* from, void* msg, std::int64_t arg2)
	{
		char addr[64]{}, command[1024]{};
		FormatRawAddress(from, addr);
		ReadConnectionlessCommand(msg, command);
		const unsigned index = g_connectionlessCount.fetch_add(1, std::memory_order_relaxed) + 1;
		LOG_INFO("LANRX", "connectionless #%u src_raw=%s command='%s' sv_running=%d net_mode=%d net_state=%d", index, addr,
			command[0] ? command : "<unparsed>", static_cast<int>(ServerRunningForTrace()), ReadNetValue(game::kDWNetModeRVA),
			ReadNetValue(game::kDWNetStateRVA));
		g_connectionlessPacket.load(std::memory_order_acquire)(from, msg, arg2);
	}

	void __fastcall TraceGetChallenge(const void* from)
	{
		char addr[64]{};
		FormatRawAddress(from, addr);
		const unsigned index = g_challengeCount.fetch_add(1, std::memory_order_relaxed) + 1;
		LOG_INFO("LANRX", "getchallenge #%u src_raw=%s sv_running=%d", index, addr, static_cast<int>(ServerRunningForTrace()));
		g_getChallenge.load(std::memory_order_acquire)(from);
		LOG_INFO("LANRX", "getchallenge #%u returned", index);
	}

	void __fastcall TraceDirectConnect(const void* from)
	{
		char addr[64]{}, shortAddr[32]{}, guid[96]{};
		FormatRawAddress(from, addr);
		FormatShortAddress(from, shortAddr);
		ReadSVCmdArg(1, guid, sizeof(guid));
		const unsigned index = g_directConnectCount.fetch_add(1, std::memory_order_relaxed) + 1;
		int sendGameStates = -1;
		uintptr_t svGlobals = 0;
		ReadSendGameStates(sendGameStates, svGlobals);
		LOG_INFO("LANRX", "directconnect #%u ENTER src=%s guid='%s' sv_running=%d sendGameStates=%d sv_globals=%p", index, addr, guid,
			static_cast<int>(ServerRunningForTrace()), sendGameStates, reinterpret_cast<void*>(svGlobals));
		OP_INFO("Client handshake | %s", shortAddr);
		g_directConnect.load(std::memory_order_acquire)(from);
		LOG_INFO("LANRX", "directconnect #%u RETURN src_raw=%s", index, addr);
	}

	void __fastcall TracePartyJoinRequest(void* party, const void* mainActiveClient, const void* from, void* msg)
	{
		char addr[64]{}, shortAddr[32]{};
		FormatRawAddress(from, addr);
		FormatShortAddress(from, shortAddr);
		const unsigned index = g_partyJoinCount.fetch_add(1, std::memory_order_relaxed) + 1;
		int sendGameStates = -1;
		uintptr_t svGlobals = 0;
		ReadSendGameStates(sendGameStates, svGlobals);
		LOG_INFO("LANRX", "pa_joinrequest #%u ENTER src_raw=%s party=%p active_client=%p msg=%p sv_running=%d sendGameStates=%d", index,
			addr, party, mainActiveClient, msg, static_cast<int>(ServerRunningForTrace()), sendGameStates);
		OP_INFO("Join request | %s", shortAddr);
		g_partyJoinRequest.load(std::memory_order_acquire)(party, mainActiveClient, from, msg);
		LOG_INFO("LANRX", "pa_joinrequest #%u RETURN src_raw=%s", index, addr);
	}

	void __fastcall TracePartySendAccept(void* party, const void* addrPtr, void* challenge, std::uint64_t xuid)
	{
		char addr[64]{}, shortAddr[32]{};
		FormatRawAddress(addrPtr, addr);
		FormatShortAddress(addrPtr, shortAddr);
		LOG_INFO("LANRX", "pa_accept SEND dst_raw=%s party=%p challenge=%p xuid=0x%016llX", addr, party, challenge,
			static_cast<unsigned long long>(xuid));
		OP_INFO("Join accepted | %s", shortAddr);
		g_partySendAccept.load(std::memory_order_acquire)(party, addrPtr, challenge, xuid);
	}

	void __fastcall TracePartySendFailed(void* party, const void* addrPtr, int response, std::uint64_t xuid)
	{
		const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
		char addr[128]{}, reason[128]{};
		joindiag::FormatAddress(addrPtr, addr, sizeof(addr));
		joindiag::ResponseName(g_base, response, reason, sizeof(reason));
		int partyFlags[3]{ -1, -1, -1 };
		if (party) safemem::ReadBytes(static_cast<const uint8_t*>(party) + 0x4BF48, partyFlags, sizeof(partyFlags));
		char shortAddr[32]{};
		FormatShortAddress(addrPtr, shortAddr);
		OP_WARN("Join rejected | %s | %s", shortAddr, reason);
		LOG_ERR("LANRX",
			"t=%llu pa_joinfailed SEND dst=%s response=%d reason=%s xuid=%llu party=%p flags[4BF48/4BF4C/4BF50]=%d/%d/%d caller_RVA=0x%llX",
			GetTickCount64(), addr, response, reason, static_cast<unsigned long long>(xuid), party, partyFlags[0], partyFlags[1],
			partyFlags[2], static_cast<unsigned long long>(caller >= g_base ? caller - g_base : 0));
		g_partySendFailed.load(std::memory_order_acquire)(party, addrPtr, response, xuid);
	}

	int __fastcall IsDedicated(const void*)
	{
		return 1;
	}

	// Direct matchmaking hosts a native PrivateParty, including its recipe deltas.
	// Keep that transport policy independent of the systemlink gameplay/script dvar.
	// Changing only the dvar routes MatchRulesChanged to the unused online lobby.
	bool UseDedicatedPartyTransport()
	{
		return !native_matchmaking::UseOnlineLobby();
	}

	std::atomic<LoadoutGroupFn> g_loadoutGroup{ nullptr };

	std::atomic<RequiredStatPacketsFn> g_requiredStatPackets{ nullptr };

	uint64_t DedicatedRequiredStatPackets()
	{
		const auto original = g_requiredStatPackets.load(std::memory_order_acquire)();
		if (!original || g_playlistId < 0 || reinterpret_cast<uint8_t (*)()>(g_base + game::kComGameModeGetActiveGameModeRVA)() != 2)
			return original;
		// Replay 10CA5D0 selects private loadouts inline when onlinegame=0.
		// Request the native online MP set: common(blob0), MP(blob1), ranked(blob3).
		// SV stores this mask at +6F18, advertises it in connectResponse, and waits
		// for the same packets before sending gamestate. Keep that handshake intact.
		uint64_t required = 0;
		for (const int blob : { 0, 1, 3 })
		{
			uint64_t mask = 0;
			if (!safemem::ReadBytes(
					reinterpret_cast<void*>(g_base + game::addresses::runtime::stats_packet_masks + 32 * blob), &mask, sizeof(mask)) ||
				!mask || (mask >> 52))
			{
				LOG_ERR("Dedicated", "native stats packet mask unavailable for blob %d", blob);
				return original;
			}
			required |= mask;
		}
		LOG_INFO("Dedicated", "playlist initial stats: native=0x%llX ranked=0x%llX", static_cast<unsigned long long>(original),
			static_cast<unsigned long long>(required));
		return required;
	}

	int DedicatedLoadoutGroup()
	{
		// Native OFFLINE network startup leaves onlinegame=0 on this host. Online
		// playlist clients nevertheless edit ranked loadouts (group3). Select the
		// same group so native delta validation accepts those edits; do not widen
		// the allow-mask or bypass packet bounds. Manual LAN hosting keeps stock.
		if (g_playlistId >= 0 && reinterpret_cast<uint8_t (*)()>(g_base + game::kComGameModeGetActiveGameModeRVA)() == 2) return 3;
		return g_loadoutGroup.load(std::memory_order_acquire)();
	}

	void* DedicatedServerLobby()
	{
		// The matchmaking listener publishes this party's session. Admission must
		// look up the XUID in that same party, not the unused online lobby selected
		// by stock SV_MainMP_GetServerLobby when systemlink=0. Native challenge,
		// registered-member lookup and capacity checks remain in DirectConnect.
		return reinterpret_cast<void*>(g_base + native_matchmaking::ServerPartyRVA());
	}
}
