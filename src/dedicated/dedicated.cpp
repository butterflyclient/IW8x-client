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
namespace dedicated
{
	using namespace detail;

	void ObserveScriptEndLoad(void* context)
	{
		if (Enabled()) ResolveBotScripts(context);
	}

	bool Enabled()
	{
		return HasFlag(L"--dedi");
	}
}

namespace dedicated::detail
{
	static constexpr uintptr_t kDevSetupRVA = game::addresses::runtime::dedicated_dev_setup;

	static constexpr uint8_t kKickClientBytes[] = { 0x40, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x70 };

	static constexpr uint8_t kRequestServerShutdownBytes[] = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x57, 0x2B, 0xF3, 0xFF, 0x84, 0xC0, 0x74, 0x1C,
		0xE8, 0x4E, 0xF5, 0xFA, 0xFF };

	static constexpr uint8_t kDevSetupBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20,
		0xe8 };

	// DevCmdSetup's native systemlink setter must also write false. Verified full
	// argument/load/call sequence, not a scan for an isolated mov dl,1 instruction.
	static constexpr uintptr_t kDevSetupSystemLinkRVA = game::addresses::runtime::dedicated_dev_setup_system_link;

	static constexpr uint8_t kDevSetupSystemLinkBytes[] = { 0xB2, 0x01, 0x48, 0x8B, 0x0C, 0x38, 0x89, 0x1C, 0x0E, 0x48, 0x8B, 0x0D, 0xA1,
		0xAF, 0x01, 0x0C, 0xE8, 0x64, 0xD3, 0x07, 0x00 };

	std::atomic<DevSetupFn> g_enterFrontend{ nullptr };
	std::atomic<DevSetupFn> g_devSetup{ nullptr };

	thread_local bool g_playlistDevmapActive = false;

	template <size_t N>
	bool Matches(uintptr_t rva, const uint8_t (&expected)[N])
	{
		uint8_t actual[N]{};
		if (safemem::RegionExecutable(reinterpret_cast<void*>(g_base + rva)) &&
			safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), actual, N) && std::memcmp(actual, expected, N) == 0)
			return true;
		LOG_ERR("Dedicated", "target bytes mismatch at RVA 0x%llX", static_cast<unsigned long long>(rva));
		return false;
	}

	void SkipFrontendFastFile()
	{
	}

	void __fastcall SkipLuiInit(bool, bool)
	{
	}

	void SkipClientUI()
	{
	}

	void __fastcall SkipResend(int)
	{
	}

	__declspec(noinline) void SetDedicatedCollisionTransient(const int requestedMode)
	{
		constexpr int MODE_NONE = 0;
		constexpr int MODE_CLOSEST = 1;
		constexpr int MODE_ALL = 2;
		const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());

		// Unload/reset requests must reach native MODE_NONE even when Donetsk full
		// collision is enabled, so its numbered-file references are released.
		if (requestedMode == MODE_NONE)
		{
			if (const auto setTransientMode = g_setTransientMode.load(std::memory_order_acquire))
			{
				bap::TraceCollisionMode(caller, requestedMode, MODE_NONE, false);
				setTransientMode(MODE_NONE);
				bap::TraceCollisionMode(caller, requestedMode, MODE_NONE, true);
			}
			return;
		}

		if (bap::ShouldUseFullCollision(g_mapName))
		{
			LOG_INFO(
				"BAP", "Donetsk BAP active; requested collision mode=%d -> numbered-tile full collision mode=%d", requestedMode, MODE_ALL);

			if (const auto setTransientMode = g_setTransientMode.load(std::memory_order_acquire))
			{
				bap::TraceCollisionMode(caller, requestedMode, MODE_ALL, false);
				setTransientMode(MODE_ALL);
				bap::TraceCollisionMode(caller, requestedMode, MODE_ALL, true);
			}
			else
			{
				LOG_ERR("BAP", "CL_TransientsCollisionMP_SetTransientMode trampoline is null");
			}
			return;
		}

		if (bap::Enabled() && bap::RequiredMap(g_mapName))
		{
			LOG_WARN("BAP", "-bap requested for Donetsk but BAP routing is not ready; forcing closest collision mode=%d", MODE_CLOSEST);
		}

		if (const auto setTransientMode = g_setTransientMode.load(std::memory_order_acquire))
		{
			bap::TraceCollisionMode(caller, requestedMode, MODE_CLOSEST, false);
			setTransientMode(MODE_CLOSEST);
			bap::TraceCollisionMode(caller, requestedMode, MODE_CLOSEST, true);
		}
		else
		{
			LOG_ERR("BAP", "CL_TransientsCollisionMP_SetTransientMode trampoline is null");
		}
	}
}

