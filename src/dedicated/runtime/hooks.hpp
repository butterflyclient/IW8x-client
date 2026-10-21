#pragma once
#include "engine/addresses.hpp"

#include "dedicated/runtime/types.hpp"
namespace dedicated::detail
{
	inline constexpr uintptr_t kRequestServerShutdownRVA = game::addresses::runtime::integration_request_server_shutdown;

	// Replay GScr_Kick1263220 uses this slot/reason/ban API. It performs the
	// native disconnect notifications, script/entity cleanup and party release.
	inline constexpr uintptr_t kKickClientRVA = game::addresses::runtime::integration_kick_client;

	inline constexpr uintptr_t kEnterFrontendRVA = 0x12AA3C0;
	inline constexpr uint8_t kEnterFrontendBytes[] = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xB7, 0xEC, 0xE1, 0xFF, 0x84, 0xC0, 0x75, 0x1D, 0xE8,
		0xEE, 0xC3 };
	extern std::atomic<DevSetupFn> g_enterFrontend;
	uintptr_t EnterFrontend();
	extern std::atomic<DevSetupFn> g_devSetup;

	extern thread_local bool g_playlistDevmapActive;

	void SkipFrontendFastFile();

	void __fastcall SkipLuiInit(bool, bool);

	void SkipClientUI();

	void __fastcall SkipResend(int);

	__declspec(noinline) void SetDedicatedCollisionTransient(const int requestedMode);
}