namespace dedicated
{
	using namespace detail;

	hook::Status Install(uintptr_t moduleBase)
	{

		const auto infil = infil_transients::Install(moduleBase);
		if (infil != hook::Status::Installed) return infil;
		const auto markGuard = asset_mark_guard::Install(moduleBase);
		if (markGuard != hook::Status::Installed) return markGuard;
		// Arm crash capture before any BAP hook/inline bridge can execute. Core hooks
		// remain staged until lifecycle installation enables the complete routing.
		if (bap::Enabled())
		{
			if (!bap::InitializeCrashDiagnostics(moduleBase)) return hook::Status::Failed;
			const auto bapCore = bap::InstallCore(moduleBase);
			if (bapCore != hook::Status::Installed) return bapCore;
			const auto bapLifecycle = bap::InstallLifecycle(moduleBase);
			if (bapLifecycle != hook::Status::Installed) return bapLifecycle;
		}
		std::lock_guard<std::mutex> lock(hook::detail::Mutex());
		if (g_installed) return hook::Status::Installed;
		g_base = moduleBase;
		LOG_INFO("Dedicated", "installing dedicated server patches at the splash boundary");
		g_botMovementWorkersSupported =
			Matches(kQueueClientThinkRVA, kQueueClientThinkBytes) && Matches(kFinishClientWorkersRVA, kFinishClientWorkersBytes);
		if (!Matches(kRequestServerShutdownRVA, kRequestServerShutdownBytes) || !Matches(kKickClientRVA, kKickClientBytes) ||
			!Matches(kDevSetupSystemLinkRVA, kDevSetupSystemLinkBytes) ||
			!Matches(game::kComGameModeSetDesiredGameModeRVA, game::kComGameModeSetDesiredGameModePrologue) ||
			!Matches(game::kComGameModeGetActiveGameModeRVA, game::kComGameModeGetActiveGameModePrologue) ||
			!Matches(game::kBGGetBotsConnectTypeRVA, game::kBGGetBotsConnectTypePrologue) ||
			!Matches(game::kBGSetBotsConnectTypeRVA, game::kBGSetBotsConnectTypePrologue) ||
			!Matches(game::kScriptContextServerRVA, game::kScriptContextServerPrologue) ||
			!Matches(game::kScrGetFunctionHandleRVA, game::kScrGetFunctionHandlePrologue) ||
			!Matches(game::kScrAddStructRVA, game::kScrAddStructPrologue) ||
			!Matches(game::kScrAddUndefinedRVA, game::kScrAddUndefinedPrologue) ||
			!Matches(game::kScrAddStringRVA, game::kScrAddStringPrologue) ||
			!Matches(game::kGScrExecEntThreadRVA, game::kGScrExecEntThreadPrologue) ||
			!Matches(game::kScrFreeThreadRVA, game::kScrFreeThreadPrologue) ||
			!Matches(game::kScrFindEntityIdRVA, game::kScrFindEntityIdPrologue) ||
			!Matches(game::kScrFindVariableRVA, game::kScrFindVariablePrologue) ||
			!Matches(game::kSLFindStringRVA, game::kSLFindStringPrologue) ||
			!Matches(game::kSLConvertToStringRVA, game::kSLConvertToStringPrologue) ||
			!Matches(game::kGScrNotifyRVA, game::kGScrNotifyPrologue) ||
			!Matches(game::kGetActiveServerApplicationRVA, game::kGetActiveServerApplicationPrologue) ||
			!Matches(game::kCmdAddServerCommandListRVA, game::kCmdAddServerCommandListPrologue) ||
			!Matches(game::kSVDevmapCmdRVA, game::kSVDevmapCmdPrologue) ||
			!Matches(game::kCLTransientsCollisionMPSetTransientModeRVA, game::kCLTransientsCollisionMPSetTransientModePrologue) ||
			!Matches(game::kDvarGetStringRVA, game::kDvarGetStringPrologue) ||
			!Matches(game::kDvarSetBoolInternalRVA, game::kDvarSetBoolInternalPrologue) ||
			!Matches(game::kEnterCriticalRVA, game::kEnterCriticalPrologue) ||
			!Matches(game::kLeaveCriticalRVA, game::kLeaveCriticalPrologue) ||
			!Matches(game::kBGOmnvarClearTablesRVA, game::kBGOmnvarClearTablesPrologue) ||
			!Matches(game::kIsAnyLocalServerRunningRVA, game::kIsAnyLocalServerRunningPrologue) ||
			!Matches(game::kNETCreateSessionRVA, game::kNETCreateSessionPrologue) ||
			!Matches(game::kSVMainMPConnectionlessPacketRVA, game::kSVMainMPConnectionlessPacketPrologue) ||
			!Matches(game::kSVClientMPGetChallengeRVA, game::kSVClientMPGetChallengePrologue) ||
			!Matches(game::kSVClientMPDirectConnectRVA, game::kSVClientMPDirectConnectPrologue) ||
			!Matches(game::kPartyAtomicHostHandleJoinRequestRVA, game::kPartyAtomicHostHandleJoinRequestPrologue) ||
			!Matches(game::kPartyAtomicHostSendAcceptMessageRVA, game::kPartyAtomicHostSendAcceptMessagePrologue) ||
			!Matches(game::kPartyAtomicHostSendJoinFailedResponseRVA, game::kPartyAtomicHostSendJoinFailedResponsePrologue) ||
			!Matches(game::kSVCmdArgvRVA, game::kSVCmdArgvPrologue) ||
			!Matches(game::kMSGBeginReadingRVA, game::kMSGBeginReadingPrologue) ||
			!Matches(game::kMSGReadLongRVA, game::kMSGReadLongPrologue) ||
			!Matches(game::kMSGReadStringLineRVA, game::kMSGReadStringLinePrologue))
			return hook::Status::Failed;

		struct Patch
		{
			uintptr_t rva;
			void* detour;
			const uint8_t* bytes;
			size_t size;
			const char* message;
		};

		const Patch patches[] = {
			{ game::addresses::runtime::bot_batch, reinterpret_cast<void*>(&BotBatch), kBotBatchBytes, sizeof(kBotBatchBytes),
				"bounded on-demand bot batch timing" },
			{ game::addresses::runtime::bot_movement, reinterpret_cast<void*>(&BotMovement), kBotMovementBytes, sizeof(kBotMovementBytes),
				"bounded on-demand bot movement timing" },
			{ game::addresses::runtime::nav_context, reinterpret_cast<void*>(&NavContext), kNavContextBytes, sizeof(kNavContextBytes),
				"native bot navigation snapshot boundary" },
			{ game::addresses::runtime::nav_reachable, reinterpret_cast<void*>(&NavReachable), kNavReachableBytes,
				sizeof(kNavReachableBytes), "native bot navigation answer validation" },
			{ kClientThinkRVA, reinterpret_cast<void*>(&ClientThink), kClientThinkBytes, sizeof(kClientThinkBytes),
				"BR native movement worker capture" },
			{ game::addresses::runtime::reachable_search, reinterpret_cast<void*>(&ReachableSearch), kReachableSearchBytes,
				sizeof(kReachableSearchBytes), "bounded on-demand reachable search timing" },
			{ game::addresses::runtime::script_goal, reinterpret_cast<void*>(&ScriptGoal), kScriptGoalBytes, sizeof(kScriptGoalBytes),
				"bounded on-demand script navigation timing" },
			{ game::addresses::runtime::navigation_projection, reinterpret_cast<void*>(&NavigationProjection), kProjectionBytes,
				sizeof(kProjectionBytes), "bounded on-demand navigation projection timing" },
			{ game::addresses::runtime::archive_character, reinterpret_cast<void*>(&ArchiveCharacter), kArchiveBytes, sizeof(kArchiveBytes),
				"bounded on-demand character archive timing" },
			{ kBotUpdateCommandRVA, reinterpret_cast<void*>(&BotUpdateCommand), kBotUpdateCommandBytes, sizeof(kBotUpdateCommandBytes),
				"BR bot decisions paced independently of native movement" },
			{ game::kGetLoadoutStatsGroupRVA, reinterpret_cast<void*>(&DedicatedLoadoutGroup), game::kGetLoadoutStatsGroupPrologue,
				sizeof(game::kGetLoadoutStatsGroupPrologue), "playlist hosts select ranked loadouts for native player-data validation" },
			{ game::kGetRequiredStatPacketsRVA, reinterpret_cast<void*>(&DedicatedRequiredStatPackets),
				game::kGetRequiredStatPacketsPrologue, sizeof(game::kGetRequiredStatPacketsPrologue),
				"playlist hosts request ranked stats during the initial native handshake" },
			{ game::kSVGetServerLobbyRVA, reinterpret_cast<void*>(&DedicatedServerLobby), game::kSVGetServerLobbyPrologue,
				sizeof(game::kSVGetServerLobbyPrologue), "server admission uses the published dedicated party" },
			{ game::kSystemLinkPredicateRVA, reinterpret_cast<void*>(&UseDedicatedPartyTransport), game::kSystemLinkPredicatePrologue,
				sizeof(game::kSystemLinkPredicatePrologue), "private-party transport retained independently of systemlink gameplay dvar" },
			{ kDevSetupRVA, reinterpret_cast<void*>(&DevSetup), kDevSetupBytes, sizeof(kDevSetupBytes),
				"playlist recipe/settings restored after native devmap party setup" },
			{ game::kNETStartRVA, reinterpret_cast<void*>(&StartLanNetwork), game::kNETStartPrologue, sizeof(game::kNETStartPrologue),
				"NET_Start selects native OFFLINE flow" },
			{ game::kNetInitDemonwareCallbackRVA, reinterpret_cast<void*>(&NetworkCompleted), game::kNetInitDemonwareCallbackPrologue,
				sizeof(game::kNetInitDemonwareCallbackPrologue), "network startup callback diagnostics installed" },
			{ game::kDWNetSetErrorRVA, reinterpret_cast<void*>(&NetError), game::kDWNetSetErrorPrologue,
				sizeof(game::kDWNetSetErrorPrologue), "network startup error diagnostics installed" },
			{ game::kNETCreateSessionRVA, reinterpret_cast<void*>(&CaptureSession), game::kNETCreateSessionPrologue,
				sizeof(game::kNETCreateSessionPrologue), "NET_CreateSession session capture installed" },
			{ game::kSVMainMPConnectionlessPacketRVA, reinterpret_cast<void*>(&TraceConnectionlessPacket),
				game::kSVMainMPConnectionlessPacketPrologue, sizeof(game::kSVMainMPConnectionlessPacketPrologue),
				"SV_MainMP_ConnectionlessPacket ingress diagnostics installed" },
			{ game::kSVClientMPGetChallengeRVA, reinterpret_cast<void*>(&TraceGetChallenge), game::kSVClientMPGetChallengePrologue,
				sizeof(game::kSVClientMPGetChallengePrologue), "SV_ClientMP_GetChallenge diagnostics installed" },
			{ game::kSVClientMPDirectConnectRVA, reinterpret_cast<void*>(&TraceDirectConnect), game::kSVClientMPDirectConnectPrologue,
				sizeof(game::kSVClientMPDirectConnectPrologue), "SV_ClientMP_DirectConnect diagnostics installed" },
			{ game::kPartyAtomicHostHandleJoinRequestRVA, reinterpret_cast<void*>(&TracePartyJoinRequest),
				game::kPartyAtomicHostHandleJoinRequestPrologue, sizeof(game::kPartyAtomicHostHandleJoinRequestPrologue),
				"PartyAtomicHost_HandleJoinRequest diagnostics installed" },
			{ game::kPartyAtomicHostSendAcceptMessageRVA, reinterpret_cast<void*>(&TracePartySendAccept),
				game::kPartyAtomicHostSendAcceptMessagePrologue, sizeof(game::kPartyAtomicHostSendAcceptMessagePrologue),
				"PartyAtomicHost_SendAcceptMessage diagnostics installed" },
			{ game::kPartyAtomicHostSendJoinFailedResponseRVA, reinterpret_cast<void*>(&TracePartySendFailed),
				game::kPartyAtomicHostSendJoinFailedResponsePrologue, sizeof(game::kPartyAtomicHostSendJoinFailedResponsePrologue),
				"PartyAtomicHost_SendJoinFailedResponse diagnostics installed" },
			{ game::kPartyIsServerDedicatedRVA, reinterpret_cast<void*>(&IsDedicated), game::kPartyIsServerDedicatedPrologue,
				sizeof(game::kPartyIsServerDedicatedPrologue), "Party_IsServerDedicated forced true" },
			{ game::kComFastFileFrameFrontEndRVA, reinterpret_cast<void*>(&SkipFrontendFastFile), game::kComFastFileFrameFrontEndPrologue,
				sizeof(game::kComFastFileFrameFrontEndPrologue), "frontend fastfile disabled" },
			{ kEnterFrontendRVA, reinterpret_cast<void*>(&EnterFrontend), kEnterFrontendBytes, sizeof(kEnterFrontendBytes),
				"completed dedicated matches recycle before frontend asset reload" },
			{ game::kLUICoDInitRVA, reinterpret_cast<void*>(&SkipLuiInit), game::kLUICoDInitPrologue, sizeof(game::kLUICoDInitPrologue),
				"LUI_CoD_Init disabled" },
			{ game::kCLInitUIRVA, reinterpret_cast<void*>(&SkipClientUI), game::kCLInitUIPrologue, sizeof(game::kCLInitUIPrologue),
				"CL_InitUI disabled" },
			{ game::kCLMainMPCheckForResendRVA, reinterpret_cast<void*>(&SkipResend), game::kCLMainMPCheckForResendPrologue,
				sizeof(game::kCLMainMPCheckForResendPrologue), "local client resend disabled" },
			{ game::kCLTransientsCollisionMPSetTransientModeRVA, reinterpret_cast<void*>(&SetDedicatedCollisionTransient),
				game::kCLTransientsCollisionMPSetTransientModePrologue, sizeof(game::kCLTransientsCollisionMPSetTransientModePrologue),
				"collision mode policy: Donetsk BAP numbered tiles, closest otherwise, native unload" },
			{ game::kComFrameRVA, reinterpret_cast<void*>(&Frame), game::kComFramePrologue, sizeof(game::kComFramePrologue),
				"Com_Frame startup driver installed" },
		};
		size_t prepared = 0;
		for (const auto& patch : patches)
		{
			void* trampoline = nullptr;
			const auto status =
				hook::detail::Prepare(reinterpret_cast<void*>(g_base + patch.rva), patch.detour, patch.bytes, patch.size, &trampoline);
			if (status != hook::Status::Installed)
			{
				LOG_ERR("Dedicated", "could not prepare critical patch at RVA 0x%llX", static_cast<unsigned long long>(patch.rva));
				for (size_t i = 0; i < prepared; ++i)
					MH_RemoveHook(reinterpret_cast<void*>(g_base + patches[i].rva));
				return hook::Status::Failed;
			}
			++prepared;
			if (patch.rva == game::addresses::runtime::bot_batch)
				g_botBatch.store(reinterpret_cast<BotBatchFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::addresses::runtime::bot_movement)
				g_botMovement.store(reinterpret_cast<BotMovementFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::addresses::runtime::nav_context)
				g_navContext.store(reinterpret_cast<NavContextFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::addresses::runtime::nav_reachable)
				g_navReachable.store(reinterpret_cast<botnav::NativeReachable>(trampoline), std::memory_order_release);
			else if (patch.rva == kClientThinkRVA)
				g_clientThink.store(reinterpret_cast<ClientThinkFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::addresses::runtime::reachable_search)
				g_reachableSearch.store(reinterpret_cast<ReachableSearchFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::addresses::runtime::script_goal)
				g_scriptGoal.store(reinterpret_cast<ScriptGoalFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::addresses::runtime::navigation_projection)
				g_navigationProjection.store(reinterpret_cast<NavigationProjectionFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::addresses::runtime::archive_character)
				g_archiveCharacter.store(reinterpret_cast<ArchiveCharacterFn>(trampoline), std::memory_order_release);
			else if (patch.rva == kBotUpdateCommandRVA)
				g_botUpdateCommand.store(reinterpret_cast<BotUpdateCommandFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kGetLoadoutStatsGroupRVA)
				g_loadoutGroup.store(reinterpret_cast<LoadoutGroupFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kGetRequiredStatPacketsRVA)
				g_requiredStatPackets.store(reinterpret_cast<RequiredStatPacketsFn>(trampoline), std::memory_order_release);
			else if (patch.rva == kDevSetupRVA)
				g_devSetup.store(reinterpret_cast<DevSetupFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kComFrameRVA)
				g_frame.store(reinterpret_cast<FrameFn>(trampoline), std::memory_order_release);
			else if (patch.rva == kEnterFrontendRVA)
				g_enterFrontend.store(reinterpret_cast<DevSetupFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kCLTransientsCollisionMPSetTransientModeRVA)
				g_setTransientMode.store(reinterpret_cast<SetTransientModeFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kNETStartRVA)
				g_netStart.store(reinterpret_cast<NetStartFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kNetInitDemonwareCallbackRVA)
				g_netCallback.store(reinterpret_cast<NetCallbackFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kDWNetSetErrorRVA)
				g_netError.store(reinterpret_cast<NetErrorFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kNETCreateSessionRVA)
				g_netCreateSession.store(reinterpret_cast<NETCreateSessionFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kSVMainMPConnectionlessPacketRVA)
				g_connectionlessPacket.store(reinterpret_cast<ConnectionlessPacketFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kSVClientMPGetChallengeRVA)
				g_getChallenge.store(reinterpret_cast<AddressOnlyFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kSVClientMPDirectConnectRVA)
				g_directConnect.store(reinterpret_cast<AddressOnlyFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kPartyAtomicHostHandleJoinRequestRVA)
				g_partyJoinRequest.store(reinterpret_cast<PartyJoinRequestFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kPartyAtomicHostSendAcceptMessageRVA)
				g_partySendAccept.store(reinterpret_cast<PartySendAcceptFn>(trampoline), std::memory_order_release);
			else if (patch.rva == game::kPartyAtomicHostSendJoinFailedResponseRVA)
				g_partySendFailed.store(reinterpret_cast<PartySendFailedFn>(trampoline), std::memory_order_release);
		}
		// At splash, startup is still disarmed and DevCmdSetup cannot be executing.
		// Change only this dedicated call's boolean argument; the native setter and
		// all ordinary client devmap behavior stay intact.
		auto* systemLinkArgument = reinterpret_cast<uint8_t*>(g_base + kDevSetupSystemLinkRVA + 1);
		DWORD previousProtection = 0, ignoredProtection = 0;
		if (!VirtualProtect(systemLinkArgument, 1, PAGE_EXECUTE_READWRITE, &previousProtection)) return hook::Status::Failed;
		*systemLinkArgument = 0;
		const bool restoredProtection = VirtualProtect(systemLinkArgument, 1, previousProtection, &ignoredProtection) != FALSE;
		if (!FlushInstructionCache(GetCurrentProcess(), systemLinkArgument, 1) || !restoredProtection) return hook::Status::Failed;
		LOG_INFO("Dedicated", "native DevCmdSetup systemlink write changed to 0 (RVA 0x136B7D7)");
		for (const auto& patch : patches)
		{
			if (MH_QueueEnableHook(reinterpret_cast<void*>(g_base + patch.rva)) != MH_OK)
			{
				LOG_ERR("Dedicated", "could not queue critical patch at RVA 0x%llX", static_cast<unsigned long long>(patch.rva));
				return hook::Status::Failed; // Splash caller terminates; never continue with a partial patch set.
			}
		}
		const MH_STATUS enabled = MH_ApplyQueued();
		if (enabled != MH_OK)
		{
			LOG_ERR("Dedicated", "enabling dedicated patch set failed: %s", MH_StatusToString(enabled));
			return hook::Status::Failed;
		}
		for (const auto& patch : patches)
			LOG_INFO("Dedicated", "%s (RVA 0x%llX)", patch.message, static_cast<unsigned long long>(patch.rva));
		g_installed = true;
		return hook::Status::Installed;
	}

	void ArmStartup()
	{
		LoadStartupOptions();
		if (!g_optionsValid)
		{
			Fail("invalid --playlist, --playlist-entry or --min-players option");
			return;
		}
		console::SetDedicatedTitle(g_mapName);
		console::SetCommandHandler(&HandleConsoleCommand);
		OP_INFO("Dedicated runtime initialized");
		if (AutoPlaylist())
			OP_INFO("Automatic dedicated host | waiting for matchmaking");
		else
			OP_INFO("Server starting | map=%s | mode=%s", g_mapName, g_gameType);
		console::EnableCommandInput(g_base);
		LOG_INFO("Dedicated", "all dedicated patches installed; auth/dvar/Omnvar/client guards ready; command startup armed");
		g_armed.store(true, std::memory_order_release);
	}

	bool AutoPlaylist()
	{
		static const bool automatic = HasFlag(L"--auto-playlist");
		return automatic;
	}

	bool StartRequested()
	{
		return g_startRequested.load(std::memory_order_acquire);
	}
}
