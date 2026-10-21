#pragma once
#include <cstddef>
#include <cstdint>
namespace game
{
	// Native LAN startup; see ida-reports/network_startup.md.
	constexpr uintptr_t kNETStartRVA = 0x12BE220;
	constexpr uint8_t kNETStartPrologue[] = {
		0x4C,
		0x8B,
		0xC2,
		0x4C,
		0x8D,
		0x0D,
		0x16,
		0x06,
		0x23,
		0x0C,
		0x48,
		0x8B,
		0xD1,
		0x48,
		0x8D,
		0x0D,
		0xEC,
		0xDF,
		0x1A,
		0x0C,
		0xE9,
		0x87,
		0xE6,
		0xFF,
		0xFF,
	};
	constexpr uintptr_t kNetInitDemonwareCallbackRVA = 0x12BC0D0;
	constexpr uint8_t kNetInitDemonwareCallbackPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x6C,
		0x24,
		0x10,
		0x48,
		0x89,
		0x74,
		0x24,
		0x18,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
	};
	constexpr uintptr_t kDWNetSetErrorRVA = 0x14EF150;
	constexpr uint8_t kDWNetSetErrorPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x30,
	};
	constexpr uintptr_t kDWNetStateRVA = 0xE6BDA40;
	constexpr uintptr_t kDWNetErrorRVA = 0xE6BDA44;
	constexpr uintptr_t kDWNetModeRVA = 0xE6BDA48;
	constexpr uintptr_t kDWNetAttemptsRVA = 0xE6BDA4C;

	// Dedicated startup: exact Replay bytes; proof in ida-reports/dedicated_startup.md.
	constexpr uintptr_t kComGameModeSetDesiredGameModeRVA = 0x10C88F0;
	constexpr uint8_t kComGameModeSetDesiredGameModePrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6,
		0x05, 0xDF, 0xBB, 0x45, 0x0B, 0x0F, 0xB6, 0xFA, 0x0F, 0xB6, 0xD9 };
	constexpr uintptr_t kPartyIsServerDedicatedRVA = 0x118EC70;
	constexpr uint8_t kPartyIsServerDedicatedPrologue[] = { 0x33, 0xC0, 0x83, 0xB9, 0xD8, 0xBE, 0x04, 0x00, 0xFE, 0x0F, 0x94, 0xC0, 0xC3 };
	constexpr uintptr_t kComFastFileFrameFrontEndRVA = 0x10B29C0;
	constexpr uint8_t kComFastFileFrameFrontEndPrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0xE8, 0x11, 0x22,
		0x00, 0x00, 0xE8, 0xCC, 0x4C, 0x03, 0x00 };
	constexpr uintptr_t kLUICoDInitRVA = 0x19D1E70;
	constexpr uint8_t kLUICoDInitPrologue[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x28, 0x01, 0x00,
		0x00 };
	constexpr uintptr_t kCLInitUIRVA = 0x15CE980;
	constexpr uint8_t kCLInitUIPrologue[] = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x37, 0xEC, 0x5C, 0x00, 0xE8, 0xB2, 0x81, 0x40, 0x00, 0xE8,
		0x0D, 0x7E, 0xAF, 0xFF };
	constexpr uintptr_t kCLMainMPCheckForResendRVA = 0x15E4FC0;
	constexpr uint8_t kCLMainMPCheckForResendPrologue[] = { 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0xC0,
		0x00, 0x00, 0x00 };
	constexpr uintptr_t kComFrameRVA = 0x12AB770;
	constexpr uint8_t kComFramePrologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xB9, 0x02, 0x00, 0x00, 0x00, 0xE8, 0x10, 0x38, 0x02,
		0x00 };
	constexpr uintptr_t kComGameModeGetActiveGameModeRVA = 0x10C86E0;
	constexpr uint8_t kComGameModeGetActiveGameModePrologue[] = { 0x0F, 0xB6, 0x05, 0xF9, 0xBD, 0x45, 0x0B, 0xC3 };
	constexpr uintptr_t kComGameModeSupportsFeatureRVA = 0x10C8980;
	constexpr uint8_t kComGameModeSupportsFeaturePrologue[] = {
		0x0F,
		0xB6,
		0x05,
		0x59,
		0xBB,
		0x45,
		0x0B,
		0x48,
		0x8D,
		0x15,
		0x72,
		0x76,
		0xF3,
		0xFE,
	};
	constexpr uintptr_t kGetActiveServerApplicationRVA = 0x132CC50;
	constexpr uint8_t kGetActiveServerApplicationPrologue[] = { 0x48, 0x8B, 0x05, 0xA9, 0x30, 0xE5, 0x0C, 0xC3 };
	// Collision-transient load mode setter. Structurally mapped from the symbol-rich
	// SV_InitMP_SpawnServer_LoadMapAssets call sequence into Replay: both map-load
	// branches pass mode 2 to this two-instruction global setter. Dedicated forces
	// mode 1 (closest transient) so large BR maps can stream collision by proximity.
	constexpr uintptr_t kCLTransientsCollisionMPSetTransientModeRVA = 0xD6B7D0;
	constexpr uint8_t kCLTransientsCollisionMPSetTransientModePrologue[] = {
		0x89,
		0x0D,
		0x5E,
		0xBD,
		0xF5,
		0x04,
		0xC3,
	};

	// BAP/xmodeldetailcollision DB seams.  Mapped in the exact Replay image by
	// aligning stock DB_LinkXAssetEntry behavior and the DB_AssetEntryPool/Table
	// method call graph against the symbol-rich build.  These are behavior/xref
	// mappings, not cross-build byte copies.
	constexpr uintptr_t kDBAssetEntryPoolAllocateRVA = 0xF5DD30;
	constexpr uintptr_t kDBAssetEntryPoolFreeRVA = 0xF5DE40;
	constexpr uintptr_t kDBAssetEntryTableAddToStashTailRVA = 0xF5DEF0;
	constexpr uintptr_t kDBAssetEntryTableAddToTableRVA = 0xF5DF60;
	constexpr uintptr_t kDBAssetEntryTableFindAssetRVA = 0xF5E010;
	constexpr uintptr_t kDBAssetEntryTableMakeFirstStashedEntryRVA = 0xF5E350;
	constexpr uintptr_t kDBAssetEntryTableRemoveFromTableRVA = 0xF5E3D0;
	constexpr uintptr_t kDBAssetEntryTableRemoveStashedAssetRVA = 0xF5E4D0;
	// Called by Replay's stock Allocate/Free implementations for per-type XAsset header
	// storage. The PDB-like label at 0xF5E5B0 is stale/misaligned: stock Allocate passes
	// the numeric XAsset type, and the target dispatches through a per-type function table.
	constexpr uintptr_t kDBAllocateXAssetHeaderRVA = 0xF5E5B0;
	constexpr uintptr_t kDBFreeXAssetHeaderRVA = 0xF5F1F0;

	// Replay DB/BAP lifecycle seams, structurally matched against the reference
	// helper behavior and confirmed by their direct asset-entry bitset accesses.
	constexpr uintptr_t kDBAssetEntryStockPoolRVA = 0xC815C60;
	constexpr size_t kDBAssetEntryStockCount = 358400;
	constexpr size_t kDBAssetEntryStockLifecyclePlaneOffset = 0x854D08;
	constexpr size_t kDBAssetEntryStockZonePrimaryOffset = 0x85FC08;
	constexpr size_t kDBAssetEntryStockZoneSecondaryOffset = 0x86AB08;
	constexpr size_t kDBAssetEntryStockZoneGroupStride = 0x15E00;
	// AddToTable writes both planes; AddToStashTail writes only the including-stashed
	// plane.  This identifies 0x849E08 as the head-only plane and 0x83EF08 as the
	// including-stashed plane in Replay.
	constexpr size_t kDBAssetEntryStockTableHeadPlaneOffset = 0x849E08;
	constexpr size_t kDBAssetEntryStockTableMemberPlaneOffset = 0x83EF08;
	// DB_LinkXAssetEntry writes this plane for transient-head/registered entries.
	// It is the Replay counterpart of the reference BAP plane at +2,499,976.
	constexpr size_t kDBAssetEntryStockTransientHeadPlaneOffset = 0x88B808;
	constexpr size_t kDBAssetEntryStockUseOncePlaneOffset = 0x8A1608;
	constexpr size_t kDBAssetEntryStockResolvedHeadPlaneOffset = 0x8AC508;
	constexpr uintptr_t kDBAssetAnyZoneRVA = 0x11AC3B0;
	constexpr uintptr_t kDBAssetUsePendingRVA = 0x11AC340;
	constexpr uintptr_t kDBAssetUseTransitionOnceRVA = 0x11AFBB0;
	constexpr uintptr_t kDBAssetLifecycleMarkRVA = 0x11AF110;
	// The stock lifecycle marker writes the stock entry bit directly here.  BAP's
	// reference hook removes that write and performs the matching stock/local write
	// in its wrapper instead.
	constexpr uintptr_t kDBAssetLifecycleStockWriteRVA = 0x11AF18D;
	// Inline stock-entry index/bitset arithmetic in the transient/default processing
	// paths.  The reference BAP replaces these blocks with auxiliary-aware helpers.
	constexpr uintptr_t kDBAssetZonePairWriteBlockRVA = 0x11AD57E;
	constexpr size_t kDBAssetZonePairWriteBlockSize = 100;
	constexpr uintptr_t kDBAssetResolvedHeadWriteBlockRVA = 0x11AE625;
	constexpr size_t kDBAssetResolvedHeadWriteBlockSize = 72;
	constexpr uintptr_t kDBAssetResolvedHeadWriteResumeRVA = 0x11AE6D0;
	constexpr uintptr_t kDBAssetAlternateLifecycleWriteBlockRVA = 0x11AAF81;
	constexpr size_t kDBAssetAlternateLifecycleWriteBlockSize = 67;
	constexpr uintptr_t kDBAssetReplaceHeadLifecycleClearBlockRVA = 0x11AEC83;
	constexpr size_t kDBAssetReplaceHeadLifecycleClearBlockSize = 74;
	// DB_LinkXAssetEntry has four additional stock-pool assumptions which the
	// reference avoids by routing type 101 through its private DB_Link clone.  Replay
	// is patched in-place instead: one stash-index -> entry conversion, two writes
	// to the transient-head plane, and the UseOnce membership read immediately after
	// AnyZone in the duplicate/stash path.  The behavior is the same while preserving
	// the target build's native DB_Link control flow.
	constexpr uintptr_t kDBLinkStashNextBlockRVA = 0x11AC8C6;
	constexpr size_t kDBLinkStashNextBlockSize = 27;
	constexpr uintptr_t kDBLinkTransientHeadWriteABlockRVA = 0x11ACAA1;
	constexpr size_t kDBLinkTransientHeadWriteABlockSize = 73;
	constexpr uintptr_t kDBLinkTransientHeadWriteBBlockRVA = 0x11ACB05;
	constexpr size_t kDBLinkTransientHeadWriteBBlockSize = 76;
	constexpr uintptr_t kDBLinkUseOnceTestBlockRVA = 0x11ACB8D;
	constexpr size_t kDBLinkUseOnceTestBlockSize = 66;
	constexpr uintptr_t kDBLinkUseOnceTestFalseResumeRVA = 0x11ACC0E;
	constexpr uintptr_t kDBTransientRegisteredPredicateRVA = 0x15F6E70;
	constexpr uintptr_t kDBTransientRegisteredFollowupRVA = 0x15F8F30;
	constexpr uintptr_t kSVDevmapCmdRVA = 0x136BBB0;
	constexpr uint8_t kSVDevmapCmdPrologue[] = { 0x41, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x38, 0x88, 0x6E,
		0x04 };
	constexpr uintptr_t kCmdAddServerCommandListRVA = 0x12966C0;
	constexpr uint8_t kCmdAddServerCommandListPrologue[] = { 0x48, 0x83, 0xEC, 0x28, 0x85, 0xD2, 0x0F, 0x84, 0xF5, 0x00, 0x00, 0x00, 0x48,
		0x89, 0x5C, 0x24, 0x30 };
	constexpr uint8_t kBGOmnvarClearTablesPrologue[] = { 0x48, 0x83, 0xEC, 0x28, 0x33, 0xD2, 0x48, 0x8D, 0x0D, 0x73, 0x30, 0xF7, 0x04, 0x41,
		0xB8, 0x00 };
	constexpr uint8_t kGameModeMP = 2;
	constexpr uintptr_t kComFullyInitializedRVA = 0xD386B6C;
	constexpr uintptr_t kServerCommandsRVA = 0xD216DA0;
	constexpr uintptr_t kClientCommandsRVA = 0xD216D98;
	constexpr uintptr_t kPriorityClientCommandsRVA = 0xD216D90;
	constexpr uintptr_t kServerCommandForwarderRVA = 0x1295ED0;
	constexpr uintptr_t kSystemLinkHostDvarRVA = 0xD386790;
	constexpr uintptr_t kDvarRegisterBoolRVA = 0x13E7670;
	constexpr uint8_t kDvarRegisterBoolPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x60,
	};
	constexpr size_t kDvarRegisterBoolStolen = sizeof(kDvarRegisterBoolPrologue);

	using Dvar_RegisterBool_t = void*(__fastcall*)(const char* name, bool value, unsigned int flags, const char* description);

	// Replay 1.20.4.7623265. Verified against the matching PC executable and the
	// live Replay IDB, not the symbol-rich Xbox build.
	constexpr uintptr_t kCLScreenUpdateRVA = 0x15CAA50;
	constexpr uint8_t kCLScreenUpdatePrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x28,
		0x80,
		0x3D,
		0x39,
		0x13,
		0x91,
		0x0D,
		0x00,
		0x75,
		0x3D,
		0x80,
		0x3D,
		0x2F,
		0x13,
		0x91,
		0x0D,
		0x00,
	};
	constexpr size_t kCLScreenUpdateStolen = sizeof(kCLScreenUpdatePrologue);

	// Main MP local-client frame dispatcher.  The Replay caller passes the local
	// client index in ECX; its virtual client update reaches
	// CL_MainMP_CheckForResend.  During a valid local-client-memory transition the
	// connection pointer can be null, but the connection state must be zero until
	// CL_ConnectAndPreloadMap reallocates it and sets state 1.
	constexpr uintptr_t kCLMainMPFrameDispatchRVA = 0x15C2BA0;
	constexpr uint8_t kCLMainMPFrameDispatchPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
	};
	constexpr size_t kCLMainMPFrameDispatchStolen = sizeof(kCLMainMPFrameDispatchPrologue);

	// Replay Omnvar table lifecycle.  BG_Omnvar_LoadTables does not clear the
	// existing table before registering omnvars/omnvars.csv + mode/gametype/level
	// tables.  The normal game-mode transition clears these tables separately.
	constexpr uintptr_t kBGOmnvarClearTablesRVA = 0xCD5500;
	constexpr uintptr_t kBGOmnvarLoadTablesRVA = 0xCD5C80;
	constexpr uint8_t kBGOmnvarLoadTablesPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x20,
		0x57,
		0x48,
		0x81,
		0xEC,
		0x40,
		0x04,
		0x00,
		0x00,
	};
	constexpr size_t kBGOmnvarLoadTablesStolen = sizeof(kBGOmnvarLoadTablesPrologue);
	constexpr uintptr_t kOmnvarTotalCountRVA = 0x5C48518;
	constexpr uintptr_t kOmnvarGameCountRVA = 0x5C48520;
	constexpr uintptr_t kOmnvarClientCountRVA = 0x5C48524;
	constexpr uintptr_t kOmnvarArchivedCountRVA = 0x5C4852C;
	constexpr uintptr_t kOmnvarPmoveCountRVA = 0x5C48530;
	// BG_Omnvar_LoadTables stores bit_length(game+client) at +0x38 and
	// bit_length(archived) at +0x3C.  Use the latter for diagnostics.
	constexpr uintptr_t kOmnvarArchivedBitsRVA = 0x5C4853C;

	constexpr uintptr_t kComDoMaxFPSWaitRVA = 0x12AA8C0;
	constexpr uintptr_t kSysIsMainThreadRVA = 0x12CF280;
	constexpr uint8_t kSysIsMainThreadPrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x28,
		0xFF,
		0x15,
		0x56,
		0x22,
		0x08,
		0x01,
		0x3B,
		0x05,
		0x90,
		0x01,
		0x2B,
		0x0C,
	};

	// R_InitDevice reaches one of these factories before command-list creation.
	// The AMD AGS path returns a result structure with device at +0.
	// Matched directly in the PC IDB; see research.md.
	constexpr uintptr_t kCreateD3DDeviceRVA = 0xEB35A0;
	constexpr uint8_t kCreateD3DDevicePrologue[] = {
		0x48,
		0xFF,
		0x25,
		0xE1,
		0x49,
		0x18,
		0x05,
	};
	constexpr uintptr_t kCreateAGSD3DDeviceRVA = 0xEB39A0;
	constexpr uint8_t kCreateAGSD3DDevicePrologue[] = {
		0x4C,
		0x8B,
		0xDC,
		0x53,
		0x48,
		0x83,
		0xEC,
		0x60,
		0x48,
		0x8B,
		0x05,
		0x49,
		0x0A,
		0xBA,
		0x04,
	};
	constexpr uintptr_t kIsAnyLocalServerRunningRVA = 0x12B0250;
	constexpr uint8_t kIsAnyLocalServerRunningPrologue[] = {
		0x48,
		0x8B,
		0x05,
		0x59,
		0x5D,
		0x0D,
		0x0C,
		0x48,
		0x85,
		0xC0,
		0x74,
		0x09,
		0x80,
		0x78,
		0x28,
		0x00,
		0x74,
		0x03,
		0xB0,
		0x01,
		0xC3,
		0x32,
		0xC0,
		0xC3,
	};
	// Read-only diagnostics for local client 0, from CL_MainMP_CheckForResend.
	constexpr uintptr_t kClient0ConnectionStateRVA = 0xEEF1288;
	constexpr uintptr_t kClient0ConnectionRVA = 0xEE85570;
	constexpr size_t kClientConnectionStateStride = 0x1B8;
	constexpr size_t kClientConnectionPointerStride = sizeof(uintptr_t);

	// CL_MainMP_SendCmd reads EE85570[index] and passes +41D90 to 15F0360.
	// That thunk enters Netchan_Transmit, whose +18 NetConnection reaches
	// NetConnection::GetAddress at 10D7950. A non-player host has no such link.
	constexpr size_t kClientConnectionNetchanOffset = 0x41D90;
	constexpr size_t kNetchanConnectionOffset = 0x18;
	constexpr uintptr_t kNetchanTransmitRVA = 0x12BB850;
	constexpr uint8_t kNetchanTransmitPrologue[] = {
		0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xB8, 0x0A, 0x00, 0x00,
		0x48, 0x8B, 0x05, 0x95, 0x8B, 0x79, 0x04,
	};

	// Replay 1.20 attach-time GDI clone bootstrap. The original startup path
	// patches the first byte to RET before the EXE CRT initializers execute.
	constexpr uintptr_t kGdiCloneBootstrapRVA = 0x3061A0;
	constexpr uint8_t kGdiCloneBootstrapPrologue[] = {
		0x40,
		0x57,
		0xB8,
		0xC0,
		0x33,
		0x00,
		0x00,
		0xE8,
		0x44,
		0x6D,
		0xEC,
		0x01,
		0x48,
		0x2B,
		0xE0,
	};

	// Replay frontend/Lua autostart path. All RVAs and leading bytes below were
	// verified against the live 1.20.4.7623265 Replay IDB.
	constexpr uintptr_t kLuaLoadfileFastfileRVA = 0x19BAEE0;
	constexpr uint8_t kLuaLoadfileFastfilePrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x6C,
		0x24,
		0x10,
		0x48,
		0x89,
		0x74,
		0x24,
		0x18,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0xDA,
	};
	constexpr size_t kLuaLoadfileFastfileStolen = sizeof(kLuaLoadfileFastfilePrologue);

	// Lua-facing wrapper registered in the Replay native function table as
	// DCJFBFGGHG.  It converts Lua arg #1 to a local client, accepts an optional
	// bool arg #2, and invokes UI_StartServer.
	constexpr uintptr_t kLuaUIStartServerWrapperRVA = 0x19E1830;
	constexpr uint8_t kLuaUIStartServerWrapperPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0xD9,
		0xE8,
		0x6E,
		0x20,
		0x6A,
		0x00,
	};

	constexpr uintptr_t kLuaGetTopRVA = 0x20838B0;
	constexpr uint8_t kLuaGetTopPrologue[] = {
		0x48,
		0x8B,
		0x41,
		0x28,
		0x48,
		0x2B,
		0x41,
		0x20,
		0x48,
		0xC1,
		0xF8,
		0x03,
		0xC3,
	};
	constexpr uintptr_t kLuaGetFieldRVA = 0x20836E0;
	constexpr uint8_t kLuaGetFieldPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x4D,
		0x8B,
		0xD0,
		0x48,
		0x8B,
		0xD9,
	};
	constexpr uintptr_t kLuaPCallRVA = 0x2083DA0;
	constexpr uint8_t kLuaPCallPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
	};
	constexpr uintptr_t kLuaPushBooleanRVA = 0x2083E80;
	constexpr uint8_t kLuaPushBooleanPrologue[] = {
		0x48,
		0x8B,
		0x41,
		0x28,
		0x45,
		0x33,
		0xC0,
		0x85,
		0xD2,
		0x41,
		0x0F,
		0x95,
		0xC0,
		0x49,
		0xFF,
		0xC0,
	};
	constexpr uintptr_t kLuaPushStringRVA = 0x2084120;
	constexpr uint8_t kLuaPushStringPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0xFA,
		0x48,
		0x8B,
		0xD9,
	};
	// Replay lua_settop; 0x2084420 is lua_remove and cannot clear a stack.
	constexpr uintptr_t kLuaSetTopRVA = 0x2084790;
	constexpr uint8_t kLuaSetTopPrologue[] = {
		0x40,
		0x53,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0xD9,
		0x85,
		0xD2,
		0x78,
		0x7A,
		0x4C,
		0x8B,
		0x41,
	};
	constexpr uintptr_t kLuaPushNumberRVA = 0x2084100;
	constexpr uint8_t kLuaPushNumberPrologue[] = {
		0x48,
		0x8B,
		0x41,
		0x28,
		0xF2,
		0x0F,
		0x11,
		0x08,
		0x48,
		0x8B,
		0x41,
		0x28,
		0x48,
		0x83,
		0xC0,
		0x08,
	};

	// LUI_OpenMenu: matched from game_test 0x142781440 to Replay 0x7FF7AB17BDB0.
	// The extended signature is unique in both IDBs; see research.md.
	constexpr uintptr_t kLUIOpenMenuRVA = 0x1B9BDB0;
	constexpr uint8_t kLUIOpenMenuPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x30,
		0x41,
		0x8B,
		0xF1,
		0x41,
		0x8B,
		0xD8,
		0x48,
		0x8B,
		0xFA,
		0xE8,
		0x83,
		0xAE,
		0x1C,
		0xFF,
		0x8B,
		0xC8,
		0x48,
		0x8B,
		0xD7,
		0x85,
		0xDB,
		0x74,
		0x3F,
		0x85,
		0xF6,
		0x74,
		0x1B,
	};
	// RIP-relative global loaded by all three LUI_OpenMenu branches.
	constexpr uintptr_t kLUILuaVMRVA = 0x11868880;

	// Scene-ready test (state >= 5), not the frontend-mode flag used by LUI.
	constexpr uintptr_t kIsFrontEndRVA = 0x10C6BD0;
	constexpr uint8_t kIsFrontEndPrologue[] = {
		0x83,
		0x3D,
		0xD9,
		0x52,
		0x42,
		0x0B,
		0x05,
		0x0F,
		0x9D,
		0xC0,
		0xC3,
	};
	// Com_FrontEnd_IsInFrontEnd; Engine.DDJFBBJAIG calls this same getter.
	constexpr uintptr_t kIsInFrontEndRVA = 0x10C67A0;
	constexpr uint8_t kIsInFrontEndPrologue[] = {
		0x0F,
		0xB6,
		0x05,
		0xC5,
		0x56,
		0x42,
		0x0B,
		0xC3,
	};
	constexpr uintptr_t kDvarGetBoolRVA = 0x13E6820;
	constexpr uint8_t kDvarGetBoolPrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x28,
		0xE8,
		0x97,
		0xFE,
		0xFF,
		0xFF,
		0x8B,
		0xC8,
		0xE8,
		0x90,
		0xFA,
		0xFF,
		0xFF,
	};
	constexpr uintptr_t kDvarGetStringRVA = 0x13E69B0;
	constexpr uint8_t kDvarGetStringPrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x28,
		0xE8,
		0x07,
		0xFD,
		0xFF,
		0xFF,
		0x8B,
		0xC8,
		0xE8,
		0x00,
		0xF9,
		0xFF,
		0xFF,
	};
	// Dvar_SetBool_Internal, also called by Replay UI_StartServer for systemlink_host.
	constexpr uintptr_t kDvarSetBoolInternalRVA = 0x13E8B50;
	constexpr uint8_t kDvarSetBoolInternalPrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x38,
		0x80,
		0x79,
		0x1D,
		0x00,
		0x75,
		0x06,
		0x88,
		0x54,
		0x24,
		0x20,
		0xEB,
		0x19,
		0x84,
		0xD2,
		0x4C,
		0x8D,
		0x05,
		0x23,
		0x42,
		0x35,
		0x01,
		0x48,
		0x8D,
		0x05,
		0xC8,
		0x3C,
		0x35,
		0x01,
	};
	constexpr uintptr_t kEnterCriticalRVA = 0x13F54F0;
	constexpr uint8_t kEnterCriticalPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
	};
	constexpr uintptr_t kLeaveCriticalRVA = 0x13F55F0;
	constexpr uint8_t kLeaveCriticalPrologue[] = {
		0x8B,
		0x05,
		0x1A,
		0xD8,
		0xAC,
		0x11,
		0x48,
		0x63,
		0xD1,
		0x65,
		0x48,
		0x8B,
		0x0C,
		0x25,
		0x58,
		0x00,
		0x00,
		0x00,
	};
	constexpr uintptr_t kConsoleBufferRVA = 0xD216D70;

	// Replay LAN/session seams, structurally mapped from the symbol-rich IW8 build
	// and re-proven in the exact 1.20.4.7623265 Replay image.
	constexpr uintptr_t kNETCreateSessionRVA = 0x12BD480;
	constexpr uint8_t kNETCreateSessionPrologue[] = {
		0x40,
		0x53,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0xD9,
	};
	constexpr uintptr_t kPartyStartLANServerJoinRVA = 0x1608270;
	constexpr uint8_t kPartyStartLANServerJoinPrologue[] = {
		0x40,
		0x53,
		0x48,
		0x83,
		0xEC,
		0x40,
		0x48,
		0x89,
		0x6C,
		0x24,
		0x50,
	};
	constexpr uintptr_t kPartyAtomicAcceptInviteMPRVA = 0x1606BE0;
	constexpr uint8_t kPartyAtomicAcceptInviteMPPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x6C,
		0x24,
		0x10,
	};
	constexpr uintptr_t kAddConsoleTextRVA = 0x15C1C20;
	constexpr uint8_t kAddConsoleTextPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
	};

	// Dedicated-server LAN ingress/join diagnostics. These are exact Replay
	// 1.20.4.7623265 mappings, structurally matched against the symbol-rich IW8 build.
	// Replay SvGameGlobalsMP::sendGameStates. Proven from the exact Replay
	// SV_ClientMP_ExecuteMessage path: RVA 0x136FF46 loads ms_svGameGlobals,
	// RVA 0x136FF4D tests +0x6D1C, RVA 0x136FF65 stores connection.IsLocal(),
	// and RVA 0x136FFBF gates the gamestate resend path on the same member.
	constexpr uintptr_t kSvGameGlobalsPtrRVA = 0xE17FE00;
	constexpr size_t kSvGameGlobalsSendGameStatesOffset = 0x6D1C;
	constexpr uintptr_t kSVClientMPExecuteMessageRVA = 0x136FE70;
	constexpr uintptr_t kSVSendClientGameStateRVA = 0x1675160;
	constexpr uintptr_t kSVMainMPConnectionlessPacketRVA = 0x137D0F0;
	constexpr uint8_t kSVMainMPConnectionlessPacketPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x18,
		0x48,
		0x89,
		0x74,
		0x24,
		0x20,
		0x55,
		0x57,
		0x41,
		0x56,
	};
	constexpr uintptr_t kSVClientMPGetChallengeRVA = 0x13709F0;
	constexpr uint8_t kSVClientMPGetChallengePrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x10,
		0x48,
		0x89,
		0x6C,
		0x24,
		0x18,
		0x48,
		0x89,
		0x74,
		0x24,
		0x20,
		0x57,
	};
	constexpr uintptr_t kSVClientMPDirectConnectRVA = 0x136EB20;
	constexpr uint8_t kSVClientMPDirectConnectPrologue[] = {
		0x40,
		0x55,
		0x56,
		0x57,
		0x41,
		0x57,
		0x48,
		0x8D,
		0xAC,
		0x24,
		0xA8,
		0xFB,
		0xFF,
		0xFF,
	};
	// Stock MP bot lifecycle. Mapped by comparing the symbol-rich IW8 wrappers/call flow
	// against Replay; do not use cross-build byte identity as validation.
	constexpr uintptr_t kSVClientMPAddBotRVA = 0x136E210;
	constexpr uintptr_t kSVClientMPSpawnBotOrTestClientRVA = 0x1373640;
	// Verified Replay GSC lifecycle; lookup tables exist only until EndLoadScripts.
	// See replay_bot_script_lifecycle.json. These are Win64 native calls, not Lua.
	constexpr uintptr_t kScriptContextServerRVA = 0x12E0E70;
	constexpr uint8_t kScriptContextServerPrologue[] = { 0x48, 0x8D, 0x05, 0x89, 0x0C, 0x2B, 0x0C, 0xC3 };
	constexpr uintptr_t kScrEndLoadScriptsRVA = 0x1316FE0;
	constexpr uint8_t kScrEndLoadScriptsPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0xF9,
	};
	constexpr uintptr_t kScrGetFunctionHandleRVA = 0x1317270;
	constexpr uint8_t kScrGetFunctionHandlePrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x6C,
		0x24,
		0x10,
		0x48,
		0x89,
		0x74,
		0x24,
		0x18,
		0x57,
	};
	constexpr uintptr_t kScrAddStructRVA = 0x1322DC0;
	constexpr uint8_t kScrAddStructPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0xF9,
		0xE8,
		0xCE,
		0x92,
		0xFF,
		0xFF,
	};
	constexpr uintptr_t kScrAddUndefinedRVA = 0x1322E50;
	constexpr uint8_t kScrAddUndefinedPrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x28,
		0xE8,
		0x27,
		0xF4,
		0xFF,
		0xFF,
		0xC6,
		0x40,
		0x08,
		0x00,
		0x48,
		0x83,
		0xC4,
		0x28,
		0xC3,
	};
	constexpr uintptr_t kScrAddStringRVA = 0x1322D90;
	constexpr uint8_t kScrAddStringPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0xFA,
	};
	constexpr uintptr_t kGScrExecEntThreadRVA = 0x1257D70;
	constexpr uint8_t kGScrExecEntThreadPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x50,
	};
	constexpr uintptr_t kScrFreeThreadRVA = 0x13242E0;
	constexpr uint8_t kScrFreeThreadPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0xF9,
		0x8B,
		0xDA,
	};
	// Read existing script fields and notify on the server-command thread. The
	// string lookup borrows an existing interned ID (no reference to release).
	constexpr uintptr_t kScrFindEntityIdRVA = 0x1320940;
	constexpr uint8_t kScrFindEntityIdPrologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x0F, 0xB6, 0xC0 };
	constexpr uintptr_t kScrFindVariableRVA = 0x131CB90;
	constexpr uint8_t kScrFindVariablePrologue[] = { 0x6B, 0xC2, 0x65, 0x41, 0x03, 0xC0, 0x44, 0x0F, 0xB7, 0xC8 };
	constexpr uintptr_t kSLFindStringRVA = 0x131AB50;
	constexpr uint8_t kSLFindStringPrologue[] = { 0x48, 0xC7, 0xC2, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0xFF, 0xC2 };
	constexpr uintptr_t kSLConvertToStringRVA = 0x131AA20;
	constexpr uint8_t kSLConvertToStringPrologue[] = { 0x85, 0xC9 };
	constexpr uintptr_t kGScrNotifyRVA = 0x1259B30;
	constexpr uint8_t kGScrNotifyPrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x57,
		0x48,
		0x83,
		0xEC,
		0x30,
	};
	// Replay Lobby bot setters all call this bool(void) predicate. Stock requires
	// the MP feature plus privateMatch/systemlink/competition permission.
	// See ida-reports/replay_online_dedi_paths.json; no later-version address.
	constexpr uintptr_t kBGAreBotsAllowedRVA = 0x11095E0;
	constexpr uint8_t kBGAreBotsAllowedPrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x28,
		0xB9,
		0x02,
		0x00,
		0x00,
		0x00,
		0xE8,
		0x92,
		0xF3,
		0xFB,
		0xFF,
	};
	// Server AI update/allocation gate inlines the permission predicate above.
	// Enabling the Lobby predicate alone leaves connected bots without AI updates.
	constexpr uintptr_t kBGBotsEnabledRVA = 0x1109560;
	constexpr uint8_t kBGBotsEnabledPrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x28,
		0xB9,
		0x02,
		0x00,
		0x00,
		0x00,
		0xE8,
		0x12,
		0xF4,
		0xFB,
		0xFF,
	};
	constexpr uintptr_t kBGGetBotsConnectTypeRVA = 0x110E320;
	constexpr uint8_t kBGGetBotsConnectTypePrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x28,
		0xB9,
		0x02,
		0x00,
		0x00,
		0x00,
		0xE8,
		0x52,
		0xA6,
		0xFB,
		0xFF,
	};
	constexpr uintptr_t kBGSetBotsConnectTypeRVA = 0x1119AE0;
	constexpr uint8_t kBGSetBotsConnectTypePrologue[] = {
		0x40,
		0x53,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x0F,
		0xB6,
		0xD9,
		0x48,
		0x8B,
		0x0D,
		0x00,
		0xCD,
		0x26,
		0x0C,
	};
	// Shared active loadout selector used by the player-data delta allow-mask.
	// 3 = ranked loadouts, 4 = private loadouts. Offline network startup on a
	// playlist host must not select a different group from its online clients.
	constexpr uintptr_t kGetLoadoutStatsGroupRVA = 0x10CA350;
	// Com_GetRequiredStatPackets repeats the loadout selection inline; changing
	// GetLoadoutStatsGroup alone only fixes deltas, not the initial connection.
	constexpr uintptr_t kGetRequiredStatPacketsRVA = 0x10CA5D0;
	constexpr uint8_t kGetRequiredStatPacketsPrologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xDB, 0xE8, 0xF3, 0xC5, 0xFF, 0xFF,
		0x84, 0xC0, 0x0F, 0x85, 0xDA, 0x00, 0x00, 0x00 };
	constexpr uint8_t kGetLoadoutStatsGroupPrologue[] = {
		0x40,
		0x53,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x48,
		0x8B,
		0x05,
		0x9B,
		0xC4,
		0x2B,
		0x0C,
	};
	// SV_MainMP_GetServerLobby maps the current session to its owning PartyData.
	// Its stock branch reads systemlink directly, independently of the lobby helper.
	constexpr uintptr_t kSVGetServerLobbyRVA = 0x137D670;
	constexpr uint8_t kSVGetServerLobbyPrologue[] = {
		0x48,
		0x83,
		0xEC,
		0x28,
		0xE8,
		0x57,
		0xA2,
		0xFF,
		0xFF,
		0x84,
		0xC0,
		0x48,
		0x8D,
		0x15,
		0x9E,
		0xF3,
		0xE2,
		0x0C,
	};
	constexpr uintptr_t kPartyAtomicHostHandleJoinRequestRVA = 0x11936F0;
	constexpr uint8_t kPartyAtomicHostHandleJoinRequestPrologue[] = {
		0x40,
		0x55,
		0x56,
		0x57,
		0x41,
		0x54,
		0x41,
		0x56,
		0x48,
		0x8D,
		0xAC,
		0x24,
		0x10,
		0xFF,
		0xFF,
		0xFF,
	};
	constexpr uintptr_t kPartyAtomicHostSendAcceptMessageRVA = 0x1197500;
	constexpr uint8_t kPartyAtomicHostSendAcceptMessagePrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x6C,
		0x24,
		0x10,
		0x48,
		0x89,
		0x74,
		0x24,
		0x18,
		0x57,
	};
	constexpr uintptr_t kPartyAtomicHostSendJoinFailedResponseRVA = 0x11975D0;
	constexpr uint8_t kPartyAtomicHostSendJoinFailedResponsePrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x08,
		0x48,
		0x89,
		0x74,
		0x24,
		0x10,
		0x4C,
		0x89,
		0x4C,
		0x24,
		0x20,
		0x57,
	};
	constexpr uintptr_t kSVCmdArgvRVA = 0x1298B10;
	constexpr uint8_t kSVCmdArgvPrologue[] = {
		0x48,
		0x63,
		0x05,
		0x89,
		0x91,
		0xF7,
		0x0B,
		0x48,
		0x8D,
		0x15,
		0x82,
		0x91,
		0xF7,
		0x0B,
	};
	constexpr uintptr_t kMSGBeginReadingRVA = 0x12B84A0;
	constexpr uint8_t kMSGBeginReadingPrologue[] = {
		0x33,
		0xC0,
		0x89,
		0x01,
		0x48,
		0x89,
		0x41,
		0x24,
		0x89,
		0x41,
		0x34,
		0xC3,
	};
	constexpr uintptr_t kMSGReadLongRVA = 0x12B9380;
	constexpr uint8_t kMSGReadLongPrologue[] = {
		0x40,
		0x56,
		0x57,
		0x41,
		0x56,
		0x48,
		0x83,
		0xEC,
		0x20,
		0x8B,
		0x51,
		0x28,
		0x48,
		0x8B,
		0xF9,
	};
	constexpr uintptr_t kMSGReadStringLineRVA = 0x12B9670;
	constexpr uint8_t kMSGReadStringLinePrologue[] = {
		0x48,
		0x89,
		0x5C,
		0x24,
		0x10,
		0x48,
		0x89,
		0x6C,
		0x24,
		0x18,
		0x48,
		0x89,
		0x74,
		0x24,
		0x20,
		0x57,
	};

	// Live_IsInSystemlinkLobby checks only the current values of systemlink and
	// com_lan_lobby_enabled. It does not report menu visibility or party readiness.
	constexpr uintptr_t kSystemLinkPredicateRVA = 0x1528420;
	constexpr uint8_t kSystemLinkPredicatePrologue[] = {
		0x48,
		0x8B,
		0x05,
		0x61,
		0xE3,
		0xE5,
		0x0B,
		0x80,
		0x78,
		0x28,
		0x00,
		0x74,
		0x10,
		0x48,
		0x8B,
		0x05,
		0x64,
		0xE3,
		0xE5,
		0x0B,
		0x80,
		0x78,
		0x28,
		0x00,
		0x74,
		0x03,
		0xB0,
		0x01,
		0xC3,
		0x32,
		0xC0,
		0xC3,
	};
	constexpr uintptr_t kSystemLinkDvarRVA = 0xD386788;
	constexpr uintptr_t kLanLobbyEnabledDvarRVA = 0xD386798;
	constexpr size_t kDvarTypeOffset = 0x1D;
	constexpr size_t kDvarCurrentOffset = 0x28;
	constexpr uintptr_t kGetSystemLinkPartyRVA = 0x118C010;
	constexpr uint8_t kGetSystemLinkPartyPrologue[] = {
		0x83,
		0x3D,
		0xB9,
		0x63,
		0x65,
		0x0B,
		0x00,
		0x74,
		0x11,
		0x83,
		0x3D,
		0xAC,
		0x63,
		0x65,
		0x0B,
		0x00,
	};
	constexpr size_t kPartyStartRequestedOffset = 0x4BFA9;
	// Fields read by Party_RequestStartMatch's host/party readiness checks.
	constexpr size_t kPartyHostOffset = 0x4BF48;
	constexpr size_t kPartyInPartyOffset = 0x4BF4C;
	constexpr size_t kPartyActiveOffset = 0x4BF50;

	using Lua_LoadFileFastFile_t = int(__fastcall*)(void* luaState, const char* filename);
	using Lua_GetTop_t = int(__fastcall*)(void* luaState);
	using Lua_GetField_t = void(__fastcall*)(void* luaState, int index, const char* key);
	using Lua_PCall_t = int(__fastcall*)(void* luaState, int argumentCount, int resultCount, int errorFunction);
	using Lua_PushBoolean_t = void(__fastcall*)(void* luaState, int value);
	using Lua_PushString_t = void(__fastcall*)(void* luaState, const char* value);
	using Lua_SetTop_t = void(__fastcall*)(void* luaState, int index);
	using Lua_PushNumber_t = void(__fastcall*)(void* luaState, double value);
	using Lua_UIStartServer_t = int(__fastcall*)(void* luaState);
	using LUI_OpenMenu_t = void(__fastcall*)(int localClientNum, const char* menuName, int isPopup, int isModal, int isExclusive);
	using IsFrontEnd_t = bool (*)();
	using Dvar_GetBool_t = bool(__fastcall*)(const char* name);
	using Dvar_GetString_t = const char*(__fastcall*)(const char* name);
	using Dvar_SetBool_Internal_t = void(__fastcall*)(const void* dvar, bool value);
	using CriticalSection_t = void(__fastcall*)(int criticalSection);
	using SystemLinkPredicate_t = bool (*)();
	using GetSystemLinkParty_t = void* (*)();

	using CL_Screen_Update_t = void (*)();
	using CL_MainMP_FrameDispatch_t = uintptr_t(__fastcall*)(int localClientNum);
	using BG_Omnvar_ClearTables_t = void (*)();
	using BG_Omnvar_LoadTables_t = void(__fastcall*)(unsigned char gameMode, const char* gameType, const char* mapName);
	using Com_DoMaxFPSWait_t = void (*)();
	using Sys_IsMainThread_t = bool (*)();

	// Noclip and movement state bindings for MW2019 Replay (1.20.4.7623265).
	// RVA 0x11E801B tests bit 0 of gclient_t::flags (+0x5DD0) and sets ps.pm_type (+0x0C) = 2 (PM_NOCLIP).
	constexpr uintptr_t kNoclipMovementFlagsRVA = 0x11E801B;
	constexpr uint8_t kNoclipMovementFlagsPrologue[] = { 0x8B, 0x86, 0xD0, 0x5D, 0x00, 0x00, 0xA8, 0x01, 0x74, 0x0C, 0xC7, 0x46, 0x0C, 0x02,
		0x00, 0x00, 0x00 };
	constexpr uintptr_t kCmdClientCommandRVA = 0x120B6A0;
	constexpr uint8_t kCmdClientCommandPrologue[] = { 0x4C, 0x8B, 0xDC, 0x55, 0x56, 0x41, 0x56, 0x41, 0x57, 0x49, 0x8D, 0xAB, 0x78, 0xFC,
		0xFF, 0xFF };
	constexpr uintptr_t kGEntitiesPtrRVA = 0xBC20F00;
	constexpr size_t kGEntityStride = 0x5A0;
	constexpr size_t kGEntityClientOffset = 0x150;
	constexpr size_t kGClientPmTypeOffset = 0x0C;
	constexpr size_t kGClientNoclipFlagsOffset = 0x5DD0;
	constexpr uint32_t kNoclipFlagBit = 0x1;
	constexpr int kPmTypeNormal = 0;
	constexpr int kPmTypeNoclip = 2;
}

// Replay-only GSC mappings. Evidence: ida-reports/gsc_port.
namespace game::addresses
{
	namespace gsc
	{
		inline constexpr uintptr_t kServerContextRva = 0xD591B00;
	}

	namespace gsc_api
	{
		inline constexpr uintptr_t ScriptContext_Server = 0x12E0E70;
		inline constexpr uintptr_t Scr_BeginLoadScripts = 0x1316DE0;
		inline constexpr uintptr_t Scr_EndLoadScripts = 0x1316FE0;
		inline constexpr uintptr_t Scr_LoadScript = 0x1317400;
		inline constexpr uintptr_t Scr_GetFunctionHandle = 0x1317270;
		inline constexpr uintptr_t Scr_ExecThread = 0x1322C30;
		inline constexpr uintptr_t Scr_ExecThreadInternal = 0x13241B0;
		inline constexpr uintptr_t Scr_FreeThread = 0x13242E0;
		inline constexpr uintptr_t Scr_AddInt = 0x1322D40;
		inline constexpr uintptr_t Scr_AddFloat = 0x1322CE0;
		inline constexpr uintptr_t Scr_AddString = 0x1322D90;
		inline constexpr uintptr_t Scr_AddUndefined = 0x1322E50;
		inline constexpr uintptr_t Scr_AddVector = 0x1322EB0;
			// Replay 1.20.4 Scr_AddBool (used by the native physics_querypoint
			// GSC builtin at 0x10A00D0); not the 1.64.5 address.
			inline constexpr uintptr_t Scr_AddBool = 0x1322B60;
			// Replay's Physics_QueryPointContents, verified native counterpart
			// of source 1.64.5 RVA 0x305EA50. Args: world, position,
			// ignoredEntity, contentsMask. Return: matching contents bits.
			inline constexpr uintptr_t Physics_QueryPointContents = 0x1098C50;
		inline constexpr uintptr_t Scr_AddEntityNum = 0x1322BB0;
		inline constexpr uintptr_t Scr_AddArray = 0x1322B10;
		inline constexpr uintptr_t Scr_MakeArray = 0x1325DC0;
		inline constexpr uintptr_t Scr_MakeStruct = 0x1322DC0;
		inline constexpr uintptr_t Scr_AddStructField = 0x1322E00;
		inline constexpr uintptr_t Scr_GetNumParam = 0x1324FF0;
		inline constexpr uintptr_t Scr_GetInt = 0x1324F00;
		inline constexpr uintptr_t Scr_GetFloat = 0x1324C00;
		inline constexpr uintptr_t Scr_GetString = 0x13254D0;
		inline constexpr uintptr_t Scr_GetConstString = 0x13247F0;
		inline constexpr uintptr_t Scr_GetType = 0x1325580;
		inline constexpr uintptr_t Scr_GetPointerType = 0x1325220;
		inline constexpr uintptr_t Scr_GetVector = 0x1325740;
		inline constexpr uintptr_t Scr_GetEntityRef = 0x1324A80;
		inline constexpr uintptr_t Scr_NotifyNum = 0x1325F20;
		inline constexpr uintptr_t Scr_NotifyLevel = 0x1325F00;
		inline constexpr uintptr_t Scr_NotifyId = 0x1325E20;
		inline constexpr uintptr_t SL_GetString = 0x131AE30;
		inline constexpr uintptr_t SL_ConvertToString = 0x131AA20;
		inline constexpr uintptr_t Scr_ClearOutParams = 0x1323410;
		inline constexpr uintptr_t Scr_IncInParam = 0x1322280;
		inline constexpr uintptr_t GetObjectType = 0x131D2C0;
		inline constexpr uintptr_t RemoveRefToObject = 0x131D6A0;
		inline constexpr uintptr_t RemoveRefToValue = 0x131D7C0;
		inline constexpr uintptr_t AddRefToObject = 0x131BD00;
		inline constexpr uintptr_t AllocObject = 0x131C0A0;
		inline constexpr uintptr_t AllocThread = 0x131C120;
		inline constexpr uintptr_t Scr_AllocArray = 0x131E130;
		inline constexpr uintptr_t GetVariable = 0x131D3D0;
		inline constexpr uintptr_t GetNewArrayVariable = 0x131CE50;
		inline constexpr uintptr_t SetNewVariableValue = 0x1321CF0;
		inline constexpr uintptr_t FindFirstSibling = 0x131CAC0;
		inline constexpr uintptr_t FindNextSibling = 0x131CAE0;
		inline constexpr uintptr_t GetVariableName = 0x131D340;
		inline constexpr uintptr_t Scr_GetArraySize = 0x131CE00;
		inline constexpr uintptr_t Scr_EvalVariable = 0x13205E0;
		inline constexpr uintptr_t EntityRef_layout = 0x1321070;
		inline constexpr uintptr_t VM_Execute = 0x132BA60;
		inline constexpr uintptr_t VM_Interpreter = 0x13272F0;
		inline constexpr uintptr_t Scr_SetBuiltinTables_Server = 0x12E0EE0;
		inline constexpr uintptr_t Scr_GetFunctionFromTable = 0x12E06C0;
		inline constexpr uintptr_t Scr_GetMethodFromTable = 0x12E0750;
		inline constexpr uintptr_t GScriptMP_GetFunction = 0x126F160;
		inline constexpr uintptr_t GScriptMP_GetMethod = 0x126F2C0;
		inline constexpr uintptr_t Dvar_FindVarByName = 0x13E63A0;
		inline constexpr uintptr_t Dvar_GetString = 0x13E69B0;
		inline constexpr uintptr_t Dvar_GetVariantString = 0x13E6B00;
		inline constexpr uintptr_t Dvar_GetIntSafe = 0x13E6960;
		inline constexpr uintptr_t Dvar_GetBoolSafe = 0x13E6820;
		inline constexpr uintptr_t Dvar_GetFloatSafe = 0x13E68C0;
		inline constexpr uintptr_t Weapon_AddParam = 0xE984D0;
		inline constexpr uintptr_t Weapon_GetWeapon = 0xE98A60;
		inline constexpr uintptr_t Weapon_IsAlternate = 0xE98B80;
		inline constexpr uintptr_t GScr_FreeEntity = 0x12581A0;
		inline constexpr uintptr_t GScr_Notify = 0x1259B30;
		inline constexpr uintptr_t Scr_FreeEntityNum = 0x1320CB0;
		inline constexpr uintptr_t native_functions = 0xD5B7350;
		inline constexpr uintptr_t native_methods = 0xD5B9410;
		// getentitynumber callback 0x1232E40: bounds 0x800 and live-byte table.
		inline constexpr uintptr_t entity_in_use_pointer = 0xBC20F08;
		inline constexpr size_t entity_capacity = 2048;
		// DirectConnect 0x136EB20 and UserinfoChanged 0x13750E0.
		inline constexpr uintptr_t clients = 0xE17F690;
		inline constexpr uintptr_t client_count = 0xE17FCD0;
		inline constexpr uintptr_t persistent_globals = 0xE17FE10;
		inline constexpr size_t client_capacity = 200;
		inline constexpr size_t client_state = 8;
		inline constexpr size_t client_name = 0x534;
		inline constexpr size_t client_name_capacity = 36;
		inline constexpr size_t persistent_frontend = 0x72;
		inline constexpr uint32_t stock_function_base = 1;
		inline constexpr uint32_t stock_function_count = 0x418;
		inline constexpr uint32_t stock_method_base = 0x8000;
		inline constexpr uint32_t stock_method_count = 0x787;
		inline constexpr uint32_t plugin_capacity = 256;
		inline constexpr uint32_t invocation_builtin_id = 0x519;
		// Main interpreter table 0x132B578: these are opcode identities.
		inline constexpr uint8_t op_checkclearparams = 0x4B;
		inline constexpr uint8_t op_callbuiltin0 = 0x1D;
		inline constexpr uint8_t op_return = 0x2F;
		inline constexpr uint8_t weapon_entity_class = 10;
		inline constexpr size_t weapon_descriptor_size = 60;
		inline constexpr size_t weapon_pool_stride = 62;
		// Fixed cross-DLL snapshot ABI; native descriptor has only 60 bytes.
		inline constexpr size_t weapon_snapshot_alternate = 62;
		inline constexpr size_t weapon_snapshot_reserved = 63;
		// AllocObject 0x131C0A0 reads the free parent ID at [parents+4].
		inline constexpr size_t parent_free_head_offset = 4;
		// GetNewArrayVariable / GetVariableName use the 24-bit integer key domain.
		inline constexpr uint32_t array_integer_key_base = 0x800000;

		namespace layout
		{
			inline constexpr size_t stack_top = 0xB9C8;
			inline constexpr size_t stack_max = 0xB9B0;
			inline constexpr size_t stack_sentinel = 0xB998;
			inline constexpr size_t frame_count = 0xB9B8;
			inline constexpr size_t frame_pointer = 0xB9C0;
			inline constexpr size_t inparam_count = 0xB9D4;
			inline constexpr size_t outparam_count = 0xB9D8;
			inline constexpr size_t instance = 0x25820;
			inline constexpr size_t parents = 0x34A8;
			inline constexpr size_t siblings = 0x34B0;
			inline constexpr size_t variable_hash = 0x34B8;
			inline constexpr size_t children = 0x34C0;
			inline constexpr size_t error_flag = 0x34D0;
			inline constexpr size_t error_message = 0x34D8;
			inline constexpr size_t error_index = 0x34E0;
			inline constexpr size_t level_object = 0x34FC;
			inline constexpr size_t execute_depth = 0x3518;
			inline constexpr size_t program_buffer = 0x3548;
			inline constexpr size_t loaded_scripts = 0xC60;
			inline constexpr size_t exports = 0xC64;
			inline constexpr size_t pending_scripts = 0xC68;
			inline constexpr size_t program_size = 0xC6C;
			inline constexpr size_t loading = 0xC70;
			inline constexpr size_t function_base = 0xC74;
			inline constexpr size_t function_end = 0xC78;
			inline constexpr size_t function_count = 0xC7C;
			inline constexpr size_t method_base = 0xC80;
			inline constexpr size_t method_end = 0xC84;
			inline constexpr size_t method_count = 0xC88;
			inline constexpr size_t functions = 0xC90;
			inline constexpr size_t methods = 0xC98;
			inline constexpr size_t dvar_type = 0x1D;
			inline constexpr size_t dvar_current = 0x28;
		}

		struct builtin_address
		{
			bool method;
			uint32_t id;
			uintptr_t rva;
		};

		inline constexpr builtin_address builtin_addresses[] = {
			{ false, 0x1, 0xcdf680 },
			{ false, 0x2, 0xcdf480 },
			{ false, 0x3, 0x122eea0 },
			{ false, 0x4, 0x1263160 },
			{ false, 0x5, 0x1262e80 },
			{ false, 0x6, 0x1262e10 },
			{ false, 0x7, 0x1263180 },
			{ false, 0xA, 0xce0ee0 },
			{ false, 0xB, 0xce0f10 },
			{ false, 0xC, 0xce1210 },
			{ false, 0xD, 0xce0f60 },
			{ false, 0xE, 0xce11b0 },
			{ false, 0xF, 0xce10c0 },
			{ false, 0x10, 0xce1040 },
			{ false, 0x11, 0x1233f90 },
			{ false, 0x12, 0x1264d90 },
			{ false, 0x13, 0xcdf400 },
			{ false, 0x14, 0xcdf500 },
			{ false, 0x15, 0xce0d20 },
			{ false, 0x16, 0xce0c90 },
			{ false, 0x17, 0xce1630 },
			{ false, 0x18, 0x122eb10 },
			{ false, 0x19, 0x122ea90 },
			{ false, 0x1A, 0x122ecb0 },
			{ false, 0x1B, 0x13d2270 },
			{ false, 0x1C, 0x12694b0 },
			{ false, 0x1D, 0x1366bc0 },
			{ false, 0x1E, 0x1365f50 },
			{ false, 0x1F, 0x1365f60 },
			{ false, 0x20, 0x1366210 },
			{ false, 0x21, 0x13662d0 },
			{ false, 0x22, 0x1366630 },
			{ false, 0x23, 0x13666b0 },
			{ false, 0x24, 0x1366720 },
			{ false, 0x25, 0x1366890 },
			{ false, 0x26, 0x1366c00 },
			{ false, 0x27, 0x1366dc0 },
			{ false, 0x28, 0x1366e50 },
			{ false, 0x29, 0x1367120 },
			{ false, 0x2A, 0x1247680 },
			{ false, 0x2C, 0x12319e0 },
			{ false, 0x2D, 0x1231600 },
			{ false, 0x2E, 0x1231860 },
			{ false, 0x30, 0xce0910 },
			{ false, 0x31, 0x1262980 },
			{ false, 0x32, 0x123c3f0 },
			{ false, 0x33, 0x1262a80 },
			{ false, 0x34, 0xcdf780 },
			{ false, 0x3D, 0xcdf890 },
			{ false, 0x40, 0x1265810 },
			{ false, 0x41, 0x1264e40 },
			{ false, 0x42, 0x12633e0 },
			{ false, 0x43, 0xce0060 },
			{ false, 0x44, 0xce12f0 },
			{ false, 0x47, 0x122ea70 },
			{ false, 0x48, 0xcdf360 },
			{ false, 0x49, 0x1004720 },
			{ false, 0x4A, 0x10ebe70 },
			{ false, 0x4B, 0x122e3a0 },
			{ false, 0x4C, 0x122e2b0 },
			{ false, 0x4D, 0x1005780 },
			{ false, 0x4E, 0x126a540 },
			{ false, 0x4F, 0x12330f0 },
			{ false, 0x52, 0x1233720 },
			{ false, 0x53, 0x122ea50 },
			{ false, 0x54, 0xcdfca0 },
			{ false, 0x55, 0xcdfd30 },
			{ false, 0x56, 0xcdfdb0 },
			{ false, 0x57, 0xcdfe30 },
			{ false, 0x59, 0x123af60 },
			{ false, 0x5A, 0x12330e0 },
			{ false, 0x5E, 0x1233630 },
			{ false, 0x5F, 0x12696c0 },
			{ false, 0x60, 0x1265440 },
			{ false, 0x61, 0x1262d80 },
			{ false, 0x62, 0xcdf7d0 },
			{ false, 0x63, 0x11df2d0 },
			{ false, 0x64, 0xcdf5d0 },
			{ false, 0x65, 0xcdf730 },
			{ false, 0x66, 0x1231c70 },
			{ false, 0x67, 0x1231cb0 },
			{ false, 0x68, 0x1231cc0 },
			{ false, 0x69, 0x1231ca0 },
			{ false, 0x6A, 0x1231c80 },
			{ false, 0x6B, 0x1231c90 },
			{ false, 0x6C, 0x1261bb0 },
			{ false, 0x6D, 0x126d200 },
			{ false, 0x6E, 0x126d3c0 },
			{ false, 0x6F, 0x126c950 },
			{ false, 0x70, 0x126cb60 },
			{ false, 0x71, 0x126cd00 },
			{ false, 0x72, 0x11df420 },
			{ false, 0x73, 0xfec280 },
			{ false, 0x74, 0x124b3f0 },
			{ false, 0x75, 0x124b530 },
			{ false, 0x76, 0x1233f00 },
			{ false, 0x77, 0xce1ee0 },
			{ false, 0x78, 0x1265450 },
			{ false, 0x7A, 0x1243a90 },
			{ false, 0x7B, 0x1243a80 },
			{ false, 0x7C, 0x1265550 },
			{ false, 0x7D, 0x11df570 },
			{ false, 0x80, 0x126a5e0 },
			{ false, 0x81, 0x1230570 },
			{ false, 0x82, 0x1230640 },
			{ false, 0x83, 0x12311b0 },
			{ false, 0x84, 0x1230590 },
			{ false, 0x85, 0x1231480 },
			{ false, 0x86, 0x1230810 },
			{ false, 0x87, 0x1230a50 },
			{ false, 0x88, 0x1231060 },
			{ false, 0x89, 0x1231310 },
			{ false, 0x8A, 0x1230f60 },
			{ false, 0x8C, 0xe92d70 },
			{ false, 0x8D, 0x12326f0 },
			{ false, 0x8E, 0x1232a60 },
			{ false, 0x8F, 0x1232780 },
			{ false, 0x90, 0x1232810 },
			{ false, 0x91, 0x1232b90 },
			{ false, 0x92, 0x1264b40 },
			{ false, 0x93, 0x1264b50 },
			{ false, 0x94, 0x122f9c0 },
			{ false, 0x95, 0x122fa90 },
			{ false, 0x96, 0x122fb60 },
			{ false, 0x97, 0x122e850 },
			{ false, 0x98, 0x122e830 },
			{ false, 0x99, 0xce1f40 },
			{ false, 0x9A, 0x126cae0 },
			{ false, 0x9C, 0x1264bd0 },
			{ false, 0x9D, 0x122e040 },
			{ false, 0x9E, 0x122e0e0 },
			{ false, 0x9F, 0x122e170 },
			{ false, 0xA0, 0x1248670 },
			{ false, 0xA1, 0x12317e0 },
			{ false, 0xA3, 0xce1fa0 },
			{ false, 0xA4, 0x11df640 },
			{ false, 0xAA, 0x1265650 },
			{ false, 0xAB, 0x1268070 },
			{ false, 0xAC, 0x1338c00 },
			{ false, 0xAD, 0x1239bc0 },
			{ false, 0xAE, 0x124b2b0 },
			{ false, 0xAF, 0xce2080 },
			{ false, 0xB0, 0x11df850 },
			{ false, 0xB1, 0x11dfab0 },
			{ false, 0xB2, 0x11dff60 },
			{ false, 0xB3, 0x11dff70 },
			{ false, 0xB4, 0x11dff80 },
			{ false, 0xB5, 0x11e01e0 },
			{ false, 0xB6, 0x11dfed0 },
			{ false, 0xB7, 0x124b690 },
			{ false, 0xB8, 0x1234020 },
			{ false, 0xB9, 0x124ce20 },
			{ false, 0xBA, 0xff6010 },
			{ false, 0xBB, 0x1231f20 },
			{ false, 0xBC, 0x124ce90 },
			{ false, 0xBD, 0x124cf50 },
			{ false, 0xBE, 0x126a650 },
			{ false, 0xBF, 0x1261b60 },
			{ false, 0xC0, 0x1261b40 },
			{ false, 0xC1, 0x1260ee0 },
			{ false, 0xC2, 0xce2000 },
			{ false, 0xC3, 0x122fb70 },
			{ false, 0xC4, 0x122fc30 },
			{ false, 0xC5, 0xeb0c20 },
			{ false, 0xC9, 0x1264b60 },
			{ false, 0xCA, 0x1264b30 },
			{ false, 0xCB, 0x1264b70 },
			{ false, 0xCC, 0xe93100 },
			{ false, 0xCD, 0xe93270 },
			{ false, 0xCE, 0x1262c50 },
			{ false, 0xD0, 0xce1930 },
			{ false, 0xD1, 0x1268580 },
			{ false, 0xD2, 0x12652b0 },
			{ false, 0xD3, 0x1269210 },
			{ false, 0xD4, 0x12693b0 },
			{ false, 0xD5, 0x1264fa0 },
			{ false, 0xD6, 0x10ebf00 },
			{ false, 0xD7, 0x1232e30 },
			{ false, 0xDA, 0x1269480 },
			{ false, 0xDB, 0x12694a0 },
			{ false, 0xDC, 0x1269490 },
			{ false, 0xDD, 0x1269470 },
			{ false, 0xDE, 0x1269460 },
			{ false, 0xDF, 0xfec2f0 },
			{ false, 0xE0, 0xfec400 },
			{ false, 0xE1, 0x1233e40 },
			{ false, 0xE2, 0x12528f0 },
			{ false, 0xE3, 0x12529c0 },
			{ false, 0xE4, 0x1233290 },
			{ false, 0xE5, 0x1233360 },
			{ false, 0xE6, 0x1233100 },
			{ false, 0xE7, 0x12335d0 },
			{ false, 0xE8, 0x1237fb0 },
			{ false, 0xE9, 0x1260e80 },
			{ false, 0xEA, 0x1254810 },
			{ false, 0xEB, 0x1233800 },
			{ false, 0xEC, 0x1232f90 },
			{ false, 0xED, 0x12336a0 },
			{ false, 0xEE, 0x11e06a0 },
			{ false, 0xEF, 0x11e06b0 },
			{ false, 0xF0, 0x11e09b0 },
			{ false, 0xF1, 0x11e0a40 },
			{ false, 0xF2, 0x11e0da0 },
			{ false, 0xF3, 0x11e1160 },
			{ false, 0xF4, 0x11e1220 },
			{ false, 0xF5, 0x124abb0 },
			{ false, 0xF7, 0x126a550 },
			{ false, 0xF8, 0x122f9b0 },
			{ false, 0xF9, 0xcdf530 },
			{ false, 0xFA, 0xce15b0 },
			{ false, 0xFB, 0x1232a50 },
			{ false, 0xFC, 0x1232af0 },
			{ false, 0xFD, 0x1237d40 },
			{ false, 0xFE, 0x1237ed0 },
			{ false, 0xFF, 0x1264c10 },
			{ false, 0x100, 0x125d9a0 },
			{ false, 0x101, 0xce2130 },
			{ false, 0x102, 0x1237f50 },
			{ false, 0x105, 0x1261b20 },
			{ false, 0x106, 0xce17d0 },
			{ false, 0x107, 0xce1880 },
			{ false, 0x108, 0x10ec610 },
			{ false, 0x109, 0x124a6f0 },
			{ false, 0x10A, 0xce2330 },
			{ false, 0x10B, 0x122e220 },
			{ false, 0x10E, 0xce2300 },
			{ false, 0x10F, 0xce23c0 },
			{ false, 0x112, 0x1253db0 },
			{ false, 0x113, 0x1264ca0 },
			{ false, 0x114, 0x1248f30 },
			{ false, 0x117, 0x1231780 },
			{ false, 0x118, 0x125d9f0 },
			{ false, 0x119, 0x1265370 },
			{ false, 0x11A, 0x1264b20 },
			{ false, 0x11B, 0xce2390 },
			{ false, 0x11C, 0xce1780 },
			{ false, 0x11D, 0x1269610 },
			{ false, 0x121, 0x1268170 },
			{ false, 0x122, 0x12315a0 },
			{ false, 0x123, 0x124a310 },
			{ false, 0x124, 0x124a380 },
			{ false, 0x125, 0x1263220 },
			{ false, 0x126, 0x124bf20 },
			{ false, 0x128, 0xcdfec0 },
			{ false, 0x129, 0xcdff30 },
			{ false, 0x12A, 0xce0000 },
			{ false, 0x12B, 0xcdff90 },
			{ false, 0x12F, 0x1231d90 },
			{ false, 0x130, 0x1231da0 },
			{ false, 0x131, 0x124b760 },
			{ false, 0x135, 0xcdf800 },
			{ false, 0x136, 0x1262360 },
			{ false, 0x137, 0x125ead0 },
			{ false, 0x138, 0x124d010 },
			{ false, 0x139, 0x1254130 },
			{ false, 0x13B, 0x1234b80 },
			{ false, 0x13C, 0x1262c60 },
			{ false, 0x13D, 0x12631d0 },
			{ false, 0x13F, 0xcdf6f0 },
			{ false, 0x140, 0xcdf6b0 },
			{ false, 0x141, 0x1226fb0 },
			{ false, 0x142, 0x12270a0 },
			{ false, 0x143, 0x1227190 },
			{ false, 0x144, 0x1227270 },
			{ false, 0x145, 0x1227360 },
			{ false, 0x14A, 0x12160e0 },
			{ false, 0x14B, 0x1216130 },
			{ false, 0x14C, 0x100e5a0 },
			{ false, 0x14D, 0x11e1560 },
			{ false, 0x14E, 0x11e19d0 },
			{ false, 0x150, 0x1262480 },
			{ false, 0x151, 0xe8cf10 },
			{ false, 0x152, 0xe8cca0 },
			{ false, 0x153, 0xe8cdc0 },
			{ false, 0x154, 0xe8d340 },
			{ false, 0x155, 0xe8d570 },
			{ false, 0x156, 0xe8d2b0 },
			{ false, 0x157, 0xe8d380 },
			{ false, 0x158, 0xe8d4d0 },
			{ false, 0x159, 0xe8d040 },
			{ false, 0x15A, 0xe8d0e0 },
			{ false, 0x15B, 0xe8d160 },
			{ false, 0x15C, 0xe8d1e0 },
			{ false, 0x15D, 0xe8ccc0 },
			{ false, 0x15E, 0xe8d630 },
			{ false, 0x15F, 0xe8d690 },
			{ false, 0x160, 0xe8d7b0 },
			{ false, 0x161, 0xe8d710 },
			{ false, 0x162, 0xe8d830 },
			{ false, 0x163, 0xe8d5b0 },
			{ false, 0x164, 0xe8d9f0 },
			{ false, 0x165, 0xe8da50 },
			{ false, 0x166, 0xe8d8d0 },
			{ false, 0x167, 0xe8d930 },
			{ false, 0x168, 0xe8d990 },
			{ false, 0x169, 0xe8dab0 },
			{ false, 0x16A, 0xe8dc10 },
			{ false, 0x16B, 0xe8db10 },
			{ false, 0x16C, 0xe8dd90 },
			{ false, 0x16D, 0xe8ded0 },
			{ false, 0x16E, 0xe8df30 },
			{ false, 0x16F, 0xe8dfb0 },
			{ false, 0x170, 0xe8e030 },
			{ false, 0x171, 0xe8e0d0 },
			{ false, 0x172, 0xe8e170 },
			{ false, 0x173, 0xe8e1e0 },
			{ false, 0x174, 0xe8e280 },
			{ false, 0x175, 0xe8e330 },
			{ false, 0x176, 0xe8e3d0 },
			{ false, 0x177, 0xe8e480 },
			{ false, 0x178, 0xe8e500 },
			{ false, 0x179, 0xe8e5a0 },
			{ false, 0x17A, 0xe8e5d0 },
			{ false, 0x17B, 0xe8e650 },
			{ false, 0x17C, 0xe8e6f0 },
			{ false, 0x17D, 0xe8e720 },
			{ false, 0x17E, 0xe8e750 },
			{ false, 0x17F, 0xe8de10 },
			{ false, 0x180, 0xe8e780 },
			{ false, 0x181, 0xe8e7b0 },
			{ false, 0x182, 0xe8e810 },
			{ false, 0x186, 0x124c3c0 },
			{ false, 0x187, 0x124c7a0 },
			{ false, 0x188, 0x124c560 },
			{ false, 0x189, 0x1247cf0 },
			{ false, 0x18A, 0x1262270 },
			{ false, 0x18B, 0x1247de0 },
			{ false, 0x18C, 0x124b8c0 },
			{ false, 0x18D, 0x124bc20 },
			{ false, 0x18E, 0x124bca0 },
			{ false, 0x18F, 0x126bf80 },
			{ false, 0x191, 0x12492a0 },
			{ false, 0x192, 0x1249260 },
			{ false, 0x193, 0x125f450 },
			{ false, 0x194, 0x125f4e0 },
			{ false, 0x195, 0xcdfaf0 },
			{ false, 0x196, 0x1262850 },
			{ false, 0x197, 0xcdf8f0 },
			{ false, 0x19A, 0x124a510 },
			{ false, 0x19B, 0x124a5c0 },
			{ false, 0x19C, 0x12634c0 },
			{ false, 0x19E, 0x1249070 },
			{ false, 0x19F, 0x1249120 },
			{ false, 0x1A0, 0x12634f0 },
			{ false, 0x1A2, 0x124a6c0 },
			{ false, 0x1A3, 0x124a540 },
			{ false, 0x1A4, 0x1263490 },
			{ false, 0x1A5, 0x124a610 },
			{ false, 0x1A7, 0x123c410 },
			{ false, 0x1A8, 0xff6500 },
			{ false, 0x1A9, 0x12622f0 },
			{ false, 0x1AA, 0x125fdb0 },
			{ false, 0x1AB, 0x124ab40 },
			{ false, 0x1AC, 0x1248dc0 },
			{ false, 0x1AD, 0x1248e90 },
			{ false, 0x1AE, 0x1248d30 },
			{ false, 0x1AF, 0x1248e10 },
			{ false, 0x1B3, 0x122f9a0 },
			{ false, 0x1B5, 0x1264ce0 },
			{ false, 0x1B6, 0x13261c0 },
			{ false, 0x1B7, 0xce0b60 },
			{ false, 0x1B8, 0xce0a50 },
			{ false, 0x1B9, 0xce07f0 },
			{ false, 0x1BD, 0x1268480 },
			{ false, 0x1BE, 0x12657a0 },
			{ false, 0x1BF, 0x122f990 },
			{ false, 0x1C2, 0x1265840 },
			{ false, 0x1C3, 0x1267b60 },
			{ false, 0x1C4, 0x1265150 },
			{ false, 0x1C6, 0x12320f0 },
			{ false, 0x1C7, 0x1232530 },
			{ false, 0x1C8, 0x1232240 },
			{ false, 0x1C9, 0x1269110 },
			{ false, 0x1CA, 0x1251b90 },
			{ false, 0x1CC, 0x10ec190 },
			{ false, 0x1CE, 0x12690a0 },
			{ false, 0x1CF, 0x1265940 },
			{ false, 0x1D0, 0x1267930 },
			{ false, 0x1D1, 0x1267a50 },
			{ false, 0x1D3, 0x12517b0 },
			{ false, 0x1D4, 0x1232b20 },
			{ false, 0x1D6, 0x126c1b0 },
			{ false, 0x1D7, 0x126c2c0 },
			{ false, 0x1D9, 0x1231cd0 },
			{ false, 0x1DA, 0x125ec60 },
			{ false, 0x1DB, 0x1231db0 },
			{ false, 0x1DC, 0x124ae20 },
			{ false, 0x1DD, 0x1265380 },
			{ false, 0x1DE, 0x1248ce0 },
			{ false, 0x1DF, 0x125f720 },
			{ false, 0x1E0, 0x122e450 },
			{ false, 0x1E5, 0x1264f30 },
			{ false, 0x1E6, 0x1269150 },
			{ false, 0x1E7, 0x12692c0 },
			{ false, 0x1E8, 0x1265020 },
			{ false, 0x1E9, 0x1251b70 },
			{ false, 0x1EA, 0x10ebff0 },
			{ false, 0x1EB, 0x10ec0f0 },
			{ false, 0x1EE, 0x1237530 },
			{ false, 0x1EF, 0x1264d20 },
			{ false, 0x1F1, 0x12478e0 },
			{ false, 0x1F2, 0xcdf330 },
			{ false, 0x1F3, 0x1251bb0 },
			{ false, 0x1F4, 0x12345d0 },
			{ false, 0x1F6, 0x1234aa0 },
			{ false, 0x1F8, 0x124d0c0 },
			{ false, 0x1F9, 0x123be20 },
			{ false, 0x1FA, 0x1243ab0 },
			{ false, 0x1FB, 0x126b900 },
			{ false, 0x1FC, 0x126bac0 },
			{ false, 0x1FD, 0x125e530 },
			{ false, 0x1FE, 0x1264b80 },
			{ false, 0x200, 0x125e2c0 },
			{ false, 0x201, 0x1253fc0 },
			{ false, 0x202, 0x1262d10 },
			{ false, 0x203, 0x1322dc0 },
			{ false, 0x204, 0x123c280 },
			{ false, 0x205, 0xff65f0 },
			{ false, 0x208, 0xcdf830 },
			{ false, 0x209, 0xcdf860 },
			{ false, 0x20A, 0x12494c0 },
			{ false, 0x20B, 0x1262330 },
			{ false, 0x20D, 0x124bda0 },
			{ false, 0x20E, 0xce1e00 },
			{ false, 0x20F, 0xce1ba0 },
			{ false, 0x211, 0xce28f0 },
			{ false, 0x212, 0xce2580 },
			{ false, 0x213, 0xce2630 },
			{ false, 0x214, 0xce2960 },
			{ false, 0x215, 0xce2a20 },
			{ false, 0x216, 0xce26c0 },
			{ false, 0x217, 0xce2770 },
			{ false, 0x218, 0xce2ae0 },
			{ false, 0x219, 0xce2be0 },
			{ false, 0x21A, 0xce2800 },
			{ false, 0x21B, 0xcdf390 },
			{ false, 0x237, 0x10ebeb0 },
			{ false, 0x238, 0xce1a80 },
			{ false, 0x239, 0x122f650 },
			{ false, 0x23A, 0x122f4d0 },
			{ false, 0x23B, 0x122f5a0 },
			{ false, 0x23C, 0x122f850 },
			{ false, 0x23D, 0x122f7c0 },
			{ false, 0x23E, 0x122f700 },
			{ false, 0x23F, 0xce06a0 },
			{ false, 0x240, 0x126beb0 },
			{ false, 0x241, 0x1261710 },
			{ false, 0x242, 0x1261500 },
			{ false, 0x243, 0x1261450 },
			{ false, 0x244, 0x1269560 },
			{ false, 0x248, 0x12651b0 },
			{ false, 0x24B, 0xce01c0 },
			{ false, 0x24C, 0xce0140 },
			{ false, 0x24D, 0xcdf940 },
			{ false, 0x24E, 0xce05c0 },
			{ false, 0x24F, 0xce0280 },
			{ false, 0x250, 0xce1700 },
			{ false, 0x251, 0xce04e0 },
			{ false, 0x252, 0xff6060 },
			{ false, 0x253, 0xff6360 },
			{ false, 0x254, 0xff5fa0 },
			{ false, 0x255, 0x124ccf0 },
			{ false, 0x256, 0x124ca10 },
			{ false, 0x257, 0x124c9e0 },
			{ false, 0x258, 0x124c9c0 },
			{ false, 0x259, 0x124c9d0 },
			{ false, 0x25A, 0x124ca00 },
			{ false, 0x25B, 0x124c9f0 },
			{ false, 0x25C, 0x124a1d0 },
			{ false, 0x25D, 0x1249dc0 },
			{ false, 0x25E, 0x1249f60 },
			{ false, 0x25F, 0x1249fd0 },
			{ false, 0x260, 0x1249630 },
			{ false, 0x262, 0x124a270 },
			{ false, 0x263, 0x124a3f0 },
			{ false, 0x264, 0x124a450 },
			{ false, 0x265, 0x1249e90 },
			{ false, 0x266, 0x124a040 },
			{ false, 0x267, 0x1249c60 },
			{ false, 0x268, 0x1249e30 },
			{ false, 0x269, 0x1249c00 },
			{ false, 0x26A, 0x1249d40 },
			{ false, 0x26B, 0x124a140 },
			{ false, 0x26D, 0x124a0b0 },
			{ false, 0x26E, 0x1249f00 },
			{ false, 0x26F, 0x1262470 },
			{ false, 0x270, 0x1231590 },
			{ false, 0x271, 0xce24b0 },
			{ false, 0x272, 0xce24f0 },
			{ false, 0x273, 0xce2540 },
			{ false, 0x274, 0x12312f0 },
			{ false, 0x275, 0x1251760 },
			{ false, 0x276, 0x12307b0 },
			{ false, 0x277, 0x1231580 },
			{ false, 0x278, 0x122f980 },
			{ false, 0x279, 0x12517a0 },
			{ false, 0x27A, 0x1251780 },
			{ false, 0x27B, 0x1251770 },
			{ false, 0x27C, 0x1251790 },
			{ false, 0x27D, 0x1232e40 },
			{ false, 0x27E, 0x1232b10 },
			{ false, 0x27F, 0xce2100 },
			{ false, 0x280, 0x1238140 },
			{ false, 0x281, 0x12312e0 },
			{ false, 0x282, 0x1251750 },
			{ false, 0x283, 0x1231440 },
			{ false, 0x284, 0x1231450 },
			{ false, 0x285, 0x1231460 },
			{ false, 0x286, 0x1230b60 },
			{ false, 0x287, 0x12311a0 },
			{ false, 0x288, 0x1230cd0 },
			{ false, 0x289, 0x1231190 },
			{ false, 0x28C, 0x12323a0 },
			{ false, 0x28D, 0x12325f0 },
			{ false, 0x28E, 0x1230910 },
			{ false, 0x28F, 0x1231470 },
			{ false, 0x290, 0x125eae0 },
			{ false, 0x291, 0x10a5620 },
			{ false, 0x292, 0x10a5700 },
			{ false, 0x293, 0x10a5780 },
			{ false, 0x294, 0x1005dc0 },
			{ false, 0x295, 0x1005a20 },
			{ false, 0x296, 0x10074e0 },
			{ false, 0x297, 0x1007310 },
			{ false, 0x298, 0x1007190 },
			{ false, 0x299, 0x10069c0 },
			{ false, 0x29A, 0x1006b80 },
			{ false, 0x29B, 0x10060e0 },
			{ false, 0x29C, 0x1005430 },
			{ false, 0x29D, 0x1005800 },
			{ false, 0x29E, 0x1006f10 },
			{ false, 0x29F, 0x1004b40 },
			{ false, 0x2A0, 0x1004e20 },
			{ false, 0x2A1, 0x10057e0 },
			{ false, 0x2A2, 0x1004690 },
			{ false, 0x2A3, 0x10046c0 },
			{ false, 0x2A4, 0x1004b70 },
			{ false, 0x2A5, 0x1004e50 },
			{ false, 0x2A6, 0x1004a80 },
			{ false, 0x2A7, 0xce2360 },
			{ false, 0x2A9, 0x1248860 },
			{ false, 0x2AA, 0x1262f30 },
			{ false, 0x2AB, 0x1262f70 },
			{ false, 0x2AD, 0x10a5840 },
			{ false, 0x2AE, 0x1232b00 },
			{ false, 0x2AF, 0x10a5b00 },
			{ false, 0x2B0, 0x10a61e0 },
			{ false, 0x2B1, 0x10a6930 },
			{ false, 0x2B2, 0x109cc90 },
			{ false, 0x2B3, 0x109da30 },
			{ false, 0x2B4, 0x109f410 },
			{ false, 0x2B5, 0x10a00d0 },
			{ false, 0x2B6, 0x10a0550 },
			{ false, 0x2B7, 0x10a0c60 },
			{ false, 0x2B8, 0x10a1de0 },
			{ false, 0x2B9, 0x10a5850 },
			{ false, 0x2BA, 0x10a25d0 },
			{ false, 0x2BB, 0x11e1ad0 },
			{ false, 0x2BC, 0x11e1630 },
			{ false, 0x2BD, 0x1249690 },
			{ false, 0x2BE, 0x1249810 },
			{ false, 0x2BF, 0x11e1c80 },
			{ false, 0x2C0, 0x11df610 },
			{ false, 0x2C1, 0x124be20 },
			{ false, 0x2C2, 0x109e6a0 },
			{ false, 0x2C3, 0x10a1440 },
			{ false, 0x2C4, 0x126bce0 },
			{ false, 0x2C5, 0x10a28e0 },
			{ false, 0x2C6, 0x10a2950 },
			{ false, 0x2C7, 0x10a29c0 },
			{ false, 0x2C8, 0x10a2a50 },
			{ false, 0x2C9, 0x10a2bb0 },
			{ false, 0x2CA, 0x11df050 },
			{ false, 0x2CC, 0x1261900 },
			{ false, 0x2CD, 0x124b6f0 },
			{ false, 0x2CE, 0xce2170 },
			{ false, 0x2CF, 0xce21b0 },
			{ false, 0x2D0, 0xce21e0 },
			{ false, 0x2D1, 0xce2240 },
			{ false, 0x2D2, 0xce22a0 },
			{ false, 0x2D3, 0x1233180 },
			{ false, 0x2D4, 0x1232ea0 },
			{ false, 0x2D6, 0x12491b0 },
			{ false, 0x2D7, 0x1230440 },
			{ false, 0x2D8, 0x125e6b0 },
			{ false, 0x2DC, 0x1006000 },
			{ false, 0x2DD, 0x1006e40 },
			{ false, 0x2DF, 0x1230bc0 },
			{ false, 0x2E0, 0x1231b60 },
			{ false, 0x2E1, 0x1249ce0 },
			{ false, 0x2E2, 0x1007090 },
			{ false, 0x2E3, 0x1230d40 },
			{ false, 0x2E4, 0x1230e50 },
			{ false, 0x2E5, 0x12306f0 },
			{ false, 0x2E6, 0x1233a00 },
			{ false, 0x2E7, 0x1233dc0 },
			{ false, 0x2E8, 0x1004670 },
			{ false, 0x2E9, 0xce0de0 },
			{ false, 0x2EA, 0x12496f0 },
			{ false, 0x2EB, 0x1249750 },
			{ false, 0x2EC, 0x12497b0 },
			{ false, 0x2ED, 0x12304f0 },
			{ false, 0x2EE, 0x11e2250 },
			{ false, 0x2EF, 0x11df200 },
			{ false, 0x2F0, 0x123bf90 },
			{ false, 0x2F1, 0x126c400 },
			{ false, 0x2F2, 0x1253e10 },
			{ false, 0x2F4, 0xce0550 },
			{ false, 0x2F5, 0x12685e0 },
			{ false, 0x2F6, 0xce1120 },
			{ false, 0x2F7, 0x1233790 },
			{ false, 0x2F9, 0x123c040 },
			{ false, 0x2FA, 0xce0360 },
			{ false, 0x2FB, 0xf863c0 },
			{ false, 0x2FC, 0x1264a90 },
			{ false, 0x2FF, 0x12489c0 },
			{ false, 0x300, 0x1248aa0 },
			{ false, 0x301, 0x1248b80 },
			{ false, 0x302, 0x124c070 },
			{ false, 0x303, 0x124c1f0 },
			{ false, 0x304, 0x10a2c90 },
			{ false, 0x306, 0xce0420 },
			{ false, 0x307, 0xce13a0 },
			{ false, 0x308, 0x126c480 },
			{ false, 0x309, 0x126c500 },
			{ false, 0x30A, 0xce14b0 },
			{ false, 0x30B, 0x1233a80 },
			{ false, 0x30C, 0x1233ad0 },
			{ false, 0x30D, 0x12340e0 },
			{ false, 0x30E, 0x1234600 },
			{ false, 0x30F, 0x12374a0 },
			{ false, 0x310, 0x126c530 },
			{ false, 0x315, 0xf86890 },
			{ false, 0x316, 0xf86950 },
			{ false, 0x317, 0xf869b0 },
			{ false, 0x319, 0x12376b0 },
			{ false, 0x31A, 0x12378d0 },
			{ false, 0x31B, 0x1237cf0 },
			{ false, 0x31C, 0x1237950 },
			{ false, 0x31E, 0x126c590 },
			{ false, 0x31F, 0x1248c60 },
			{ false, 0x321, 0x13d2ca0 },
			{ false, 0x322, 0x13d3210 },
			{ false, 0x323, 0x13d3070 },
			{ false, 0x326, 0x1234090 },
			{ false, 0x327, 0x1233030 },
			{ false, 0x328, 0x1268620 },
			{ false, 0x329, 0x12622c0 },
			{ false, 0x32A, 0x1237b30 },
			{ false, 0x32B, 0x1264270 },
			{ false, 0x32C, 0x1264360 },
			{ false, 0x32D, 0x1264450 },
			{ false, 0x331, 0x123bb00 },
			{ false, 0x332, 0x1268600 },
			{ false, 0x333, 0x125ec40 },
			{ false, 0x334, 0xe63900 },
			{ false, 0x335, 0xe62870 },
			{ false, 0x336, 0x126c660 },
			{ false, 0x337, 0x124a4b0 },
			{ false, 0x338, 0x10a2de0 },
			{ false, 0x339, 0x10a2f70 },
			{ false, 0x33A, 0x10a30e0 },
			{ false, 0x33B, 0xf86a10 },
			{ false, 0x33C, 0xf87320 },
			{ false, 0x33D, 0xf86cd0 },
			{ false, 0x33E, 0xf86ee0 },
			{ false, 0x33F, 0xf86b60 },
			{ false, 0x340, 0xf87110 },
			{ false, 0x341, 0xe96440 },
			{ false, 0x342, 0xe96830 },
			{ false, 0x343, 0xe8a0b0 },
			{ false, 0x344, 0xe8a2d0 },
			{ false, 0x345, 0xe8a960 },
			{ false, 0x346, 0xe8a880 },
			{ false, 0x347, 0xe8a7a0 },
			{ false, 0x348, 0xe8abf0 },
			{ false, 0x349, 0xe8ab20 },
			{ false, 0x34A, 0xe8aa50 },
			{ false, 0x34B, 0xe8adc0 },
			{ false, 0x34C, 0xe8a6f0 },
			{ false, 0x34D, 0xe8af80 },
			{ false, 0x34E, 0xe89ee0 },
			{ false, 0x34F, 0xe89fc0 },
			{ false, 0x350, 0xe8a470 },
			{ false, 0x351, 0xe8a550 },
			{ false, 0x352, 0xe8b020 },
			{ false, 0x353, 0xe8a3e0 },
			{ false, 0x354, 0xe8ae70 },
			{ false, 0x355, 0xe8acd0 },
			{ false, 0x356, 0xe9a6e0 },
			{ false, 0x357, 0xe9a890 },
			{ false, 0x358, 0xe9ab40 },
			{ false, 0x359, 0xe9af60 },
			{ false, 0x35A, 0xe9b180 },
			{ false, 0x35B, 0xe9b290 },
			{ false, 0x35C, 0xe9a570 },
			{ false, 0x35D, 0xe9a620 },
			{ false, 0x35E, 0xe9a960 },
			{ false, 0x35F, 0xe9aa10 },
			{ false, 0x360, 0xe9aad0 },
			{ false, 0x361, 0xe9a8f0 },
			{ false, 0x362, 0xe96590 },
			{ false, 0x363, 0xe965f0 },
			{ false, 0x364, 0xe96750 },
			{ false, 0x365, 0xe96580 },
			{ false, 0x366, 0xe96780 },
			{ false, 0x36C, 0xe96450 },
			{ false, 0x36D, 0xe96460 },
			{ false, 0x36E, 0xe62880 },
			{ false, 0x370, 0xeb0800 },
			{ false, 0x371, 0xeb0810 },
			{ false, 0x372, 0xeb08e0 },
			{ false, 0x373, 0xeb09b0 },
			{ false, 0x374, 0xeb09c0 },
			{ false, 0x375, 0xeb0a90 },
			{ false, 0x376, 0xeb0b80 },
			{ false, 0x377, 0xeb0da0 },
			{ false, 0x378, 0xeb0ed0 },
			{ false, 0x379, 0xeb0f10 },
			{ false, 0x37A, 0xeb1310 },
			{ false, 0x37B, 0x1234180 },
			{ false, 0x37C, 0x12344e0 },
			{ false, 0x37D, 0x1262430 },
			{ false, 0x37E, 0x1262420 },
			{ false, 0x37F, 0x1262440 },
			{ false, 0x380, 0x1262450 },
			{ false, 0x381, 0x1262460 },
			{ false, 0x382, 0x1231300 },
			{ false, 0x383, 0x12328c0 },
			{ false, 0x384, 0x12677c0 },
			{ false, 0x385, 0x1267830 },
			{ false, 0x386, 0x1267880 },
			{ false, 0x387, 0x12677e0 },
			{ false, 0x388, 0x109c2c0 },
			{ false, 0x389, 0x126dfe0 },
			{ false, 0x38A, 0x126dff0 },
			{ false, 0x38B, 0x126e010 },
			{ false, 0x38C, 0x126e030 },
			{ false, 0x38D, 0x122f1d0 },
			{ false, 0x38E, 0x126cf50 },
			{ false, 0x38F, 0x126d070 },
			{ false, 0x390, 0x126d160 },
			{ false, 0x391, 0xf863e0 },
			{ false, 0x392, 0xf86450 },
			{ false, 0x393, 0xf86480 },
			{ false, 0x394, 0xf864d0 },
			{ false, 0x395, 0xf86530 },
			{ false, 0x396, 0xf86590 },
			{ false, 0x397, 0xf865f0 },
			{ false, 0x398, 0xf86680 },
			{ false, 0x399, 0xf867b0 },
			{ false, 0x39A, 0xf86820 },
			{ false, 0x39B, 0xce23f0 },
			{ false, 0x39C, 0xce2420 },
			{ false, 0x39D, 0xce2450 },
			{ false, 0x39E, 0xce2480 },
			{ false, 0x3A0, 0x122ee80 },
			{ false, 0x3A5, 0xeb10a0 },
			{ false, 0x3A6, 0xeb11b0 },
			{ false, 0x3A7, 0xeb1240 },
			{ false, 0x3A8, 0xe47520 },
			{ false, 0x3A9, 0x122fc40 },
			{ false, 0x3AB, 0x122fc50 },
			{ false, 0x3AC, 0x122fc60 },
			{ false, 0x3AD, 0x126c3d0 },
			{ false, 0x3AE, 0x122fd10 },
			{ false, 0x3AF, 0xce2ce0 },
			{ false, 0x3B0, 0xce2e00 },
			{ false, 0x3B1, 0xce2f20 },
			{ false, 0x3B2, 0xce3040 },
			{ false, 0x3B3, 0xce3130 },
			{ false, 0x3B4, 0xce32a0 },
			{ false, 0x3B5, 0xce3460 },
			{ false, 0x3B6, 0x1398780 },
			{ false, 0x3B7, 0x1243800 },
			{ false, 0x3B8, 0x1366780 },
			{ false, 0x3B9, 0x11dfe00 },
			{ false, 0x3BC, 0x11e0600 },
			{ false, 0x3BF, 0x1265750 },
			{ false, 0x3C0, 0x1265780 },
			{ false, 0x3C1, 0x10ec570 },
			{ false, 0x3C2, 0x1263110 },
			{ false, 0x3C3, 0xf86440 },
			{ false, 0x3C4, 0x1006e00 },
			{ false, 0x3C5, 0x1230350 },
			{ false, 0x3C6, 0x124ca20 },
			{ false, 0x3C7, 0x11dfdb0 },
			{ false, 0x3C8, 0x11dfd10 },
			{ false, 0x3C9, 0x1244460 },
			{ false, 0x3CA, 0x1244470 },
			{ false, 0x3CB, 0x1244490 },
			{ false, 0x3CC, 0x1244630 },
			{ false, 0x3CD, 0x1244650 },
			{ false, 0x3CE, 0x1244690 },
			{ false, 0x3CF, 0x1244710 },
			{ false, 0x3D0, 0x12448b0 },
			{ false, 0x3D1, 0x12448d0 },
			{ false, 0x3D2, 0x122fc70 },
			{ false, 0x3D3, 0x123c7b0 },
			{ false, 0x3D4, 0x123c790 },
			{ false, 0x3D7, 0x1243c50 },
			{ false, 0x3D8, 0xe8d260 },
			{ false, 0x3D9, 0x122fe20 },
			{ false, 0x3DA, 0x11dfd90 },
			{ false, 0x3DB, 0x10ec750 },
			{ false, 0x3DC, 0x10ec860 },
			{ false, 0x3DD, 0x12448e0 },
			{ false, 0x3DE, 0xeafa30 },
			{ false, 0x3DF, 0x126aa20 },
			{ false, 0x3E0, 0x126ab80 },
			{ false, 0x3E1, 0x126acf0 },
			{ false, 0x3E2, 0x126ae30 },
			{ false, 0x3E3, 0x126b1b0 },
			{ false, 0x3E4, 0x126b1d0 },
			{ false, 0x3E5, 0x126b1e0 },
			{ false, 0x3E6, 0x126b2a0 },
			{ false, 0x3E7, 0x126b2b0 },
			{ false, 0x3E8, 0x126b2c0 },
			{ false, 0x3E9, 0xe8dc70 },
			{ false, 0x3EA, 0x12457a0 },
			{ false, 0x3EB, 0xeb0e50 },
			{ false, 0x3EC, 0x122ff80 },
			{ false, 0x3ED, 0x122ffb0 },
			{ false, 0x3EE, 0x122ffe0 },
			{ false, 0x3EF, 0x1230010 },
			{ false, 0x3F0, 0x1230280 },
			{ false, 0x3F1, 0xeb1bf0 },
			{ false, 0x3F2, 0xe92fc0 },
			{ false, 0x3F3, 0xe92c00 },
			{ false, 0x3F4, 0x1243880 },
			{ false, 0x3F5, 0x1243900 },
			{ false, 0x3F6, 0x1251dd0 },
			{ false, 0x3F7, 0x126e440 },
			{ false, 0x3F8, 0xe8dcd0 },
			{ false, 0x3F9, 0xe8dd30 },
			{ false, 0x3FA, 0x1261b30 },
			{ false, 0x3FB, 0x126e600 },
			{ false, 0x3FC, 0xe8ce50 },
			{ false, 0x3FD, 0xe8cee0 },
			{ false, 0x3FE, 0x122e420 },
			{ false, 0x3FF, 0xe8ddd0 },
			{ false, 0x400, 0x12681c0 },
			{ false, 0x401, 0x1268190 },
			{ false, 0x402, 0x12681f0 },
			{ false, 0x403, 0x1268220 },
			{ false, 0x404, 0x1268250 },
			{ false, 0x405, 0x1268280 },
			{ false, 0x406, 0x1232830 },
			{ false, 0x407, 0x1232950 },
			{ false, 0x408, 0x126b2d0 },
			{ false, 0x409, 0x10050e0 },
			{ false, 0x40A, 0x10046f0 },
			{ false, 0x40B, 0x1005110 },
			{ false, 0x40C, 0x1251a80 },
			{ false, 0x40D, 0x126b2e0 },
			{ false, 0x40E, 0xff60d0 },
			{ false, 0x40F, 0x1246770 },
			{ false, 0x410, 0x12650d0 },
			{ false, 0x411, 0x124d210 },
			{ false, 0x412, 0xe8a0e0 },
			{ false, 0x413, 0xe8a640 },
			{ false, 0x414, 0xe8de70 },
			{ false, 0x415, 0x12622b0 },
			{ false, 0x416, 0x1243910 },
			{ false, 0x417, 0x1230110 },
			{ false, 0x418, 0xe968a0 },
			{ true, 0x8000, 0x1239270 },
			{ true, 0x8002, 0x1271810 },
			{ true, 0x8003, 0x1271830 },
			{ true, 0x8004, 0x1271820 },
			{ true, 0x8005, 0x11fb7a0 },
			{ true, 0x8006, 0x1336ba0 },
			{ true, 0x8007, 0xf84d40 },
			{ true, 0x8008, 0x1248000 },
			{ true, 0x8009, 0x1248510 },
			{ true, 0x800A, 0x11fe3e0 },
			{ true, 0x800B, 0x11fed80 },
			{ true, 0x800C, 0xf8cd80 },
			{ true, 0x800D, 0x11fe8e0 },
			{ true, 0x800E, 0x11fe980 },
			{ true, 0x800F, 0x11ff3b0 },
			{ true, 0x8010, 0x11fefc0 },
			{ true, 0x8011, 0x11feea0 },
			{ true, 0x8012, 0x1202960 },
			{ true, 0x8013, 0x11fea40 },
			{ true, 0x8014, 0x11fec30 },
			{ true, 0x8016, 0x11feb90 },
			{ true, 0x8017, 0xf833f0 },
			{ true, 0x8018, 0xf8b2d0 },
			{ true, 0x8019, 0xe8f9c0 },
			{ true, 0x801A, 0xe8f740 },
			{ true, 0x801C, 0x11fd920 },
			{ true, 0x801D, 0xf8c1d0 },
			{ true, 0x801E, 0x123c5a0 },
			{ true, 0x8020, 0x123c6a0 },
			{ true, 0x8021, 0x11fb6f0 },
			{ true, 0x8022, 0x123fb70 },
			{ true, 0x8023, 0x1263c70 },
			{ true, 0x8024, 0x1263bf0 },
			{ true, 0x8025, 0x11fe1e0 },
			{ true, 0x8027, 0x1361230 },
			{ true, 0x8028, 0x1361460 },
			{ true, 0x8029, 0x13615c0 },
			{ true, 0x802A, 0x13616d0 },
			{ true, 0x802B, 0x1361900 },
			{ true, 0x802C, 0x1361a40 },
			{ true, 0x802E, 0x1361c10 },
			{ true, 0x802F, 0x1361d30 },
			{ true, 0x8031, 0x1361e90 },
			{ true, 0x8032, 0x1361fa0 },
			{ true, 0x8033, 0x13620b0 },
			{ true, 0x8036, 0x1362440 },
			{ true, 0x8037, 0x1362320 },
			{ true, 0x8038, 0x1362450 },
			{ true, 0x8039, 0x1362580 },
			{ true, 0x803A, 0x13626a0 },
			{ true, 0x803B, 0x1362820 },
			{ true, 0x803C, 0x1362990 },
			{ true, 0x803D, 0x1362ab0 },
			{ true, 0x803E, 0x1362bd0 },
			{ true, 0x803F, 0x1362cf0 },
			{ true, 0x8040, 0x1362e70 },
			{ true, 0x8041, 0x1362ec0 },
			{ true, 0x8042, 0x1363100 },
			{ true, 0x8043, 0x1362fe0 },
			{ true, 0x8044, 0x1363340 },
			{ true, 0x8045, 0x1363640 },
			{ true, 0x8047, 0x13639f0 },
			{ true, 0x8048, 0x1363b50 },
			{ true, 0x8049, 0x1363d10 },
			{ true, 0x804A, 0x1363f40 },
			{ true, 0x804B, 0x1364220 },
			{ true, 0x804C, 0x13643b0 },
			{ true, 0x804D, 0x1364540 },
			{ true, 0x804E, 0x13646e0 },
			{ true, 0x804F, 0x1364850 },
			{ true, 0x8050, 0x1364970 },
			{ true, 0x8051, 0x1364ac0 },
			{ true, 0x8052, 0x1364c20 },
			{ true, 0x8053, 0x1364d80 },
			{ true, 0x8054, 0x1364ef0 },
			{ true, 0x8055, 0x1365090 },
			{ true, 0x8056, 0x13651f0 },
			{ true, 0x8057, 0x1365340 },
			{ true, 0x8058, 0x1365660 },
			{ true, 0x8059, 0x1365940 },
			{ true, 0x805A, 0x1365b70 },
			{ true, 0x805B, 0x1365d10 },
			{ true, 0x805C, 0x1363220 },
			{ true, 0x805D, 0x1200bb0 },
			{ true, 0x805E, 0x1252b80 },
			{ true, 0x805F, 0x1252cb0 },
			{ true, 0x8060, 0x1238fe0 },
			{ true, 0x8061, 0x1238d00 },
			{ true, 0x8062, 0x11f3b10 },
			{ true, 0x8063, 0x11f3950 },
			{ true, 0x8064, 0x11f2dc0 },
			{ true, 0x8065, 0x11f2f90 },
			{ true, 0x8066, 0xf8b4b0 },
			{ true, 0x8067, 0xf8b3c0 },
			{ true, 0x8068, 0xf8a2f0 },
			{ true, 0x8069, 0x1263050 },
			{ true, 0x806A, 0xf83120 },
			{ true, 0x806F, 0x1214fc0 },
			{ true, 0x8070, 0xf856a0 },
			{ true, 0x8071, 0xf855b0 },
			{ true, 0x8072, 0xf85a60 },
			{ true, 0x8073, 0xf85b50 },
			{ true, 0x8074, 0xf834e0 },
			{ true, 0x8075, 0x1214250 },
			{ true, 0x8076, 0xe8f830 },
			{ true, 0x8077, 0x123dd80 },
			{ true, 0x8078, 0xf8c7e0 },
			{ true, 0x8079, 0xf8c9c0 },
			{ true, 0x807A, 0xf8d8c0 },
			{ true, 0x807B, 0xf89f30 },
			{ true, 0x807E, 0x11f36a0 },
			{ true, 0x807F, 0xf8e7c0 },
			{ true, 0x8080, 0x12614e0 },
			{ true, 0x8081, 0xf8cab0 },
			{ true, 0x8082, 0x123df20 },
			{ true, 0x8083, 0x1261420 },
			{ true, 0x8084, 0x1214580 },
			{ true, 0x8085, 0x12362c0 },
			{ true, 0x8089, 0x1268e40 },
			{ true, 0x808A, 0x1268f30 },
			{ true, 0x808B, 0x1336f80 },
			{ true, 0x808C, 0x12729b0 },
			{ true, 0x808D, 0x12021f0 },
			{ true, 0x8091, 0x122e9d0 },
			{ true, 0x8092, 0x122e940 },
			{ true, 0x8093, 0x1252e60 },
			{ true, 0x8094, 0x1253240 },
			{ true, 0x8095, 0x124ae40 },
			{ true, 0x809A, 0x1264b90 },
			{ true, 0x809B, 0x1215610 },
			{ true, 0x809C, 0x123d3c0 },
			{ true, 0x809D, 0x123d4f0 },
			{ true, 0x809E, 0x123d420 },
			{ true, 0x809F, 0x124acd0 },
			{ true, 0x80A1, 0x1264200 },
			{ true, 0x80A2, 0x11f2820 },
			{ true, 0x80A4, 0x126a440 },
			{ true, 0x80A5, 0x126a350 },
			{ true, 0x80A7, 0x1239300 },
			{ true, 0x80A9, 0x126b3f0 },
			{ true, 0x80AA, 0x126a9c0 },
			{ true, 0x80AB, 0x11fcf20 },
			{ true, 0x80AC, 0x1261d10 },
			{ true, 0x80AE, 0x11f3190 },
			{ true, 0x80AF, 0x11fcae0 },
			{ true, 0x80B0, 0x11f2720 },
			{ true, 0x80B1, 0x11fcd00 },
			{ true, 0x80B2, 0x11fd020 },
			{ true, 0x80B3, 0x122e9b0 },
			{ true, 0x80B4, 0x122e8d0 },
			{ true, 0x80B6, 0x1005880 },
			{ true, 0x80B7, 0x124a760 },
			{ true, 0x80B8, 0x11de9a0 },
			{ true, 0x80B9, 0x11def10 },
			{ true, 0x80BA, 0x11dee10 },
			{ true, 0x80BB, 0x11deef0 },
			{ true, 0x80BE, 0x1239770 },
			{ true, 0x80C2, 0x12011c0 },
			{ true, 0x80C3, 0x1201310 },
			{ true, 0x80C4, 0xf8e5e0 },
			{ true, 0x80C5, 0x126a7c0 },
			{ true, 0x80C6, 0x12071d0 },
			{ true, 0x80C7, 0x12073d0 },
			{ true, 0x80C8, 0x12070d0 },
			{ true, 0x80C9, 0x12074a0 },
			{ true, 0x80CA, 0x1207580 },
			{ true, 0x80CB, 0x12077b0 },
			{ true, 0x80CC, 0x12078a0 },
			{ true, 0x80CD, 0x1207940 },
			{ true, 0x80CE, 0x1207a00 },
			{ true, 0x80CF, 0x1207aa0 },
			{ true, 0x80D0, 0x1207b60 },
			{ true, 0x80D1, 0x12641a0 },
			{ true, 0x80D2, 0x10ed750 },
			{ true, 0x80D3, 0x11f28a0 },
			{ true, 0x80D6, 0x11f78d0 },
			{ true, 0x80D8, 0x12391f0 },
			{ true, 0x80DB, 0x123fd10 },
			{ true, 0x80DC, 0x126b450 },
			{ true, 0x80DD, 0x126ab20 },
			{ true, 0x80DF, 0x11fcfa0 },
			{ true, 0x80E0, 0x1261c30 },
			{ true, 0x80E2, 0x11f3210 },
			{ true, 0x80E3, 0x11fcb90 },
			{ true, 0x80E4, 0x11f27a0 },
			{ true, 0x80E5, 0x11fcd80 },
			{ true, 0x80E6, 0x11fd0a0 },
			{ true, 0x80E7, 0x11fe360 },
			{ true, 0x80E8, 0xf81e60 },
			{ true, 0x80E9, 0xf81f50 },
			{ true, 0x80EA, 0x123b2f0 },
			{ true, 0x80EB, 0x1214eb0 },
			{ true, 0x80EC, 0xf888b0 },
			{ true, 0x80ED, 0xf893f0 },
			{ true, 0x80EE, 0xf89300 },
			{ true, 0x80EF, 0xf8dd70 },
			{ true, 0x80F0, 0xf89210 },
			{ true, 0x80F1, 0x1337680 },
			{ true, 0x80F2, 0x1201490 },
			{ true, 0x80F4, 0xf8bff0 },
			{ true, 0x80F6, 0x11f3a90 },
			{ true, 0x80F9, 0xf82d60 },
			{ true, 0x80FA, 0x126a270 },
			{ true, 0x80FB, 0x126a2e0 },
			{ true, 0x80FC, 0x1244ec0 },
			{ true, 0x80FD, 0x12443b0 },
			{ true, 0x80FF, 0x11fb930 },
			{ true, 0x8100, 0x10ecbc0 },
			{ true, 0x8105, 0x11fc8f0 },
			{ true, 0x8106, 0x11fc9e0 },
			{ true, 0x8107, 0xf8d500 },
			{ true, 0x8108, 0x1254ce0 },
			{ true, 0x8109, 0x1249a40 },
			{ true, 0x810A, 0x1249b10 },
			{ true, 0x810B, 0xf8d050 },
			{ true, 0x810C, 0x10eda70 },
			{ true, 0x810D, 0x10edf80 },
			{ true, 0x810E, 0x10edfd0 },
			{ true, 0x810F, 0x10edb80 },
			{ true, 0x8110, 0x10edc80 },
			{ true, 0x8111, 0xe8fae0 },
			{ true, 0x8112, 0xe8fc00 },
			{ true, 0x8113, 0xe8fdf0 },
			{ true, 0x8114, 0x123da10 },
			{ true, 0x8115, 0x123d6f0 },
			{ true, 0x8117, 0x123d5a0 },
			{ true, 0x8118, 0x123d8a0 },
			{ true, 0x8119, 0x1235020 },
			{ true, 0x811B, 0x123f1f0 },
			{ true, 0x811E, 0x12036f0 },
			{ true, 0x811F, 0x1260430 },
			{ true, 0x8120, 0x10ebb90 },
			{ true, 0x8121, 0x1269e90 },
			{ true, 0x8122, 0x126a0b0 },
			{ true, 0x8123, 0xf894e0 },
			{ true, 0x8124, 0x11f7fb0 },
			{ true, 0x8125, 0x1209530 },
			{ true, 0x8126, 0x1209520 },
			{ true, 0x8127, 0x11f7e70 },
			{ true, 0x8128, 0x12055f0 },
			{ true, 0x8129, 0x1205600 },
			{ true, 0x812A, 0x1205af0 },
			{ true, 0x812B, 0x10ece20 },
			{ true, 0x812C, 0x10ed3f0 },
			{ true, 0x812D, 0x10ebb30 },
			{ true, 0x812E, 0x1237c90 },
			{ true, 0x812F, 0x12388c0 },
			{ true, 0x8130, 0x12474d0 },
			{ true, 0x8131, 0x1205310 },
			{ true, 0x8132, 0xf8d7d0 },
			{ true, 0x8133, 0x11f9ab0 },
			{ true, 0x8134, 0x11f9910 },
			{ true, 0x8136, 0xf89e40 },
			{ true, 0x8137, 0xf8c8d0 },
			{ true, 0x8138, 0x1203110 },
			{ true, 0x8139, 0x11dea40 },
			{ true, 0x813A, 0x11f61e0 },
			{ true, 0x813B, 0x1234300 },
			{ true, 0x813C, 0x1203540 },
			{ true, 0x813D, 0x1252190 },
			{ true, 0x813E, 0x1252530 },
			{ true, 0x813F, 0x1252590 },
			{ true, 0x8140, 0x1252050 },
			{ true, 0x8141, 0x12520f0 },
			{ true, 0x8142, 0x1252460 },
			{ true, 0x8143, 0x123f780 },
			{ true, 0x8144, 0x123f600 },
			{ true, 0x8146, 0x1338940 },
			{ true, 0x8147, 0x1202ff0 },
			{ true, 0x8148, 0x1202f40 },
			{ true, 0x8149, 0x1202d70 },
			{ true, 0x814A, 0x1261fe0 },
			{ true, 0x814B, 0xf8bd20 },
			{ true, 0x814D, 0x123f940 },
			{ true, 0x814E, 0x1238bb0 },
			{ true, 0x814F, 0x1238650 },
			{ true, 0x8150, 0x1238e30 },
			{ true, 0x8151, 0x1205100 },
			{ true, 0x8152, 0xf887d0 },
			{ true, 0x8153, 0xf88840 },
			{ true, 0x8154, 0xf886f0 },
			{ true, 0x8155, 0x124cb30 },
			{ true, 0x8156, 0x123a7d0 },
			{ true, 0x8157, 0x12604f0 },
			{ true, 0x8159, 0x11faca0 },
			{ true, 0x815A, 0x11f82f0 },
			{ true, 0x815B, 0x11f8480 },
			{ true, 0x815C, 0x125eb50 },
			{ true, 0x815D, 0x1338980 },
			{ true, 0x815E, 0x11fa550 },
			{ true, 0x815F, 0x11fa680 },
			{ true, 0x8160, 0x1264530 },
			{ true, 0x8164, 0x1204df0 },
			{ true, 0x8165, 0x11fab70 },
			{ true, 0x8167, 0x122ddd0 },
			{ true, 0x8168, 0x1268310 },
			{ true, 0x8169, 0x12384d0 },
			{ true, 0x816A, 0x1204f30 },
			{ true, 0x816B, 0x12038c0 },
			{ true, 0x816C, 0x1247030 },
			{ true, 0x816D, 0x124d670 },
			{ true, 0x816E, 0x124d470 },
			{ true, 0x816F, 0x124d7d0 },
			{ true, 0x8170, 0x1236060 },
			{ true, 0x8171, 0x1204820 },
			{ true, 0x8172, 0x10ec330 },
			{ true, 0x8173, 0xf83300 },
			{ true, 0x8174, 0x1235a40 },
			{ true, 0x8175, 0x1236370 },
			{ true, 0x8176, 0x1236450 },
			{ true, 0x8177, 0x1203640 },
			{ true, 0x8178, 0x1203650 },
			{ true, 0x8179, 0x11dece0 },
			{ true, 0x817A, 0x11deb30 },
			{ true, 0x817C, 0x12358b0 },
			{ true, 0x817D, 0x11fa4b0 },
			{ true, 0x817E, 0x11f4a20 },
			{ true, 0x817F, 0x1209250 },
			{ true, 0x8180, 0x12475d0 },
			{ true, 0x8181, 0x11fd630 },
			{ true, 0x8182, 0x11fd760 },
			{ true, 0x8183, 0x1254af0 },
			{ true, 0x8184, 0x11ffa90 },
			{ true, 0x8185, 0x11ffa30 },
			{ true, 0x8186, 0x11ffa70 },
			{ true, 0x8187, 0x11ffa60 },
			{ true, 0x8188, 0x11ffa50 },
			{ true, 0x8189, 0x11ffa40 },
			{ true, 0x818A, 0x11ffa80 },
			{ true, 0x818C, 0x12033f0 },
			{ true, 0x818D, 0x11fc800 },
			{ true, 0x818E, 0x11f97c0 },
			{ true, 0x818F, 0x11f9690 },
			{ true, 0x8190, 0x11f7970 },
			{ true, 0x8191, 0xf827c0 },
			{ true, 0x8192, 0xf826d0 },
			{ true, 0x8194, 0x1206a30 },
			{ true, 0x8195, 0x1205c90 },
			{ true, 0x8196, 0x11f3400 },
			{ true, 0x8197, 0x11f86a0 },
			{ true, 0x8198, 0x1260710 },
			{ true, 0x8199, 0x123e750 },
			{ true, 0x819C, 0x123dac0 },
			{ true, 0x819D, 0x123e350 },
			{ true, 0x819E, 0x123e110 },
			{ true, 0x81A0, 0x1246db0 },
			{ true, 0x81A1, 0x1246620 },
			{ true, 0x81A2, 0x1246b40 },
			{ true, 0x81A3, 0x1246c30 },
			{ true, 0x81A4, 0x1246440 },
			{ true, 0x81A5, 0x12469e0 },
			{ true, 0x81A6, 0x124d2c0 },
			{ true, 0x81A7, 0x124d3d0 },
			{ true, 0x81A8, 0xf83b70 },
			{ true, 0x81A9, 0xf838a0 },
			{ true, 0x81AA, 0x1202030 },
			{ true, 0x81AB, 0x12020b0 },
			{ true, 0x81AC, 0x10ecf10 },
			{ true, 0x81AD, 0xf88e50 },
			{ true, 0x81AE, 0x11f9d80 },
			{ true, 0x81B1, 0x1205500 },
			{ true, 0x81B3, 0x1234e60 },
			{ true, 0x81B4, 0x1253e20 },
			{ true, 0x81B5, 0xf85970 },
			{ true, 0x81B6, 0x12037e0 },
			{ true, 0x81B7, 0x10ecda0 },
			{ true, 0x81B8, 0xf8a110 },
			{ true, 0x81B9, 0xf80790 },
			{ true, 0x81BA, 0x1264260 },
			{ true, 0x81BB, 0xf8c590 },
			{ true, 0x81BC, 0xf8c4a0 },
			{ true, 0x81BD, 0x11fb2f0 },
			{ true, 0x81BE, 0x123f5a0 },
			{ true, 0x81C0, 0x11f3bc0 },
			{ true, 0x81C1, 0x11f2a10 },
			{ true, 0x81C2, 0x11f2ab0 },
			{ true, 0x81C3, 0x11f2c90 },
			{ true, 0x81C4, 0x11f2d30 },
			{ true, 0x81C5, 0x1202bc0 },
			{ true, 0x81C7, 0xf89030 },
			{ true, 0x81C9, 0x11fa190 },
			{ true, 0x81CA, 0x11fc250 },
			{ true, 0x81CB, 0x1239c60 },
			{ true, 0x81CC, 0x1239dd0 },
			{ true, 0x81CD, 0x1239fe0 },
			{ true, 0x81CE, 0x123a2c0 },
			{ true, 0x81CF, 0x126df00 },
			{ true, 0x81D0, 0xf8c680 },
			{ true, 0x81D1, 0x1252880 },
			{ true, 0x81D2, 0x11f9e30 },
			{ true, 0x81D3, 0x1204cd0 },
			{ true, 0x81D4, 0x1204a60 },
			{ true, 0x81D5, 0x1204ad0 },
			{ true, 0x81D6, 0x1204b90 },
			{ true, 0x81D7, 0x11fb150 },
			{ true, 0x81D8, 0x11f9160 },
			{ true, 0x81D9, 0xf896c0 },
			{ true, 0x81DA, 0xf88f40 },
			{ true, 0x81DB, 0xf8bc30 },
			{ true, 0x81DC, 0x11f9f00 },
			{ true, 0x81DD, 0x11f9fe0 },
			{ true, 0x81DE, 0x11fa0b0 },
			{ true, 0x81E0, 0x11fbe10 },
			{ true, 0x81E1, 0x1234290 },
			{ true, 0x81E2, 0x1240a50 },
			{ true, 0x81E4, 0x12682b0 },
			{ true, 0x81E5, 0x11fc310 },
			{ true, 0x81E7, 0x1249870 },
			{ true, 0x81E9, 0x11fba90 },
			{ true, 0x81EA, 0x1204440 },
			{ true, 0x81EB, 0x126d5f0 },
			{ true, 0x81EC, 0xf837b0 },
			{ true, 0x81ED, 0xf836c0 },
			{ true, 0x81EE, 0x1245250 },
			{ true, 0x81EF, 0x1245160 },
			{ true, 0x81F3, 0x12450a0 },
			{ true, 0x81F4, 0x1244fe0 },
			{ true, 0x81F5, 0x10ecd10 },
			{ true, 0x81F6, 0x10ecc80 },
			{ true, 0x81F7, 0x12044f0 },
			{ true, 0x81FA, 0x122e6f0 },
			{ true, 0x81FB, 0x1241a80 },
			{ true, 0x81FC, 0x123f150 },
			{ true, 0x81FD, 0x123f320 },
			{ true, 0x81FE, 0x123f330 },
			{ true, 0x8202, 0x1214470 },
			{ true, 0x8203, 0x1205b00 },
			{ true, 0x8204, 0x123ade0 },
			{ true, 0x8205, 0x12663e0 },
			{ true, 0x8206, 0x1267c90 },
			{ true, 0x8207, 0x1265ee0 },
			{ true, 0x8208, 0x1265fa0 },
			{ true, 0x8209, 0x125eb40 },
			{ true, 0x820A, 0xf85c40 },
			{ true, 0x820B, 0xf85d30 },
			{ true, 0x820C, 0x1253ed0 },
			{ true, 0x820D, 0x12610d0 },
			{ true, 0x820E, 0x10ec8b0 },
			{ true, 0x8210, 0x12614c0 },
			{ true, 0x8211, 0x1261340 },
			{ true, 0x8212, 0x1236940 },
			{ true, 0x8213, 0x12368a0 },
			{ true, 0x8214, 0x12422c0 },
			{ true, 0x8215, 0x1242140 },
			{ true, 0x8217, 0x1253380 },
			{ true, 0x8218, 0x12534a0 },
			{ true, 0x8219, 0x1204c00 },
			{ true, 0x821A, 0xf8c3b0 },
			{ true, 0x821B, 0xf8c0e0 },
			{ true, 0x821C, 0xf8b870 },
			{ true, 0x821D, 0x11fb880 },
			{ true, 0x821E, 0x1239a80 },
			{ true, 0x821F, 0x1239ba0 },
			{ true, 0x8220, 0x1239bb0 },
			{ true, 0x8221, 0x12397d0 },
			{ true, 0x8222, 0x1239960 },
			{ true, 0x8223, 0x1246ed0 },
			{ true, 0x8224, 0x1246e70 },
			{ true, 0x8226, 0x1270df0 },
			{ true, 0x8227, 0x1215280 },
			{ true, 0x8228, 0x123c820 },
			{ true, 0x8229, 0x1271050 },
			{ true, 0x822A, 0x1270b00 },
			{ true, 0x822B, 0x1271350 },
			{ true, 0x822C, 0x1271360 },
			{ true, 0x822D, 0x1271370 },
			{ true, 0x822E, 0xf8b960 },
			{ true, 0x822F, 0xf8ba50 },
			{ true, 0x8230, 0x1238790 },
			{ true, 0x8234, 0x1245650 },
			{ true, 0x8235, 0x1245a30 },
			{ true, 0x8236, 0x1245b40 },
			{ true, 0x8237, 0x1245920 },
			{ true, 0x8238, 0x122e9f0 },
			{ true, 0x8239, 0x11f3770 },
			{ true, 0x823A, 0x11f3860 },
			{ true, 0x823B, 0x1272c70 },
			{ true, 0x823C, 0x11fdc20 },
			{ true, 0x823F, 0xf8b1e0 },
			{ true, 0x8240, 0x124cab0 },
			{ true, 0x8241, 0x124ca30 },
			{ true, 0x8242, 0x1271e30 },
			{ true, 0x8243, 0x1272110 },
			{ true, 0x8244, 0x1272720 },
			{ true, 0x8245, 0x1270c90 },
			{ true, 0x8246, 0xf85790 },
			{ true, 0x8247, 0x1200c90 },
			{ true, 0x8249, 0x125ef00 },
			{ true, 0x824A, 0x11f4350 },
			{ true, 0x824B, 0x11f4280 },
			{ true, 0x824C, 0x11fc000 },
			{ true, 0x824E, 0x126a170 },
			{ true, 0x824F, 0x12607d0 },
			{ true, 0x8250, 0x1240e70 },
			{ true, 0x8251, 0x12408b0 },
			{ true, 0x8252, 0x1240b40 },
			{ true, 0x8253, 0x12412f0 },
			{ true, 0x8254, 0x12410a0 },
			{ true, 0x8258, 0x12404d0 },
			{ true, 0x8259, 0x1240560 },
			{ true, 0x825A, 0x1241560 },
			{ true, 0x825B, 0x12404e0 },
			{ true, 0x825C, 0x12404f0 },
			{ true, 0x825D, 0x11fc120 },
			{ true, 0x825E, 0x11fc1a0 },
			{ true, 0x825F, 0x126a6e0 },
			{ true, 0x8262, 0x11f40f0 },
			{ true, 0x8263, 0x1208df0 },
			{ true, 0x8264, 0x1260440 },
			{ true, 0x8265, 0x1249250 },
			{ true, 0x8266, 0x1249240 },
			{ true, 0x8267, 0x12492e0 },
			{ true, 0x8268, 0x12493d0 },
			{ true, 0x8269, 0x125f210 },
			{ true, 0x826A, 0x125f4f0 },
			{ true, 0x826B, 0x125f300 },
			{ true, 0x826C, 0x125fa90 },
			{ true, 0x826D, 0x125f8d0 },
			{ true, 0x826E, 0x125ed60 },
			{ true, 0x826F, 0x1203a50 },
			{ true, 0x8270, 0x1203c00 },
			{ true, 0x8271, 0x1203cf0 },
			{ true, 0x8272, 0xf806a0 },
			{ true, 0x8274, 0x125fb70 },
			{ true, 0x8275, 0x124ab50 },
			{ true, 0x8276, 0xf8de60 },
			{ true, 0x8277, 0xf8dc80 },
			{ true, 0x8278, 0x12051d0 },
			{ true, 0x8279, 0x1269010 },
			{ true, 0x827A, 0x1254960 },
			{ true, 0x827B, 0x12548e0 },
			{ true, 0x827C, 0x1252f50 },
			{ true, 0x827D, 0x1253050 },
			{ true, 0x827E, 0x1263e80 },
			{ true, 0x827F, 0x1263f60 },
			{ true, 0x8280, 0x12539c0 },
			{ true, 0x8281, 0x1253bf0 },
			{ true, 0x8282, 0x1264030 },
			{ true, 0x8283, 0x12640f0 },
			{ true, 0x8284, 0x1239380 },
			{ true, 0x8286, 0x12155c0 },
			{ true, 0x8287, 0x11fc4c0 },
			{ true, 0x8288, 0x1237170 },
			{ true, 0x828D, 0x126af40 },
			{ true, 0x828E, 0x1271530 },
			{ true, 0x828F, 0x1271610 },
			{ true, 0x8290, 0x1215450 },
			{ true, 0x8291, 0x1271840 },
			{ true, 0x8292, 0x1271860 },
			{ true, 0x8293, 0x1271380 },
			{ true, 0x8294, 0x1271460 },
			{ true, 0x8295, 0x1271b70 },
			{ true, 0x8296, 0x1271850 },
			{ true, 0x8297, 0xf82e50 },
			{ true, 0x829B, 0x12150d0 },
			{ true, 0x829E, 0x1338880 },
			{ true, 0x829F, 0x13388a0 },
			{ true, 0x82A0, 0x13388c0 },
			{ true, 0x82A1, 0x13388e0 },
			{ true, 0x82A2, 0x1338900 },
			{ true, 0x82A3, 0x1338920 },
			{ true, 0x82A4, 0x1338960 },
			{ true, 0x82A5, 0x13389c0 },
			{ true, 0x82A6, 0x1338a00 },
			{ true, 0x82A7, 0x1338a20 },
			{ true, 0x82A8, 0x1338a40 },
			{ true, 0x82A9, 0x1338a60 },
			{ true, 0x82AA, 0x1338a80 },
			{ true, 0x82AB, 0x1338aa0 },
			{ true, 0x82AC, 0x1338ac0 },
			{ true, 0x82AD, 0x1338ae0 },
			{ true, 0x82AE, 0x1338b00 },
			{ true, 0x82AF, 0x1338b20 },
			{ true, 0x82B0, 0x1338b40 },
			{ true, 0x82B1, 0x1338b60 },
			{ true, 0x82B2, 0x1338b80 },
			{ true, 0x82B3, 0x1338ba0 },
			{ true, 0x82B4, 0x1338bc0 },
			{ true, 0x82B5, 0x1338be0 },
			{ true, 0x82B7, 0x100ffb0 },
			{ true, 0x82B8, 0x100f870 },
			{ true, 0x82B9, 0x100fbc0 },
			{ true, 0x82BA, 0x11fb9e0 },
			{ true, 0x82BB, 0xf895d0 },
			{ true, 0x82BC, 0x12654c0 },
			{ true, 0x82BE, 0x12091d0 },
			{ true, 0x82BF, 0x1338270 },
			{ true, 0x82C0, 0x11fc720 },
			{ true, 0x82C6, 0xe90a20 },
			{ true, 0x82C7, 0x10ee130 },
			{ true, 0x82C8, 0xe8fa60 },
			{ true, 0x82C9, 0xe8faa0 },
			{ true, 0x82CA, 0xe8fab0 },
			{ true, 0x82CB, 0xe8fad0 },
			{ true, 0x82CC, 0xe8fac0 },
			{ true, 0x82CD, 0xe8fa70 },
			{ true, 0x82CE, 0xe8fa90 },
			{ true, 0x82CF, 0xe8fa80 },
			{ true, 0x82D0, 0xe90a30 },
			{ true, 0x82D1, 0xe90a50 },
			{ true, 0x82D2, 0xe90a40 },
			{ true, 0x82D3, 0x10ee240 },
			{ true, 0x82D4, 0xe90310 },
			{ true, 0x82D5, 0xe90500 },
			{ true, 0x82D6, 0xe90630 },
			{ true, 0x82D7, 0xe90790 },
			{ true, 0x82D8, 0xe902a0 },
			{ true, 0x82DA, 0x1236ff0 },
			{ true, 0x82DB, 0x1208d40 },
			{ true, 0x82DD, 0x1236f50 },
			{ true, 0x82DE, 0x1271cd0 },
			{ true, 0x82DF, 0x1271d80 },
			{ true, 0x82E0, 0x12042e0 },
			{ true, 0x82E1, 0x123f270 },
			{ true, 0x82E2, 0x1206c90 },
			{ true, 0x82E3, 0x1206e90 },
			{ true, 0x82E4, 0x11fdd10 },
			{ true, 0x82E5, 0x11fe010 },
			{ true, 0x82E6, 0x1260da0 },
			{ true, 0x82E7, 0x123cc50 },
			{ true, 0x82E8, 0x123d1f0 },
			{ true, 0x82E9, 0x123d640 },
			{ true, 0x82EA, 0x123d940 },
			{ true, 0x82EB, 0x12149a0 },
			{ true, 0x82EC, 0x12149c0 },
			{ true, 0x82ED, 0x1238150 },
			{ true, 0x82EE, 0x12366e0 },
			{ true, 0x82EF, 0x12365a0 },
			{ true, 0x82F0, 0x126a020 },
			{ true, 0x82F3, 0x1242460 },
			{ true, 0x82F4, 0x1011870 },
			{ true, 0x82F6, 0xf8bb40 },
			{ true, 0x82F7, 0x12370c0 },
			{ true, 0x82F8, 0x124d8b0 },
			{ true, 0x82FA, 0x1014a80 },
			{ true, 0x82FB, 0x1254da0 },
			{ true, 0x82FC, 0xf8cf60 },
			{ true, 0x82FD, 0xf8ce70 },
			{ true, 0x82FE, 0x1253140 },
			{ true, 0x82FF, 0xf8d9b0 },
			{ true, 0x8300, 0x1263db0 },
			{ true, 0x8301, 0xf8d6e0 },
			{ true, 0x8302, 0xe90010 },
			{ true, 0x8303, 0xe8ff90 },
			{ true, 0x8304, 0xe8ffd0 },
			{ true, 0x8305, 0xe8fff0 },
			{ true, 0x8306, 0xe8ffa0 },
			{ true, 0x8307, 0xe8ffc0 },
			{ true, 0x8308, 0xe8ffb0 },
			{ true, 0x8309, 0xe90020 },
			{ true, 0x830A, 0xe90040 },
			{ true, 0x830B, 0xe90030 },
			{ true, 0x830C, 0xf8bf00 },
			{ true, 0x830D, 0x12603d0 },
			{ true, 0x830E, 0xf89a80 },
			{ true, 0x830F, 0xf898a0 },
			{ true, 0x8310, 0xf89b70 },
			{ true, 0x8311, 0xf89990 },
			{ true, 0x8312, 0xf89c60 },
			{ true, 0x8313, 0xf89d50 },
			{ true, 0x8315, 0x126b810 },
			{ true, 0x8316, 0x126b700 },
			{ true, 0x8317, 0x1242a70 },
			{ true, 0x831A, 0x11f6320 },
			{ true, 0x831C, 0x1236ca0 },
			{ true, 0x831D, 0x1252290 },
			{ true, 0x831E, 0x12525f0 },
			{ true, 0x831F, 0x1252080 },
			{ true, 0x8320, 0x1252120 },
			{ true, 0x8321, 0x1252480 },
			{ true, 0x8322, 0x12716e0 },
			{ true, 0x8324, 0xf824f0 },
			{ true, 0x8326, 0xf825e0 },
			{ true, 0x8327, 0xf82a90 },
			{ true, 0x8328, 0xf82b80 },
			{ true, 0x832C, 0x1221a40 },
			{ true, 0x832D, 0x1204660 },
			{ true, 0x832E, 0x1204790 },
			{ true, 0x832F, 0x1202c50 },
			{ true, 0x8330, 0x1261e80 },
			{ true, 0x8331, 0x123b660 },
			{ true, 0x8333, 0x123eda0 },
			{ true, 0x8337, 0x123a940 },
			{ true, 0x8338, 0x123a510 },
			{ true, 0x8339, 0x12605a0 },
			{ true, 0x833C, 0x11f81f0 },
			{ true, 0x833D, 0x11f8380 },
			{ true, 0x833E, 0x12093d0 },
			{ true, 0x833F, 0x123bd70 },
			{ true, 0x8340, 0x11f3290 },
			{ true, 0x8341, 0xf8e8b0 },
			{ true, 0x8342, 0x1209480 },
			{ true, 0x8343, 0x1269e30 },
			{ true, 0x8344, 0x12646b0 },
			{ true, 0x8346, 0x1215660 },
			{ true, 0x8347, 0x12364f0 },
			{ true, 0x8348, 0xf8cba0 },
			{ true, 0x8349, 0x123ce90 },
			{ true, 0x834A, 0x123d7a0 },
			{ true, 0x834B, 0x123dc60 },
			{ true, 0x834C, 0xf82040 },
			{ true, 0x834D, 0x1215770 },
			{ true, 0x834E, 0x12041d0 },
			{ true, 0x8351, 0x1236b40 },
			{ true, 0x8352, 0x12398d0 },
			{ true, 0x8353, 0x123b0a0 },
			{ true, 0x8354, 0x1261130 },
			{ true, 0x8355, 0x1235d70 },
			{ true, 0x8356, 0x1235b20 },
			{ true, 0x8357, 0x1235c60 },
			{ true, 0x8358, 0x1214260 },
			{ true, 0x8359, 0x125f130 },
			{ true, 0x835A, 0xce88d0 },
			{ true, 0x835C, 0x1200ea0 },
			{ true, 0x835F, 0x12045b0 },
			{ true, 0x8360, 0x11fc3a0 },
			{ true, 0x8362, 0x1247170 },
			{ true, 0x8364, 0x1239670 },
			{ true, 0x8366, 0xf835d0 },
			{ true, 0x8367, 0x1214410 },
			{ true, 0x8368, 0x1235f70 },
			{ true, 0x836A, 0x1268d60 },
			{ true, 0x836B, 0x1214720 },
			{ true, 0x836C, 0x1214980 },
			{ true, 0x836D, 0x1214870 },
			{ true, 0x836E, 0x1214120 },
			{ true, 0x836F, 0x10ec270 },
			{ true, 0x8370, 0x12145d0 },
			{ true, 0x8371, 0x1214700 },
			{ true, 0x8372, 0x12145f0 },
			{ true, 0x8374, 0x1236df0 },
			{ true, 0x8376, 0xf83210 },
			{ true, 0x8378, 0x123fc20 },
			{ true, 0x837B, 0x1235e80 },
			{ true, 0x837C, 0x1237bd0 },
			{ true, 0x837F, 0x12367e0 },
			{ true, 0x8380, 0x12441e0 },
			{ true, 0x8381, 0x1244350 },
			{ true, 0x8382, 0x12149e0 },
			{ true, 0x8387, 0x11fa3e0 },
			{ true, 0x8388, 0x1241ec0 },
			{ true, 0x8389, 0x11f4940 },
			{ true, 0x838A, 0x1209310 },
			{ true, 0x838C, 0x1250e90 },
			{ true, 0x838D, 0x124cb60 },
			{ true, 0x8390, 0x1252ce0 },
			{ true, 0x8391, 0x1214c40 },
			{ true, 0x8392, 0x1214d60 },
			{ true, 0x8393, 0x1214dd0 },
			{ true, 0x8394, 0x1214e40 },
			{ true, 0x8395, 0x11fd120 },
			{ true, 0x8396, 0x11fd390 },
			{ true, 0x8397, 0x12549e0 },
			{ true, 0x8398, 0x1254c10 },
			{ true, 0x839C, 0x124ae60 },
			{ true, 0x839D, 0xf85f10 },
			{ true, 0x839E, 0xf8b690 },
			{ true, 0x839F, 0xf862d0 },
			{ true, 0x83A0, 0x12357c0 },
			{ true, 0x83A2, 0xf81d70 },
			{ true, 0x83A3, 0x12606a0 },
			{ true, 0x83A4, 0x123eb40 },
			{ true, 0x83A6, 0x1206ac0 },
			{ true, 0x83A8, 0x123e830 },
			{ true, 0x83A9, 0x1260a50 },
			{ true, 0x83AA, 0x1260ba0 },
			{ true, 0x83AC, 0x124ae50 },
			{ true, 0x83AD, 0x1236150 },
			{ true, 0x83AE, 0x1272b90 },
			{ true, 0x83AF, 0x1202130 },
			{ true, 0x83B0, 0x1338550 },
			{ true, 0x83B2, 0x1263080 },
			{ true, 0x83B6, 0x126a4a0 },
			{ true, 0x83B7, 0x1234f00 },
			{ true, 0x83B8, 0xf8e040 },
			{ true, 0x83BA, 0x1234d20 },
			{ true, 0x83C4, 0x1269750 },
			{ true, 0x83C5, 0x1269800 },
			{ true, 0x83C6, 0xf81c80 },
			{ true, 0x83C7, 0xf8e400 },
			{ true, 0x83CA, 0x126a4f0 },
			{ true, 0x83CB, 0xe8f7e0 },
			{ true, 0x83CC, 0x1234f90 },
			{ true, 0x83CD, 0x1234dc0 },
			{ true, 0x83CE, 0x1208f00 },
			{ true, 0x83CF, 0xf854c0 },
			{ true, 0x83D0, 0x12491e0 },
			{ true, 0x83D1, 0x1271270 },
			{ true, 0x83D2, 0x126b2f0 },
			{ true, 0x83D3, 0x1249520 },
			{ true, 0x83D4, 0x123b1a0 },
			{ true, 0x83D5, 0x123c960 },
			{ true, 0x83D6, 0x125f740 },
			{ true, 0x83D7, 0x1234af0 },
			{ true, 0x83D8, 0xe90180 },
			{ true, 0x83D9, 0xf83030 },
			{ true, 0x83DC, 0x124b130 },
			{ true, 0x83DD, 0x1201e90 },
			{ true, 0x83DE, 0x11f88c0 },
			{ true, 0x83DF, 0x11f87a0 },
			{ true, 0x83E0, 0x11f8830 },
			{ true, 0x83E1, 0x11f7cc0 },
			{ true, 0x83E2, 0x11f7c00 },
			{ true, 0x83E3, 0xf82c70 },
			{ true, 0x83E5, 0x1245f60 },
			{ true, 0x83E6, 0x1245f00 },
			{ true, 0x83E7, 0x1246fb0 },
			{ true, 0x83E8, 0x1246f30 },
			{ true, 0x83E9, 0x1245e70 },
			{ true, 0x83EA, 0x1245c90 },
			{ true, 0x83EB, 0x1245d40 },
			{ true, 0x83EC, 0xf85880 },
			{ true, 0x83ED, 0x12407c0 },
			{ true, 0x83EE, 0x1240960 },
			{ true, 0x83EF, 0x1237600 },
			{ true, 0x83F0, 0x1237740 },
			{ true, 0x83F1, 0x1237330 },
			{ true, 0x83F2, 0x12373f0 },
			{ true, 0x83F3, 0x12377f0 },
			{ true, 0x83F4, 0x10079e0 },
			{ true, 0x83F5, 0x123f410 },
			{ true, 0x83F7, 0x11f3530 },
			{ true, 0x83F8, 0x126da40 },
			{ true, 0x83F9, 0x126da50 },
			{ true, 0x83FA, 0x1239110 },
			{ true, 0x83FB, 0xf82130 },
			{ true, 0x83FC, 0xe90050 },
			{ true, 0x83FD, 0x11fb300 },
			{ true, 0x83FE, 0x11fb590 },
			{ true, 0x83FF, 0x11fb650 },
			{ true, 0x8400, 0x125ebc0 },
			{ true, 0x8401, 0xf88d60 },
			{ true, 0x8404, 0x1248fb0 },
			{ true, 0x8405, 0xf82f40 },
			{ true, 0x8406, 0x11fbd70 },
			{ true, 0x8409, 0x10144b0 },
			{ true, 0x8428, 0x1271870 },
			{ true, 0x8429, 0x1251600 },
			{ true, 0x842A, 0xf83990 },
			{ true, 0x842B, 0x1263af0 },
			{ true, 0x842C, 0x1263820 },
			{ true, 0x842D, 0x12636b0 },
			{ true, 0x842E, 0x1263810 },
			{ true, 0x842F, 0x1263a00 },
			{ true, 0x8430, 0x1263520 },
			{ true, 0x8431, 0x1207d60 },
			{ true, 0x8432, 0x1207e20 },
			{ true, 0x8433, 0x1208a10 },
			{ true, 0x8434, 0x1263910 },
			{ true, 0x8435, 0x1208bf0 },
			{ true, 0x8436, 0x11f4780 },
			{ true, 0x8437, 0x11f48a0 },
			{ true, 0x8438, 0x11f4660 },
			{ true, 0x8439, 0x11f4800 },
			{ true, 0x843A, 0x1260680 },
			{ true, 0x843B, 0x122dab0 },
			{ true, 0x843C, 0x122dc20 },
			{ true, 0x843D, 0x1254e40 },
			{ true, 0x843E, 0xe8ff10 },
			{ true, 0x843F, 0x123f350 },
			{ true, 0x8441, 0x11f3750 },
			{ true, 0x8442, 0x1214240 },
			{ true, 0x8443, 0x11f3760 },
			{ true, 0x8444, 0x11ff3c0 },
			{ true, 0x8445, 0x11fea20 },
			{ true, 0x8446, 0x11fea30 },
			{ true, 0x8447, 0x11f3c40 },
			{ true, 0x8448, 0x11f3ce0 },
			{ true, 0x8449, 0x12350c0 },
			{ true, 0x844A, 0x10a3330 },
			{ true, 0x844B, 0x10a3410 },
			{ true, 0x844C, 0x10a3570 },
			{ true, 0x844D, 0x10a3850 },
			{ true, 0x844E, 0x10a3a70 },
			{ true, 0x844F, 0x10a3e60 },
			{ true, 0x8450, 0xf884a0 },
			{ true, 0x8451, 0xf8c770 },
			{ true, 0x8452, 0x11ff3d0 },
			{ true, 0x8453, 0x11ff3e0 },
			{ true, 0x8454, 0x11ff0e0 },
			{ true, 0x8455, 0x11ff100 },
			{ true, 0x8456, 0x11ff2a0 },
			{ true, 0x8457, 0x11ff2b0 },
			{ true, 0x8458, 0x1005b30 },
			{ true, 0x8459, 0x1260ca0 },
			{ true, 0x845A, 0x11ffa10 },
			{ true, 0x845B, 0x11ffa20 },
			{ true, 0x845C, 0x11ff990 },
			{ true, 0x845D, 0x11ff9a0 },
			{ true, 0x845E, 0x11ffa00 },
			{ true, 0x845F, 0x11ff9f0 },
			{ true, 0x8460, 0x11ff9e0 },
			{ true, 0x8461, 0x11ff9d0 },
			{ true, 0x8462, 0x11ff9b0 },
			{ true, 0x8463, 0x11ff9c0 },
			{ true, 0x8464, 0x10a40c0 },
			{ true, 0x8465, 0x12352c0 },
			{ true, 0x8466, 0x11f4aa0 },
			{ true, 0x8467, 0x11f4b20 },
			{ true, 0x8468, 0x11f4ea0 },
			{ true, 0x8469, 0x11f5100 },
			{ true, 0x846A, 0x11f4f50 },
			{ true, 0x846B, 0x11fce00 },
			{ true, 0x846C, 0x10a4210 },
			{ true, 0x846D, 0x10a4340 },
			{ true, 0x846E, 0x10a4470 },
			{ true, 0x846F, 0x10a4830 },
			{ true, 0x8470, 0x124fd60 },
			{ true, 0x8471, 0x1250490 },
			{ true, 0x8472, 0x1250690 },
			{ true, 0x8473, 0xfc6270 },
			{ true, 0x8474, 0xfc6380 },
			{ true, 0x8475, 0x11f3d60 },
			{ true, 0x8476, 0x1260910 },
			{ true, 0x8477, 0x11fbec0 },
			{ true, 0x8478, 0x126b4b0 },
			{ true, 0x8479, 0x11f64b0 },
			{ true, 0x847A, 0x11f65e0 },
			{ true, 0x847B, 0x11f6970 },
			{ true, 0x847C, 0x11f6a10 },
			{ true, 0x847D, 0x123af70 },
			{ true, 0x847E, 0x11f6ee0 },
			{ true, 0x847F, 0x11f6d60 },
			{ true, 0x8480, 0x11f7090 },
			{ true, 0x8481, 0x11ff5c0 },
			{ true, 0x8482, 0x123fe10 },
			{ true, 0x8483, 0xf88260 },
			{ true, 0x8484, 0x1200bc0 },
			{ true, 0x8485, 0x123caa0 },
			{ true, 0x8486, 0x1232c80 },
			{ true, 0x8487, 0x10068d0 },
			{ true, 0x8488, 0x124faf0 },
			{ true, 0x8489, 0x124fca0 },
			{ true, 0x848A, 0x1250c10 },
			{ true, 0x848B, 0x12500f0 },
			{ true, 0x848C, 0x1250990 },
			{ true, 0x848D, 0x11fe4e0 },
			{ true, 0x848E, 0x11fe690 },
			{ true, 0x848F, 0x11fe740 },
			{ true, 0x8490, 0x11fe7f0 },
			{ true, 0x8491, 0x11fa230 },
			{ true, 0x8492, 0x11fa330 },
			{ true, 0x8493, 0x1250ab0 },
			{ true, 0x8494, 0xf83a80 },
			{ true, 0x8495, 0xfbb220 },
			{ true, 0x8496, 0xfbb040 },
			{ true, 0x8497, 0x126b8d0 },
			{ true, 0x8498, 0xf87820 },
			{ true, 0x8499, 0xf87a00 },
			{ true, 0x849A, 0xf87910 },
			{ true, 0x849B, 0xf87af0 },
			{ true, 0x849C, 0xf87be0 },
			{ true, 0x849D, 0xf87eb0 },
			{ true, 0x849E, 0xf88680 },
			{ true, 0x849F, 0x11ff060 },
			{ true, 0x84A0, 0x10ecfc0 },
			{ true, 0x84A1, 0xf86000 },
			{ true, 0x84A2, 0x1240d30 },
			{ true, 0x84A3, 0x12444b0 },
			{ true, 0x84A4, 0x1244730 },
			{ true, 0x84A5, 0x123f140 },
			{ true, 0x84A6, 0x123f160 },
			{ true, 0x84A7, 0x1244ad0 },
			{ true, 0x84A8, 0x1244db0 },
			{ true, 0x84A9, 0x12337f0 },
			{ true, 0x84AB, 0x12432a0 },
			{ true, 0x84AC, 0x1243340 },
			{ true, 0x84AD, 0x12434a0 },
			{ true, 0x84AE, 0x1243590 },
			{ true, 0x84AF, 0x12436d0 },
			{ true, 0x84B0, 0x1243110 },
			{ true, 0x84B1, 0x1243890 },
			{ true, 0x84B2, 0x1243180 },
			{ true, 0x84B3, 0x12431f0 },
			{ true, 0x84B4, 0x1244150 },
			{ true, 0x84B5, 0x1243920 },
			{ true, 0x84B6, 0x1242e70 },
			{ true, 0x84B8, 0x1006820 },
			{ true, 0x84B9, 0xf83c60 },
			{ true, 0x84BA, 0xf83d50 },
			{ true, 0x84BC, 0xf88580 },
			{ true, 0x84BD, 0xf88630 },
			{ true, 0x84BE, 0xf88660 },
			{ true, 0x84BF, 0xf881f0 },
			{ true, 0x84C0, 0x1243bc0 },
			{ true, 0x84C1, 0x13389a0 },
			{ true, 0x84C2, 0xf87fa0 },
			{ true, 0x84C3, 0x13389e0 },
			{ true, 0x84C4, 0x1253930 },
			{ true, 0x84C5, 0x11f6710 },
			{ true, 0x84C6, 0x11f6ab0 },
			{ true, 0x84C7, 0x11f8510 },
			{ true, 0x84C8, 0x11f8610 },
			{ true, 0x84CD, 0x11ff4a0 },
			{ true, 0x84D6, 0x123cbb0 },
			{ true, 0x84D7, 0x123d170 },
			{ true, 0x84D8, 0x10a4940 },
			{ true, 0x84D9, 0x10a4a70 },
			{ true, 0x84DA, 0x124f530 },
			{ true, 0x84DB, 0x124f010 },
			{ true, 0x84DC, 0x1253870 },
			{ true, 0x84DF, 0x123f020 },
			{ true, 0x84E0, 0xf883c0 },
			{ true, 0x84E1, 0xf88430 },
			{ true, 0x84E2, 0x11f5190 },
			{ true, 0x84E3, 0x11f51b0 },
			{ true, 0x84E4, 0x125da50 },
			{ true, 0x84E5, 0x11f4da0 },
			{ true, 0x84E6, 0x123cc30 },
			{ true, 0x84E7, 0x123cc40 },
			{ true, 0x84EA, 0x1253750 },
			{ true, 0x84EB, 0x1253770 },
			{ true, 0x84EC, 0xf84f20 },
			{ true, 0x84ED, 0xf85010 },
			{ true, 0x84EE, 0xf85100 },
			{ true, 0x84EF, 0x123ce40 },
			{ true, 0x84F0, 0x11f6e20 },
			{ true, 0x84F1, 0x10ec3c0 },
			{ true, 0x84F2, 0x10ec480 },
			{ true, 0x84F3, 0x123ce50 },
			{ true, 0x84F4, 0x123d0b0 },
			{ true, 0x84F5, 0x1206360 },
			{ true, 0x84F6, 0x1206790 },
			{ true, 0x84F7, 0x12354d0 },
			{ true, 0x84F9, 0xf83e40 },
			{ true, 0x84FA, 0xf83f30 },
			{ true, 0x84FB, 0xf85e20 },
			{ true, 0x84FC, 0x123ce60 },
			{ true, 0x84FD, 0x12507b0 },
			{ true, 0x84FE, 0xf8be10 },
			{ true, 0x84FF, 0x12394c0 },
			{ true, 0x8500, 0xf88c70 },
			{ true, 0x8501, 0xf88760 },
			{ true, 0x8502, 0x1205610 },
			{ true, 0x8503, 0x12057f0 },
			{ true, 0x8506, 0x11f91e0 },
			{ true, 0x8507, 0x1253790 },
			{ true, 0x8508, 0x123ce70 },
			{ true, 0x8509, 0x123ce80 },
			{ true, 0x850B, 0xf8a4d0 },
			{ true, 0x8512, 0xfbab80 },
			{ true, 0x8513, 0xfbb5d0 },
			{ true, 0x8514, 0x1205de0 },
			{ true, 0x8515, 0x1205f50 },
			{ true, 0x8516, 0x12061b0 },
			{ true, 0x8517, 0x12068d0 },
			{ true, 0x8519, 0x1250cf0 },
			{ true, 0x851A, 0x123c180 },
			{ true, 0x851B, 0x100fda0 },
			{ true, 0x851C, 0x123d0a0 },
			{ true, 0x851D, 0x1235630 },
			{ true, 0x851E, 0x1235710 },
			{ true, 0x851F, 0x11f6b50 },
			{ true, 0x8520, 0x126a3b0 },
			{ true, 0x8521, 0x12724a0 },
			{ true, 0x8522, 0xf81000 },
			{ true, 0x8523, 0xf810f0 },
			{ true, 0x8524, 0x125f730 },
			{ true, 0x8525, 0x13617e0 },
			{ true, 0x8527, 0x125e8c0 },
			{ true, 0x8529, 0x11f6fa0 },
			{ true, 0x852A, 0x11f5230 },
			{ true, 0x852B, 0x10a4b20 },
			{ true, 0x852C, 0x10a4bf0 },
			{ true, 0x852D, 0x10a4cc0 },
			{ true, 0x852E, 0x10a4d90 },
			{ true, 0x852F, 0x10a4e60 },
			{ true, 0x8530, 0x10a5180 },
			{ true, 0x8531, 0x10a53d0 },
			{ true, 0x8532, 0x12338e0 },
			{ true, 0x8533, 0x1242f70 },
			{ true, 0x8534, 0x10a4ff0 },
			{ true, 0x8535, 0xf8a7a0 },
			{ true, 0x8536, 0xf8a730 },
			{ true, 0x8539, 0x1272d50 },
			{ true, 0x853A, 0x1238b30 },
			{ true, 0x853C, 0x1206630 },
			{ true, 0x853D, 0x12389f0 },
			{ true, 0x853E, 0x11f4e20 },
			{ true, 0x853F, 0xe8fcf0 },
			{ true, 0x8540, 0x1237a40 },
			{ true, 0x8541, 0x11f6840 },
			{ true, 0x8542, 0xf8a880 },
			{ true, 0x8545, 0x11fe5c0 },
			{ true, 0x8546, 0xf801f0 },
			{ true, 0x8547, 0xf803d0 },
			{ true, 0x8548, 0x1245fc0 },
			{ true, 0x8549, 0x1246030 },
			{ true, 0x854A, 0x12460a0 },
			{ true, 0x854C, 0x11f7270 },
			{ true, 0x854D, 0x11f80e0 },
			{ true, 0x8550, 0xfbb760 },
			{ true, 0x8551, 0x123b820 },
			{ true, 0x8552, 0xf805b0 },
			{ true, 0x8553, 0x122e5f0 },
			{ true, 0x8554, 0x11fbf40 },
			{ true, 0x8555, 0x1233430 },
			{ true, 0x8556, 0x11ff1d0 },
			{ true, 0x8557, 0x1006690 },
			{ true, 0x8558, 0x1006490 },
			{ true, 0x855B, 0xf851f0 },
			{ true, 0x855C, 0x1243ff0 },
			{ true, 0x855D, 0x1243e00 },
			{ true, 0x855E, 0x1243e90 },
			{ true, 0x8561, 0x12031c0 },
			{ true, 0x8562, 0x12032e0 },
			{ true, 0x8563, 0x1203660 },
			{ true, 0x8564, 0x1014760 },
			{ true, 0x8565, 0x11f7430 },
			{ true, 0x8566, 0x11f74d0 },
			{ true, 0x8567, 0x11f75d0 },
			{ true, 0x8568, 0x11f7720 },
			{ true, 0x8569, 0x1265d50 },
			{ true, 0x856A, 0x1265e60 },
			{ true, 0x856D, 0x1272fa0 },
			{ true, 0x856E, 0x1273070 },
			{ true, 0x856F, 0x123f340 },
			{ true, 0x8572, 0xf84020 },
			{ true, 0x8574, 0x11faa00 },
			{ true, 0x8575, 0x11f2920 },
			{ true, 0x857E, 0x1239410 },
			{ true, 0x857F, 0x123b930 },
			{ true, 0x8580, 0x123a850 },
			{ true, 0x8581, 0x12423d0 },
			{ true, 0x8582, 0x11f4ca0 },
			{ true, 0x8583, 0x11f4d20 },
			{ true, 0x8584, 0x1203990 },
			{ true, 0x8585, 0x123f170 },
			{ true, 0x8588, 0x12644c0 },
			{ true, 0x8589, 0x1239a00 },
			{ true, 0x858A, 0x1246210 },
			{ true, 0x858B, 0x1246280 },
			{ true, 0x858C, 0xf8a640 },
			{ true, 0x858D, 0x1205a10 },
			{ true, 0x858E, 0x1206540 },
			{ true, 0x858F, 0x1265f00 },
			{ true, 0x8590, 0x1265f40 },
			{ true, 0x8591, 0x1265f80 },
			{ true, 0x8592, 0x1202890 },
			{ true, 0x8593, 0x12462f0 },
			{ true, 0x8594, 0x12463d0 },
			{ true, 0x8595, 0xf80100 },
			{ true, 0x8596, 0x1263b00 },
			{ true, 0x8597, 0x1264870 },
			{ true, 0x8598, 0x11f3de0 },
			{ true, 0x8599, 0x11f3ed0 },
			{ true, 0x859A, 0x11f3fc0 },
			{ true, 0x859C, 0x1214aa0 },
			{ true, 0x859D, 0x1214b80 },
			{ true, 0x859E, 0x123b9f0 },
			{ true, 0x859F, 0x1265f60 },
			{ true, 0x85A0, 0x1265f20 },
			{ true, 0x85A1, 0xe63c90 },
			{ true, 0x85A2, 0xe62260 },
			{ true, 0x85A3, 0xe637a0 },
			{ true, 0x85A4, 0xe63990 },
			{ true, 0x85A5, 0xe638a0 },
			{ true, 0x85A6, 0xe62fe0 },
			{ true, 0x85A7, 0xe62660 },
			{ true, 0x85A8, 0xe635f0 },
			{ true, 0x85A9, 0xe62a00 },
			{ true, 0x85AA, 0xe636d0 },
			{ true, 0x85AB, 0xe63510 },
			{ true, 0x85AC, 0xe63480 },
			{ true, 0x85AD, 0xe63c00 },
			{ true, 0x85AE, 0xe62eb0 },
			{ true, 0x85AF, 0xe62da0 },
			{ true, 0x85B0, 0xe62b90 },
			{ true, 0x85B1, 0xe62970 },
			{ true, 0x85B2, 0xe62c90 },
			{ true, 0x85B3, 0xe624d0 },
			{ true, 0x85B4, 0xe631e0 },
			{ true, 0x85B5, 0xe63350 },
			{ true, 0x85B6, 0xe630b0 },
			{ true, 0x85B7, 0xe62890 },
			{ true, 0x85B8, 0xf80880 },
			{ true, 0x85B9, 0xf80970 },
			{ true, 0x85BA, 0xf84a70 },
			{ true, 0x85BB, 0xf84b60 },
			{ true, 0x85BC, 0xf8a970 },
			{ true, 0x85BD, 0xf8ac40 },
			{ true, 0x85BE, 0xf8d5f0 },
			{ true, 0x85BF, 0xf8e9a0 },
			{ true, 0x85C0, 0xf8ea90 },
			{ true, 0x85C1, 0xf8eb80 },
			{ true, 0x85C2, 0xf8ec70 },
			{ true, 0x85C3, 0xf8ee50 },
			{ true, 0x85C4, 0xf8ed60 },
			{ true, 0x85C5, 0xf8ef40 },
			{ true, 0x85C6, 0xf80a60 },
			{ true, 0x85C7, 0xf84110 },
			{ true, 0x85C8, 0xf84200 },
			{ true, 0x85C9, 0xf842f0 },
			{ true, 0x85CA, 0xf843e0 },
			{ true, 0x85CB, 0xf844d0 },
			{ true, 0x85CC, 0xf847a0 },
			{ true, 0x85CD, 0xf84890 },
			{ true, 0x85CE, 0xf84980 },
			{ true, 0x85CF, 0xf846b0 },
			{ true, 0x85D0, 0xf80b50 },
			{ true, 0x85D1, 0xf80c40 },
			{ true, 0x85D2, 0xf845c0 },
			{ true, 0x85D3, 0xf80d30 },
			{ true, 0x85D4, 0x123f690 },
			{ true, 0x85D5, 0x123f810 },
			{ true, 0x85D6, 0x123fa60 },
			{ true, 0x85D7, 0xf8df50 },
			{ true, 0x85D8, 0xf8db90 },
			{ true, 0x85D9, 0xf87dc0 },
			{ true, 0x85DA, 0xf87cd0 },
			{ true, 0x85DB, 0x10ed880 },
			{ true, 0x85DC, 0xf8e4f0 },
			{ true, 0x85DD, 0xf8b780 },
			{ true, 0x85DE, 0x12610f0 },
			{ true, 0x85DF, 0x126c860 },
			{ true, 0x85E0, 0x126c750 },
			{ true, 0x85E1, 0x1208ad0 },
			{ true, 0x85E2, 0x1208b70 },
			{ true, 0x85E3, 0x125de10 },
			{ true, 0x85E4, 0x1265ed0 },
			{ true, 0x85E5, 0x12395c0 },
			{ true, 0x85E6, 0xf8a6c0 },
			{ true, 0x85E7, 0x125dc90 },
			{ true, 0x85E8, 0x1263cf0 },
			{ true, 0x85E9, 0x12051f0 },
			{ true, 0x85EA, 0x1209000 },
			{ true, 0x85EB, 0x11f43d0 },
			{ true, 0x85EC, 0x11f4470 },
			{ true, 0x85ED, 0x11f4540 },
			{ true, 0x85EE, 0x11f45d0 },
			{ true, 0x85EF, 0x1209540 },
			{ true, 0x85F0, 0xf8e130 },
			{ true, 0x85F1, 0x11ff0f0 },
			{ true, 0x85F2, 0x10148c0 },
			{ true, 0x85F3, 0x126c5c0 },
			{ true, 0x85F4, 0x10ed0c0 },
			{ true, 0x85F5, 0xf88180 },
			{ true, 0x85F6, 0x10ed520 },
			{ true, 0x85F7, 0xf8cc90 },
			{ true, 0x85F8, 0xf8a3e0 },
			{ true, 0x85F9, 0x124f730 },
			{ true, 0x85FA, 0x1014d40 },
			{ true, 0x85FB, 0xf8b5a0 },
			{ true, 0x85FC, 0xf8daa0 },
			{ true, 0x85FD, 0xf88510 },
			{ true, 0x85FE, 0xe97b70 },
			{ true, 0x85FF, 0xe97cd0 },
			{ true, 0x8600, 0x123f4c0 },
			{ true, 0x8601, 0xe97db0 },
			{ true, 0x8602, 0xe97e40 },
			{ true, 0x8603, 0xe968c0 },
			{ true, 0x8604, 0x1245340 },
			{ true, 0x8605, 0xeb1370 },
			{ true, 0x8606, 0xf80e20 },
			{ true, 0x8607, 0xeb17f0 },
			{ true, 0x8608, 0xf8e6d0 },
			{ true, 0x860C, 0x12455c0 },
			{ true, 0x860D, 0xf889a0 },
			{ true, 0x860E, 0xf88a90 },
			{ true, 0x860F, 0xf88b80 },
			{ true, 0x8610, 0x125f750 },
			{ true, 0x8611, 0x125f7f0 },
			{ true, 0x8612, 0x11fc570 },
			{ true, 0x8613, 0x11fc670 },
			{ true, 0x8614, 0x11fc080 },
			{ true, 0x8615, 0x11f5020 },
			{ true, 0x8616, 0xf804c0 },
			{ true, 0x8617, 0x122deb0 },
			{ true, 0x8618, 0xeb1a50 },
			{ true, 0x8619, 0xeb1b10 },
			{ true, 0x861A, 0x126da60 },
			{ true, 0x861B, 0x10100c0 },
			{ true, 0x861C, 0xf8d140 },
			{ true, 0x861D, 0xf8ab50 },
			{ true, 0x861E, 0xe62550 },
			{ true, 0x861F, 0xe62760 },
			{ true, 0x8620, 0x1246360 },
			{ true, 0x8622, 0xf8ae20 },
			{ true, 0x8623, 0xf8d230 },
			{ true, 0x8624, 0xf8ad30 },
			{ true, 0x8625, 0xf8af10 },
			{ true, 0x862A, 0x1014be0 },
			{ true, 0x862B, 0x125f500 },
			{ true, 0x862E, 0x124f350 },
			{ true, 0x862F, 0x124f950 },
			{ true, 0x8630, 0xf852e0 },
			{ true, 0x8631, 0xf853d0 },
			{ true, 0x8632, 0xf8d410 },
			{ true, 0x8633, 0xf8b000 },
			{ true, 0x8634, 0xf8b0f0 },
			{ true, 0x8635, 0xf882d0 },
			{ true, 0x8636, 0x1250eb0 },
			{ true, 0x8637, 0x1250f20 },
			{ true, 0x8639, 0x1251010 },
			{ true, 0x863A, 0x1251320 },
			{ true, 0x863B, 0x1251240 },
			{ true, 0x863C, 0x12513a0 },
			{ true, 0x863D, 0x11f8d40 },
			{ true, 0x863E, 0x11f8ff0 },
			{ true, 0x863F, 0x11f8a00 },
			{ true, 0x8640, 0x11f8c80 },
			{ true, 0x8641, 0x11f9490 },
			{ true, 0x8642, 0xe96c30 },
			{ true, 0x8643, 0xe96d80 },
			{ true, 0x8644, 0xe969a0 },
			{ true, 0x8645, 0xe96ae0 },
			{ true, 0x8646, 0xe96e70 },
			{ true, 0x8647, 0xe97030 },
			{ true, 0x8648, 0xe97210 },
			{ true, 0x8649, 0xe973f0 },
			{ true, 0x864A, 0xe974c0 },
			{ true, 0x864B, 0xe97660 },
			{ true, 0x864C, 0xe977a0 },
			{ true, 0x864D, 0xe97890 },
			{ true, 0x864E, 0xe97aa0 },
			{ true, 0x864F, 0xf811e0 },
			{ true, 0x8650, 0xf812d0 },
			{ true, 0x8651, 0x11dee40 },
			{ true, 0x8652, 0x1239d50 },
			{ true, 0x8653, 0x1239f00 },
			{ true, 0x8654, 0x123a120 },
			{ true, 0x8655, 0x123a5c0 },
			{ true, 0x8656, 0x123a9f0 },
			{ true, 0x8657, 0x12731c0 },
			{ true, 0x8658, 0x123e4f0 },
			{ true, 0x8659, 0x123ec20 },
			{ true, 0x865A, 0x123eec0 },
			{ true, 0x865B, 0x123e9d0 },
			{ true, 0x865C, 0x124fa30 },
			{ true, 0x865D, 0x11f52c0 },
			{ true, 0x865E, 0x11f5380 },
			{ true, 0x865F, 0x11f5440 },
			{ true, 0x8660, 0x11f5500 },
			{ true, 0x8662, 0x123fa70 },
			{ true, 0x8663, 0xeb1b80 },
			{ true, 0x8664, 0x124e4f0 },
			{ true, 0x8665, 0x124e620 },
			{ true, 0x8666, 0x124e750 },
			{ true, 0x8667, 0x124e930 },
			{ true, 0x8668, 0x124dd50 },
			{ true, 0x8669, 0x12535b0 },
			{ true, 0x866A, 0xf84c50 },
			{ true, 0x866B, 0x12403d0 },
			{ true, 0x866C, 0x1240420 },
			{ true, 0x866D, 0x124dea0 },
			{ true, 0x866E, 0x124dff0 },
			{ true, 0x866F, 0x124e160 },
			{ true, 0x8670, 0x124eb10 },
			{ true, 0x8671, 0x124ec50 },
			{ true, 0x8672, 0x124ed90 },
			{ true, 0x8673, 0x124eed0 },
			{ true, 0x8674, 0x123b500 },
			{ true, 0x8675, 0x123bf70 },
			{ true, 0x8676, 0x123bf80 },
			{ true, 0x8677, 0x1014e10 },
			{ true, 0x8678, 0x1203df0 },
			{ true, 0x8679, 0x1203fa0 },
			{ true, 0x867A, 0x1204090 },
			{ true, 0x867C, 0x123c160 },
			{ true, 0x867D, 0x123c170 },
			{ true, 0x867E, 0xf80f10 },
			{ true, 0x867F, 0x123ff80 },
			{ true, 0x8680, 0xf80010 },
			{ true, 0x8682, 0x1240500 },
			{ true, 0x8683, 0x1240760 },
			{ true, 0x8684, 0xf8c2c0 },
			{ true, 0x8685, 0x1240c10 },
			{ true, 0x8686, 0x1240d50 },
			{ true, 0x8687, 0x1240f20 },
			{ true, 0x8688, 0x1241180 },
			{ true, 0x8689, 0x12413d0 },
			{ true, 0x868A, 0x1242900 },
			{ true, 0x868B, 0x12418e0 },
			{ true, 0x868C, 0x1242220 },
			{ true, 0x868D, 0x1242330 },
			{ true, 0x868E, 0x1242790 },
			{ true, 0x868F, 0x1242830 },
			{ true, 0x8690, 0x1230400 },
			{ true, 0x8691, 0x1230420 },
			{ true, 0x8692, 0x12303b0 },
			{ true, 0x8693, 0x122ea20 },
			{ true, 0x8694, 0x1398810 },
			{ true, 0x8695, 0x1398930 },
			{ true, 0x8696, 0x11ff790 },
			{ true, 0x8697, 0x11ff8d0 },
			{ true, 0x8698, 0x1209630 },
			{ true, 0x8699, 0x12096f0 },
			{ true, 0x869A, 0x12097b0 },
			{ true, 0x869B, 0x1209870 },
			{ true, 0x869C, 0x1209930 },
			{ true, 0x869D, 0x12099d0 },
			{ true, 0x869E, 0x123c230 },
			{ true, 0x869F, 0xf813c0 },
			{ true, 0x86A0, 0x11feae0 },
			{ true, 0x86A1, 0x11fb260 },
			{ true, 0x86A2, 0x11fb1d0 },
			{ true, 0x86A3, 0x1338310 },
			{ true, 0x86A4, 0x13375f0 },
			{ true, 0x86A5, 0x1336d50 },
			{ true, 0x86A6, 0x1336de0 },
			{ true, 0x86A7, 0x1336eb0 },
			{ true, 0x86A8, 0xf8a810 },
			{ true, 0x86A9, 0x1266dd0 },
			{ true, 0x86AA, 0x12673f0 },
			{ true, 0x86AB, 0x1398630 },
			{ true, 0x86AC, 0x13984e0 },
			{ true, 0x86AD, 0xf8d320 },
			{ true, 0x86AE, 0xe908e0 },
			{ true, 0x86AF, 0x124d8d0 },
			{ true, 0x86B0, 0x124db50 },
			{ true, 0x86B1, 0x124dcf0 },
			{ true, 0x86B2, 0x1273400 },
			{ true, 0x86B3, 0xf814b0 },
			{ true, 0x86B4, 0x1237210 },
			{ true, 0x86B5, 0x12372a0 },
			{ true, 0x86B6, 0x1236bf0 },
			{ true, 0x86B7, 0x1236a90 },
			{ true, 0x86B8, 0x1236d40 },
			{ true, 0x86B9, 0x1236ea0 },
			{ true, 0x86BA, 0x12369e0 },
			{ true, 0x86BB, 0xf815a0 },
			{ true, 0x86BC, 0xf81690 },
			{ true, 0x86BD, 0x1364100 },
			{ true, 0x86BE, 0xf8a200 },
			{ true, 0x86BF, 0xf8aa60 },
			{ true, 0x86C0, 0x1239b20 },
			{ true, 0x86C1, 0x1239ce0 },
			{ true, 0x86C2, 0x11f93f0 },
			{ true, 0x86C3, 0x1252810 },
			{ true, 0x86C4, 0xf860f0 },
			{ true, 0x86C5, 0xf81780 },
			{ true, 0x86C6, 0x11f90a0 },
			{ true, 0x86C7, 0x1246110 },
			{ true, 0x86C8, 0x12461a0 },
			{ true, 0x86C9, 0xf81870 },
			{ true, 0x86CA, 0xf82310 },
			{ true, 0x86CB, 0xf82220 },
			{ true, 0x86CC, 0xf82400 },
			{ true, 0x86CD, 0x10063b0 },
			{ true, 0x86CE, 0xf861e0 },
			{ true, 0x86CF, 0x123c390 },
			{ true, 0x86D0, 0x123c530 },
			{ true, 0x86D1, 0x123c560 },
			{ true, 0x86D2, 0x123c610 },
			{ true, 0x86D3, 0x1242c90 },
			{ true, 0x86D4, 0x1242d80 },
			{ true, 0x86D5, 0x11f5730 },
			{ true, 0x86D6, 0x11f5840 },
			{ true, 0x86D7, 0x11f5a90 },
			{ true, 0x86D8, 0x11f5bb0 },
			{ true, 0x86D9, 0x11f5cb0 },
			{ true, 0x86DA, 0x1243620 },
			{ true, 0x86DB, 0x1243440 },
			{ true, 0x86DC, 0x1243530 },
			{ true, 0x86DF, 0x11f5950 },
			{ true, 0x86E0, 0x11f59f0 },
			{ true, 0x86E1, 0xf828b0 },
			{ true, 0x86E2, 0xf829a0 },
			{ true, 0x86E3, 0xe47a30 },
			{ true, 0x86E4, 0x11f5db0 },
			{ true, 0x86E5, 0x11f5eb0 },
			{ true, 0x86E6, 0x11fb080 },
			{ true, 0x86E7, 0x11faff0 },
			{ true, 0x86E8, 0x123a410 },
			{ true, 0x86E9, 0x12448f0 },
			{ true, 0x86EA, 0xea56e0 },
			{ true, 0x86EB, 0xea5900 },
			{ true, 0x86EC, 0xea5950 },
			{ true, 0x86ED, 0xea59c0 },
			{ true, 0x86EE, 0xea5a50 },
			{ true, 0x86EF, 0xea5af0 },
			{ true, 0x86F0, 0x11ff3f0 },
			{ true, 0x86F1, 0x11fa8d0 },
			{ true, 0x86F2, 0x125e000 },
			{ true, 0x86F3, 0xf802e0 },
			{ true, 0x86F4, 0x123b9a0 },
			{ true, 0x86F5, 0x123ba90 },
			{ true, 0x86F6, 0x11fcc40 },
			{ true, 0x86F7, 0x1265e80 },
			{ true, 0x86F8, 0x1209bc0 },
			{ true, 0x86FB, 0x1244f60 },
			{ true, 0x86FC, 0x1245050 },
			{ true, 0x86FD, 0x1245110 },
			{ true, 0x86FE, 0x12451e0 },
			{ true, 0x86FF, 0x12452d0 },
			{ true, 0x8700, 0x1243f20 },
			{ true, 0x8701, 0x1244080 },
			{ true, 0x8702, 0x1244280 },
			{ true, 0x8703, 0x1014e90 },
			{ true, 0x8704, 0x1245440 },
			{ true, 0x8705, 0x1273340 },
			{ true, 0x8706, 0x1209b20 },
			{ true, 0x8707, 0x11f4ba0 },
			{ true, 0x8708, 0x11f4c20 },
			{ true, 0x8709, 0x125e970 },
			{ true, 0x870A, 0x1209c80 },
			{ true, 0x870B, 0x1269900 },
			{ true, 0x870C, 0x1014fb0 },
			{ true, 0x870D, 0x11fbc00 },
			{ true, 0x870E, 0xf81960 },
			{ true, 0x870F, 0xf84e30 },
			{ true, 0x8710, 0x1245490 },
			{ true, 0x8711, 0x1245760 },
			{ true, 0x8712, 0xf88090 },
			{ true, 0x8714, 0x1267210 },
			{ true, 0x8715, 0xf81aa0 },
			{ true, 0x8716, 0xf897b0 },
			{ true, 0x8717, 0x12048e0 },
			{ true, 0x8718, 0x12049c0 },
			{ true, 0x8719, 0x1238030 },
			{ true, 0x871A, 0xe95620 },
			{ true, 0x871B, 0xe95430 },
			{ true, 0x871C, 0x1238290 },
			{ true, 0x871D, 0x126e0e0 },
			{ true, 0x871E, 0x1209d50 },
			{ true, 0x871F, 0x11fa970 },
			{ true, 0x8720, 0xeb1640 },
			{ true, 0x8721, 0x1202e00 },
			{ true, 0x8722, 0x11fda20 },
			{ true, 0x8723, 0x11fdaa0 },
			{ true, 0x8724, 0x11fdb20 },
			{ true, 0x8725, 0x11fdba0 },
			{ true, 0x8726, 0x11fb4e0 },
			{ true, 0x8727, 0x1269b40 },
			{ true, 0x8728, 0x11f55c0 },
			{ true, 0x8729, 0x11f5670 },
			{ true, 0x872A, 0x1207eb0 },
			{ true, 0x872B, 0x1207fb0 },
			{ true, 0x872C, 0x1208060 },
			{ true, 0x872D, 0x1208180 },
			{ true, 0x872E, 0x1208230 },
			{ true, 0x872F, 0x1208720 },
			{ true, 0x8730, 0x1237da0 },
			{ true, 0x8731, 0x11f6040 },
			{ true, 0x8732, 0xf8a020 },
			{ true, 0x8733, 0x12088d0 },
			{ true, 0x8734, 0x1338470 },
			{ true, 0x8735, 0x11fa800 },
			{ true, 0x8736, 0xf8e220 },
			{ true, 0x8737, 0xf8e310 },
			{ true, 0x8738, 0xf89120 },
			{ true, 0x8739, 0x11fbcb0 },
			{ true, 0x873A, 0x1262090 },
			{ true, 0x873B, 0x1262110 },
			{ true, 0x873C, 0x1262190 },
			{ true, 0x873D, 0x1262210 },
			{ true, 0x873E, 0x1241a00 },
			{ true, 0x873F, 0x1241dc0 },
			{ true, 0x8740, 0x1241e40 },
			{ true, 0x8741, 0x12420c0 },
			{ true, 0x8742, 0x11fde20 },
			{ true, 0x8743, 0x11fbb40 },
			{ true, 0x8744, 0xea5b00 },
			{ true, 0x8745, 0x1202af0 },
			{ true, 0x8746, 0x1202ea0 },
			{ true, 0x8747, 0x1207700 },
			{ true, 0x8748, 0xeb1d00 },
			{ true, 0x8749, 0x12083b0 },
			{ true, 0x874A, 0x1207c00 },
			{ true, 0x874B, 0x1207cc0 },
			{ true, 0x874C, 0x12072d0 },
			{ true, 0x874D, 0x1208990 },
			{ true, 0x874E, 0x126c8d0 },
			{ true, 0x8750, 0x12085c0 },
			{ true, 0x8751, 0xe97ec0 },
			{ true, 0x8752, 0xe97f70 },
			{ true, 0x8753, 0xe98020 },
			{ true, 0x8754, 0xe980d0 },
			{ true, 0x8755, 0x1015060 },
			{ true, 0x8756, 0x1015090 },
			{ true, 0x8757, 0x10150c0 },
			{ true, 0x8758, 0x1015030 },
			{ true, 0x8759, 0xf81b90 },
			{ true, 0x875A, 0x12735f0 },
			{ true, 0x875B, 0x1015120 },
			{ true, 0x875C, 0x1015180 },
			{ true, 0x875D, 0x10150f0 },
			{ true, 0x875E, 0x1209a70 },
			{ true, 0x8760, 0x123c4a0 },
			{ true, 0x8761, 0x1244a20 },
			{ true, 0x8762, 0x1244c30 },
			{ true, 0x8763, 0x126e700 },
			{ true, 0x8764, 0xea5b80 },
			{ true, 0x8765, 0xea5c20 },
			{ true, 0x8766, 0xea5cd0 },
			{ true, 0x8767, 0xea5dd0 },
			{ true, 0x8768, 0x124e290 },
			{ true, 0x8769, 0x124e3c0 },
			{ true, 0x876A, 0x120a0d0 },
			{ true, 0x876B, 0x120a160 },
			{ true, 0x876C, 0x11fce90 },
			{ true, 0x876D, 0x1207660 },
			{ true, 0x876E, 0x1261da0 },
			{ true, 0x876F, 0x1261e10 },
			{ true, 0x8770, 0x11fb3f0 },
			{ true, 0x8771, 0x11fa730 },
			{ true, 0x8772, 0x1204190 },
			{ true, 0x8773, 0x12041a0 },
			{ true, 0x8774, 0x11f60d0 },
			{ true, 0x8775, 0xea5bd0 },
			{ true, 0x8776, 0x12041b0 },
			{ true, 0x8777, 0x12041c0 },
			{ true, 0x8778, 0xea5d20 },
			{ true, 0x8779, 0xea5d70 },
			{ true, 0x877A, 0x126e650 },
			{ true, 0x877B, 0x1207350 },
			{ true, 0x877C, 0x10151e0 },
			{ true, 0x877D, 0x1015210 },
			{ true, 0x877E, 0x1015240 },
			{ true, 0x877F, 0x10151b0 },
			{ true, 0x8780, 0x10152a0 },
			{ true, 0x8781, 0x1015300 },
			{ true, 0x8782, 0x1015270 },
			{ true, 0x8783, 0x1241980 },
			{ true, 0x8784, 0x12684f0 },
			{ true, 0x8785, 0x126a940 },
			{ true, 0x8786, 0x12159a0 },
		};
	}
}

// Replay GSC lifecycle hook boundaries (IDA-derived).
namespace game::addresses::gsc_api
{
	inline constexpr uintptr_t G_MainMP_RunFrame = 0x121ec80;
	inline constexpr uint8_t G_MainMP_RunFrame_entry[] = { 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89,
		0x70, 0x20, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
	inline constexpr uintptr_t G_ClientMP_Connect = 0x11f1700;
	inline constexpr uint8_t G_ClientMP_Connect_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74,
		0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0x30 };
	inline constexpr uintptr_t SV_ClientMP_DropClient = 0x136f830;
	inline constexpr uint8_t SV_ClientMP_DropClient_entry[] = { 0x40, 0x55, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x81, 0xec, 0x88, 0x02,
		0x00, 0x00, 0x48, 0x8b, 0x05, 0xb3, 0x4b, 0x6e, 0x04, 0x48, 0x33, 0xc4 };
	inline constexpr uintptr_t G_ActiveMP_ClientSpawn = 0x11f0e30;
	inline constexpr uint8_t G_ActiveMP_ClientSpawn_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
		0x41, 0x56, 0x41, 0x57, 0xb8, 0xa0, 0x1d, 0x00, 0x00, 0xe8, 0xa6, 0xc0 };
	inline constexpr uintptr_t G_CombatMP_PlayerDie = 0x1210260;
	inline constexpr uint8_t G_CombatMP_PlayerDie_entry[] = { 0x40, 0x53, 0x55, 0x56, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xec, 0x60, 0x48,
		0x8b, 0x81, 0x50, 0x01, 0x00, 0x00, 0x48, 0x8b, 0xf1, 0x33, 0xdb };
	inline constexpr uintptr_t SV_SendConnectResponseToClient = 0x13767d0;
	inline constexpr uint8_t SV_SendConnectResponseToClient_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x48, 0x81, 0xec, 0x90, 0x02,
		0x00, 0x00, 0x48, 0x8b, 0x05, 0x14, 0xdc, 0x6d, 0x04, 0x48, 0x33, 0xc4, 0x48 };
	inline constexpr uintptr_t SV_ClientMP_FreeClient = 0x1370260;
	inline constexpr uint8_t SV_ClientMP_FreeClient_entry[] = { 0x40, 0x56, 0x48, 0x83, 0xec, 0x30, 0x48, 0x89, 0x5c, 0x24, 0x40, 0x4c,
		0x8b, 0xc1, 0x4c, 0x2b, 0x05, 0x1b, 0xf4, 0xe0, 0x0c, 0x48, 0x89, 0x6c };
	inline constexpr uintptr_t Scr_PlayerKilled = 0x12705e0;
	inline constexpr uint8_t Scr_PlayerKilled_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7c,
		0x24, 0x18, 0x4c, 0x89, 0x74, 0x24, 0x20, 0x41, 0x57, 0x48, 0x83 };
	inline constexpr uintptr_t Persistent_ServerShutdown = 0x137af40;
	inline constexpr uint8_t Persistent_ServerShutdown_entry[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda, 0xe8, 0x62, 0x26,
		0x00, 0x00, 0xe8, 0xad, 0x4e, 0x9f, 0xff, 0xe8, 0xc8, 0x40, 0x00, 0x00 };
	inline constexpr uintptr_t App_ServerShutdown = 0x137ae50;
	inline constexpr uint8_t App_ServerShutdown_entry[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x05, 0xb3, 0x4f, 0xe0, 0x0c,
		0x48, 0x85, 0xc0, 0x74, 0x17, 0x8b, 0x40, 0x14, 0x85, 0xc0, 0x74 };
	inline constexpr uint8_t GScr_FreeEntity_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x91, 0x50,
		0x01, 0x00, 0x00, 0x48, 0x8b, 0xf9, 0x48, 0x8b, 0x1d, 0x65 };
	inline constexpr uint8_t Scr_BeginLoadScripts_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83,
		0xec, 0x20, 0x41, 0x8b, 0xf8, 0x0f, 0xb6, 0xda, 0x48, 0x8b, 0xf1 };
	inline constexpr uint8_t Scr_EndLoadScripts_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9,
		0xe8, 0xee, 0x07, 0x00, 0x00, 0x48, 0x8b, 0xcf, 0xe8, 0x66, 0x3a };
	inline constexpr uintptr_t DirectConnectGameConnectReturn = 0x136F353;
	// Clears ServerStart's state at 0xE195760 after all native finalization.
	// Called by RunFrame (0x137C237) and synchronous startup (0x137E970).
	inline constexpr uintptr_t ServerStart_Finalize = 0x137E290;
	inline constexpr uintptr_t server_start_state = 0xE195760; // uint32: 0 idle; 1/2 startup, 3 restart request.
	inline constexpr uint8_t ServerStart_Finalize_entry[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xe8, 0xd2, 0x1f, 0xc4,
		0xff, 0xe8, 0xcd, 0x2b, 0xf6, 0xff, 0x48, 0x8b, 0xc8, 0xe8, 0x65 };
	inline constexpr uintptr_t G_CmdsMP_SayTo = 0x120C2D0;
	inline constexpr uintptr_t G_CmdsMP_SayToFormatTail = 0x120C4BA;
	inline constexpr uintptr_t G_CmdsMP_SayToSendTail = 0x120C50F;
	inline constexpr uintptr_t SV_Game_BroadcastServerCommand = 0x1377270;
	inline constexpr size_t reliable_command_vtable_slot = 1;
	inline constexpr int reliable_server_command = 1;
}

// Replay compiled-script loading and full-reset ownership (IDA-derived).
namespace game::addresses::gsc
{
	inline constexpr uintptr_t kLoadScriptFileRva = 0xDFE370;
	inline constexpr uintptr_t kPostloadScriptFileRva = 0xE01280;
	inline constexpr uintptr_t kLoadScriptsRva = 0x126ECB0;
	inline constexpr uintptr_t kShutdownScriptsRva = 0x121F880;
	inline constexpr uintptr_t kLinkScriptFileRva = 0x1322350;
	inline constexpr uintptr_t kProcessMapRestartRva = 0x136BF60;
	inline constexpr uintptr_t kAllocZoneMemoryRva = 0x11A8C20;
	inline constexpr uintptr_t kGetScriptProgramRangeRva = 0x11A9500;
	inline constexpr uintptr_t kReleaseXAssetRva = 0xF5F0E0;
	inline constexpr uintptr_t kVarScriptFileRva = 0x5D43670;
	inline constexpr uintptr_t kRestartPendingRva = 0xE192E72;
	inline constexpr uintptr_t kRestartLoadScriptsRva = 0xE192E73;
	inline constexpr uintptr_t kRestartSavegameRva = 0xE192E76;
	inline constexpr uintptr_t kScrLinkFunctionRva = 0x1316800;
	inline constexpr uintptr_t kComErrorRva = 0x12AB4A0;
	inline constexpr uintptr_t kCompileObjectsOffset = 0xC60;
	inline constexpr uintptr_t kProgramLengthOffset = 0xC6C;
	inline constexpr uintptr_t kProgramBufferOffset = 0x3548;
	inline constexpr uintptr_t kProgramChecksumOffset = 0x352C;
	inline constexpr uintptr_t kVMFrameCountOffset = 0xB9B8;
	inline constexpr uintptr_t kLoadingScriptsOffset = 0xC70;
	inline constexpr uintptr_t kChildrenOffset = 0x34C0;
	inline constexpr uintptr_t kChildStride = 0x20;
	inline constexpr uintptr_t kChildTypeOffset = 0xC;
	inline constexpr uintptr_t kChildObjectOffset = 0;
	inline constexpr uint8_t kChildTypeMask = 0x3F;
	inline constexpr uintptr_t kScriptFileAssetType = 0x34;
	inline constexpr uintptr_t kLoadedScriptsOffset = 0xC60;
	inline constexpr uintptr_t kExportsOffset = 0xC64;
	inline constexpr uintptr_t kPendingScriptsOffset = 0xC68;
	inline constexpr uint8_t Load_ScriptFile_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x15, 0xef,
		0x52, 0xf4, 0x04, 0x8b, 0xd9, 0xb9, 0x34, 0x00, 0x00, 0x00 };
	inline constexpr uint8_t Postload_ScriptFile_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x15,
		0xdf, 0x23, 0xf4, 0x04, 0x8b, 0xd9, 0xb9, 0x34, 0x00, 0x00, 0x00 };
	inline constexpr uint8_t GScr_MainMP_LoadScripts_entry[] = { 0x40, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0xb8, 0xb0, 0x77, 0x00,
		0x00, 0xe8, 0x2e, 0xe2, 0xf5, 0x00, 0x48, 0x2b, 0xe0, 0x48, 0x8b, 0x05 };
	inline constexpr uint8_t G_MainMP_ShutdownGame_entry[] = { 0x40, 0x55, 0x48, 0x83, 0xec, 0x20, 0x0f, 0xb6, 0xe9, 0x48, 0x8d, 0x15, 0xa0,
		0x5d, 0x24, 0x01, 0x48, 0x89, 0x5c, 0x24, 0x30, 0x44, 0x8b, 0xc5 };
	inline constexpr uint8_t LinkScriptFile_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x55, 0x57, 0x41, 0x54,
		0x41, 0x56, 0x41, 0x57, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec };
	inline constexpr uint8_t ProcessMapRestart_entry[] = { 0x4c, 0x8b, 0xdc, 0x55, 0x49, 0x8d, 0x6b, 0xe8, 0x48, 0x81, 0xec, 0x10, 0x01,
		0x00, 0x00, 0x48, 0x8b, 0x05, 0x82, 0x84, 0x6e, 0x04, 0x48, 0x33 };
	inline constexpr uint8_t AllocZoneMemory_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
		0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83 };
	inline constexpr uint8_t ReleaseXAsset_entry[] = { 0x48, 0x63, 0xc2, 0x4c, 0x8d, 0x0d, 0x06, 0xdf, 0x49, 0x01, 0x4d, 0x8b, 0x0c, 0xc1,
		0x4d, 0x85, 0xc9, 0x74, 0x06, 0x41, 0x8b, 0xd0, 0x49, 0xff };
	inline constexpr uint8_t Scr_LinkFunction_entry[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74,
		0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x50 };
	inline constexpr uint8_t Com_Error_entry[] = { 0x48, 0x89, 0x54, 0x24, 0x10, 0x4c, 0x89, 0x44, 0x24, 0x18, 0x4c, 0x89, 0x4c, 0x24, 0x20,
		0x48, 0x83, 0xec, 0x28, 0x4c, 0x8d, 0x44, 0x24, 0x40 };
} // namespace game::addresses::gsc

// Runtime bindings previously scattered across feature implementations.
// Numeric literals retain their original C++ type; wrappers/layouts stay with their owners.
namespace game::addresses::runtime
{
	// common/diagnostics.cpp:83
	inline constexpr auto rva_003153fe = 0x3153FE;
	// dedicated/assets/bap/collision.cpp:34
	inline constexpr auto collision_collision_process = 0xD6AEA0;
	// dedicated/assets/bap/collision.cpp:273
	inline constexpr auto rva_00d6bbc0 = 0xD6BBC0;
	// dedicated/assets/bap/collision.cpp:274
	inline constexpr auto rva_00d6bbf7 = 0xD6BBF7;
	// dedicated/assets/bap/collision.cpp:275
	inline constexpr auto rva_00d6bc15 = 0xD6BC15;
	// dedicated/assets/bap/collision.cpp:276
	inline constexpr auto rva_00d6bc46 = 0xD6BC46;
	// network/online_services.cpp:608
	inline constexpr auto rva_00f21db0 = 0xF21DB0;
	// dedicated/assets/bap.cpp:57
	inline constexpr auto bap_allocate = 0xF5DD30;
	// dedicated/assets/bap.cpp:59
	inline constexpr auto bap_free = 0xF5DE40;
	// dedicated/assets/bap.cpp:61
	inline constexpr auto bap_add_to_stash_tail = 0xF5DEF0;
	// dedicated/assets/bap.cpp:63
	inline constexpr auto bap_add_to_table = 0xF5DF60;
	// dedicated/assets/bap.cpp:65
	inline constexpr auto bap_find_asset = 0xF5E010;
	// dedicated/assets/bap.cpp:67
	inline constexpr auto bap_make_first_stashed_entry = 0xF5E350;
	// dedicated/assets/bap.cpp:69
	inline constexpr auto bap_remove_from_table = 0xF5E3D0;
	// dedicated/assets/bap.cpp:71
	inline constexpr auto bap_remove_stashed_asset = 0xF5E4D0;
	// dedicated/assets/bap.cpp:73
	inline constexpr auto bap_allocate_header = 0xF5E5B0;
	// dedicated/assets/bap.cpp:75
	inline constexpr auto bap_free_header = 0xF5F1F0;
	// dedicated/dedicated.cpp:248
	inline constexpr auto script_goal = 0x10063B0;
	// dedicated/dedicated.cpp:252
	inline constexpr auto archive_character = 0x100AB60;
	// dedicated/network/transport.cpp:161
	inline constexpr auto qos_session_call = 0x106128A;
	// auth/profile/challenge_transport.cpp:155
	inline constexpr auto rva_010adea0 = 0x10ADEA0;
	// network/playlists.cpp:43
	inline constexpr auto rva_010c6bd0 = 0x10C6BD0;
	// auth/profile/local_progression.cpp:149
	inline constexpr auto rva_010ca2c0 = 0x10CA2C0;
	// auth/profile/local_progression.cpp:150
	inline constexpr auto rva_010caab0 = 0x10CAAB0;
	// network/playlists.cpp:45
	inline constexpr auto rva_010ce1a0 = 0x10CE1A0;
	// network/playlists.cpp:44
	inline constexpr auto rva_010d3ac0 = 0x10D3AC0;
	// dedicated/bots/bot_population.cpp:141
	inline constexpr auto rva_0118e210 = 0x118E210;
	// network/playlists.cpp:60
	inline constexpr auto rva_011916c0 = 0x11916C0;
	// network/services/join_route.hpp:21
	inline constexpr auto join_route_party_sleep = 0x1191B80;
	// dedicated/headless/non_player.cpp:74
	inline constexpr auto rva_011977a0 = 0x11977A0;
	// network/online_services.cpp:227
	inline constexpr auto party_host_session_call = 0x119E70D;
	// dedicated/headless/non_player.cpp:76
	inline constexpr auto rva_0119e780 = 0x119E780;
	// network/online_services.cpp:227
	inline constexpr auto private_party_session_call = 0x119F163;
	// client/online_frontend.cpp:309
	inline constexpr auto rva_011a7940 = 0x11A7940;
	// diagnostics/fastfile.cpp:13
	inline constexpr auto fastfile_load = 0x11A79F0;
	// dedicated/assets/bap/lifecycle.cpp:368
	inline constexpr auto rva_011ac876 = 0x11AC876;
	// dedicated/assets/bap/hooks.hpp:33
	inline constexpr auto integration_zone_pair_allocated_branch = 0x11AD449;
	// dedicated/assets/asset_mark_guard.cpp:56
	inline constexpr auto rva_011ad8cf = 0x11AD8CF;
	// dedicated/assets/asset_mark_guard.cpp:37
	inline constexpr auto rva_011ad9a5 = 0x11AD9A5;
	// dedicated/assets/bap/collision.cpp:45
	inline constexpr auto collision_glass_piece_state_changed = 0x1212B00;
	// auth/profile/local_progression.cpp:382
	inline constexpr auto rva_0126da60 = 0x126DA60;
	// auth/profile/local_progression.cpp:152
	inline constexpr auto rva_0126eaa0 = 0x126EAA0;
	// auth/profile/challenge_transport.cpp:128
	inline constexpr auto rva_012968b0 = 0x12968B0;
	// auth/profile/challenge_transport.cpp:122
	inline constexpr auto rva_01296960 = 0x1296960;
	// network/playlists.cpp:36
	inline constexpr auto rva_01297140 = 0x1297140;
	// auth/offline_auth.cpp:386
	inline constexpr auto rva_012a1eb0 = 0x12A1EB0;
	// auth/profile/local_progression.cpp:151
	inline constexpr auto rva_012a2110 = 0x12A2110;
	// auth/profile/local_stats.cpp:60
	inline constexpr auto rva_012a2860 = 0x12A2860;
	// auth/profile/local_stats.cpp:59
	inline constexpr auto rva_012a3ee0 = 0x12A3EE0;
	// auth/offline_auth.cpp:40
	inline constexpr auto offline_auth_stats_init = 0x12A4190;
	// diagnostics/join.cpp:26
	inline constexpr auto join_error = 0x12AB1C0;
	// diagnostics/join.cpp:26
	inline constexpr auto join_print = 0x12B0660;
	// diagnostics/join.cpp:405
	inline constexpr auto rva_012b3710 = 0x12B3710;
	// diagnostics/join.cpp:321
	inline constexpr auto rva_012bd870 = 0x12BD870;
	// client/client_console.cpp:164
	inline constexpr auto client_console_is_main_thread = 0x12CF280;
	// dedicated/bots/bot_movement.hpp:29
	inline constexpr auto bot_movement_finish_client_workers = 0x12E40E0;
	// dedicated/bots/bot_movement.hpp:29
	inline constexpr auto bot_movement_queue_client_think = 0x12E41B0;
	// auth/profile/local_progression.cpp:279
	inline constexpr auto rva_013247f0 = 0x13247F0;
	// auth/profile/local_progression.cpp:290
	inline constexpr auto rva_01324c00 = 0x1324C00;
	// auth/profile/local_progression.cpp:285
	inline constexpr auto rva_01324f00 = 0x1324F00;
	// auth/profile/local_progression.cpp:277
	inline constexpr auto rva_01324ff0 = 0x1324FF0;
	// auth/profile/local_progression.cpp:283
	inline constexpr auto rva_01325580 = 0x1325580;
	// dedicated/bots/bot_movement.hpp:6
	inline constexpr auto bot_movement_bot_update_command = 0x1349800;
	// dedicated/dedicated.cpp:237
	inline constexpr auto bot_batch = 0x135ADD0;
	// dedicated/dedicated.cpp:238
	inline constexpr auto bot_movement = 0x135AF90;
	// dedicated/dedicated.cpp:53
	inline constexpr auto dedicated_dev_setup = 0x136B720;
	// dedicated/dedicated.cpp:65
	inline constexpr auto dedicated_dev_setup_system_link = 0x136B7D7;
	// dedicated/runtime/hooks.hpp:10
	inline constexpr auto integration_kick_client = 0x136C160;
	// auth/profile/local_progression.cpp:148
	inline constexpr auto rva_01370c20 = 0x1370C20;
	// auth/profile/local_progression.cpp:147
	inline constexpr auto rva_01370c60 = 0x1370C60;
	// auth/profile/local_progression.cpp:153
	inline constexpr auto rva_01373430 = 0x1373430;
	// dedicated/bots/bot_movement.hpp:29
	inline constexpr auto bot_movement_client_think = 0x1373B00;
	// auth/profile/challenge_transport.cpp:63
	inline constexpr auto rva_013761e0 = 0x13761E0;
	// auth/profile/challenge_transport.cpp:151
	inline constexpr auto rva_0137c670 = 0x137C670;
	// dedicated/bots/bot_population.cpp:134
	inline constexpr auto rva_0137d670 = 0x137D670;
	// dedicated/runtime/hooks.hpp:6
	inline constexpr auto integration_request_server_shutdown = 0x137D6F0;
	// dedicated/assets/bap/collision.cpp:272
	inline constexpr auto rva_0139e9d0 = 0x139E9D0;
	// auth/profile/local_progression.cpp:143
	inline constexpr auto rva_013e2b00 = 0x13E2B00;
	// auth/profile/local_progression.cpp:144
	inline constexpr auto rva_013e2ba0 = 0x13E2BA0;
	// network/playlists.cpp:39
	inline constexpr auto rva_013e62c0 = 0x13E62C0;
	// network/playlists.cpp:38
	inline constexpr auto rva_013e66c0 = 0x13E66C0;
	// network/playlists.cpp:61
	inline constexpr auto rva_013e8b50 = 0x13E8B50;
	// network/playlists.cpp:42
	inline constexpr auto rva_013e97b0 = 0x13E97B0;
	// network/playlists.cpp:37
	inline constexpr auto rva_013e9a20 = 0x13E9A20;
	// network/playlists.cpp:62
	inline constexpr auto rva_013e9be0 = 0x13E9BE0;
	// dedicated/assets/bap/collision.cpp:271
	inline constexpr auto rva_01491980 = 0x1491980;
	// network/native_network.cpp:79
	inline constexpr auto rva_014ef30f = 0x14EF30F;
	// dedicated/assets/bap/collision.cpp:47
	inline constexpr auto collision_glass_get_system_for_initial_piece = 0x150D780;
	// diagnostics/join.cpp:27
	inline constexpr auto join_services = 0x15274C0;
	// auth/offline_auth.cpp:384
	inline constexpr auto rva_01528490 = 0x1528490;
	// network/playlists.cpp:54
	inline constexpr auto rva_015990e0 = 0x15990E0;
	// network/playlists.cpp:56
	inline constexpr auto rva_01599be0 = 0x1599BE0;
	// network/playlists.cpp:57
	inline constexpr auto rva_01599eb0 = 0x1599EB0;
	// network/playlists.cpp:58
	inline constexpr auto rva_0159a160 = 0x159A160;
	// network/playlists.cpp:55
	inline constexpr auto rva_0159a500 = 0x159A500;
	// network/playlists.cpp:59
	inline constexpr auto rva_0159a690 = 0x159A690;
	// auth/profile/challenge_transport.cpp:165
	inline constexpr auto rva_015a26a0 = 0x15A26A0;
	// client/client_console.cpp:160
	inline constexpr auto client_console_draw_console = 0x15AE0B0;
	// client/client_console.cpp:163
	inline constexpr auto client_console_console_active = 0x15B0EF0;
	// client/client_console.cpp:161
	inline constexpr auto client_console_toggle_console = 0x15B18C0;
	// client/client_console.cpp:162
	inline constexpr auto client_console_toggle_output = 0x15B1930;
	// client/client_console.cpp:159
	inline constexpr auto client_console_key_event = 0x15BEB80;
	// network/services/join_route.hpp:16
	inline constexpr auto join_route_start_private_party = 0x15C5710;
	// auth/offline_auth.cpp:39
	inline constexpr auto offline_auth_get_stats_ddl_context = 0x15C7940;
	// auth/offline_auth.cpp:41
	inline constexpr auto offline_auth_profile_login = 0x15D7BB0;
	// client/client_console.cpp:158
	inline constexpr auto client_console_overlay = 0x15E1340;
	// dedicated/assets/bap/collision.cpp:270
	inline constexpr auto rva_015f71c0 = 0x15F71C0;
	// diagnostics/join.cpp:404
	inline constexpr auto rva_01605780 = 0x1605780;
	// diagnostics/join.cpp:252
	inline constexpr auto rva_01605852 = 0x1605852;
	// diagnostics/join.cpp:262
	inline constexpr auto rva_01605857 = 0x1605857;
	// network/services/join_route.hpp:11
	inline constexpr auto join_route_party_join_abort = 0x1606A20;
	// network/online_services.cpp:607
	inline constexpr auto rva_01607550 = 0x1607550;
	// diagnostics/join.cpp:27
	inline constexpr auto join_join_failed = 0x16079D0;
	// diagnostics/join.cpp:407
	inline constexpr auto rva_01608c40 = 0x1608C40;
	// client/clan_tags.cpp:43
	inline constexpr auto rva_016094c0 = 0x16094C0;
	// auth/offline_auth.cpp:383
	inline constexpr auto rva_0165ffa0 = 0x165FFA0;
	// auth/offline_auth.cpp:353
	inline constexpr auto rva_01660280 = 0x1660280;
	// auth/offline_auth.cpp:390
	inline constexpr auto rva_01662c40 = 0x1662C40;
	// auth/offline_auth.cpp:354
	inline constexpr auto rva_01662fc0 = 0x1662FC0;
	// auth/offline_auth.cpp:389
	inline constexpr auto rva_016649f0 = 0x16649F0;
	// auth/offline_auth.cpp:353
	inline constexpr auto rva_01665990 = 0x1665990;
	// auth/offline_auth.cpp:353
	inline constexpr auto rva_01665c10 = 0x1665C10;
	// auth/offline_auth.cpp:388
	inline constexpr auto rva_01665c80 = 0x1665C80;
	// auth/offline_auth.cpp:387
	inline constexpr auto rva_01666030 = 0x1666030;
	// dedicated/headless/headless.cpp:38
	inline constexpr auto headless_set_last_stand_enabled = 0x1778E80;
	// auth/offline_auth.cpp:385
	inline constexpr auto rva_017ec930 = 0x17EC930;
	// auth/offline_auth.cpp:46
	inline constexpr auto offline_auth_stock_demo = 0x19B96A0;
	// client/lua/lua_injection.cpp:36
	inline constexpr auto lua_injection_load_file = 0x19BAEE0;
	// auth/offline_auth.cpp:47
	inline constexpr auto offline_auth_stock_true = 0x1A039E0;
	// auth/profile/local_playercard.cpp:49
	inline constexpr auto rva_01b96540 = 0x1B96540;
	// network/online_services.cpp:185
	inline constexpr auto rva_01b97230 = 0x1B97230;
	// auth/profile/local_playercard.cpp:113
	inline constexpr auto rva_01b972e0 = 0x1B972E0;
	// auth/profile/local_playercard.cpp:122
	inline constexpr auto rva_01b97980 = 0x1B97980;
	// auth/profile/local_playercard.cpp:61
	inline constexpr auto rva_01b992f0 = 0x1B992F0;
	// auth/profile/local_playercard.cpp:54
	inline constexpr auto rva_01b996b0 = 0x1B996B0;
	// client/clan_tags.cpp:45
	inline constexpr auto rva_01b9b9c0 = 0x1B9B9C0;
	// network/playlists.cpp:46
	inline constexpr auto rva_01b9f890 = 0x1B9F890;
	// client/clan_tags.cpp:41
	inline constexpr auto rva_01ba06d0 = 0x1BA06D0;
	// bootstrap/dllmain.cpp:298
	inline constexpr auto splash_load_image_call = 0x1BD347F;
	// dedicated/dedicated.cpp:250
	inline constexpr auto navigation_projection = 0x1FE5030;
	// dedicated/dedicated.cpp:246
	inline constexpr auto reachable_search = 0x1FE5DD0;
	// dedicated/dedicated.cpp:240
	inline constexpr auto nav_context = 0x1FF1990;
	// dedicated/dedicated.cpp:242
	inline constexpr auto nav_reachable = 0x1FF3640;
	// auth/offline_auth.cpp:42
	inline constexpr auto offline_auth_ddl_load_asset = 0x2051180;
	// auth/profile/local_progression.cpp:145
	inline constexpr auto rva_02051750 = 0x2051750;
	// auth/profile/local_progression.cpp:141
	inline constexpr auto rva_02051bf0 = 0x2051BF0;
	// auth/profile/local_progression.cpp:139
	inline constexpr auto rva_02051c70 = 0x2051C70;
	// auth/profile/local_stats.cpp:56
	inline constexpr auto rva_02052150 = 0x2052150;
	// auth/profile/local_progression.cpp:140
	inline constexpr auto rva_02052430 = 0x2052430;
	// auth/profile/local_progression.cpp:142
	inline constexpr auto rva_02052820 = 0x2052820;
	// auth/profile/local_stats.cpp:58
	inline constexpr auto rva_02052ee0 = 0x2052EE0;
	// auth/profile/local_stats.cpp:57
	inline constexpr auto rva_02055710 = 0x2055710;
	// network/playlists.cpp:49
	inline constexpr auto rva_0206ab70 = 0x206AB70;
	// network/playlists.cpp:50
	inline constexpr auto rva_0206afe0 = 0x206AFE0;
	// network/playlists.cpp:48
	inline constexpr auto rva_0206b040 = 0x206B040;
	// network/playlists.cpp:47
	inline constexpr auto rva_0206b170 = 0x206B170;
	// network/playlists.cpp:51
	inline constexpr auto rva_0206b1e0 = 0x206B1E0;
	// client/lua/lua_injection.cpp:40
	inline constexpr auto lua_injection_get_top = 0x20838B0;
	// client/online_frontend.cpp:66
	inline constexpr auto lua_is_number = 0x20839E0;
	// client/online_frontend.cpp:107
	inline constexpr auto lua_push_string = 0x2084120;
	// client/lua/lua_injection.cpp:44
	inline constexpr auto lua_injection_remove = 0x2084420;
	// client/lua/lua_injection.cpp:42
	inline constexpr auto lua_injection_set_top = 0x2084790;
	// client/online_frontend.cpp:137
	inline constexpr auto lua_to_string = 0x2084950;
	// client/online_frontend.cpp:67
	inline constexpr auto lua_to_number = 0x2084A10;
	// client/online_frontend.cpp:389
	inline constexpr auto lua_register_functions = 0x20885C0;
	// client/lua/lua_injection.cpp:38
	inline constexpr auto lua_injection_load = 0x208A6F0;
	// network/native_network.cpp:180
	inline constexpr auto rva_020ec390 = 0x20EC390;
	// network/native_network.cpp:92
	inline constexpr auto rva_0217bbe2 = 0x217BBE2;
	// network/native_network.cpp:178
	inline constexpr auto rva_0217c040 = 0x217C040;
	// dedicated/assets/bap.cpp:41
	inline constexpr auto bap_native_pool_size_table = 0x23FBBA0;
	inline constexpr uintptr_t db_script_reserve_size = 0x2459518;
	inline constexpr uintptr_t db_memory_initialized = 0xC815BF0;
	// auth/profile/local_progression.cpp:88
	inline constexpr auto rva_02458160 = 0x2458160;
	// auth/offline_auth.cpp:45
	inline constexpr auto offline_auth_demo_registration = 0x259C448;
	// auth/profile/local_progression.cpp:261
	inline constexpr auto rva_04483d20 = 0x4483D20;
	// auth/profile/local_progression.cpp:262
	inline constexpr auto rva_04483d28 = 0x4483D28;
	// auth/profile/local_progression.cpp:265
	inline constexpr auto rva_04483d30 = 0x4483D30;
	// auth/profile/local_progression.cpp:266
	inline constexpr auto rva_04483d38 = 0x4483D38;
	// diagnostics/join.cpp:29
	inline constexpr auto join_response_names = 0x459F1D0;
	// auth/offline_auth.cpp:31
	inline constexpr auto offline_auth_auth = 0x4622910;
	// dedicated/assets/bap/collision.cpp:36
	inline constexpr auto collision_collision_state = 0x5CC74F0;
	// auth/profile/local_progression.cpp:86
	inline constexpr auto rva_0b8f5a80 = 0xB8F5A80;
	// client/online_frontend.cpp:284
	inline constexpr auto filesystem = 0xB8F6CB8;
	// dedicated/bots/bot_population.cpp:165
	inline constexpr auto rva_0bc21408 = 0xBC21408;
	// dedicated/bots/bot_movement.cpp:310
	inline constexpr auto game_clients = 0xBC216B8;
	// auth/profile/challenge_transport.cpp:76
	inline constexpr auto rva_0c4ea830 = 0xC4EA830;
	// client/online_frontend.cpp:285
	inline constexpr auto filesystem_zone_count = 0xC4FD8B4;
	// client/online_frontend.cpp:292
	inline constexpr auto filesystem_zone_names = 0xC4FD8D8;
	// dedicated/network/transport.cpp:397
	inline constexpr auto stats_packet_masks = 0xC524E30;
	// network/playlists.cpp:67
	inline constexpr auto playlists_playlist_table = 0xC524EE0;
	// network/playlists.cpp:460
	inline constexpr auto rva_0c542280 = 0xC542280;
	// network/playlists.cpp:66
	inline constexpr auto playlists_entries = 0xC5484D8;
	// network/playlists.cpp:65
	inline constexpr auto playlists_version = 0xC5484DC;
	// network/playlists.cpp:66
	inline constexpr auto playlists_gametype_last = 0xC57A4F8;
	// network/playlists.cpp:167
	inline constexpr auto rva_0c57a500 = 0xC57A500;
	// network/playlists.cpp:475
	inline constexpr auto rva_0c57c500 = 0xC57C500;
	// network/playlists.cpp:141
	inline constexpr auto rva_0c57c510 = 0xC57C510;
	// network/playlists.cpp:134
	inline constexpr auto rva_0c57c910 = 0xC57C910;
	// network/playlists.cpp:401
	inline constexpr auto rva_0c57c920 = 0xC57C920;
	// network/services/join_route.hpp:21
	inline constexpr auto join_route_game_party = 0xC73ADE0;
	// network/playlists.cpp:512
	inline constexpr auto rva_0c796328 = 0xC796328;
	// network/online_services.cpp:156
	inline constexpr auto private_party = 0xC796480;
	// diagnostics/join.cpp:28
	inline constexpr auto join_join_info = 0xC7F1870;
	// auth/profile/local_stats.cpp:252
	inline constexpr auto rva_0d2f36b8 = 0xD2F36B8;
	// auth/offline_auth.cpp:38
	inline constexpr auto offline_auth_online_stats_fetched = 0xD305A34;
	// auth/offline_auth.cpp:37
	inline constexpr auto offline_auth_offline_stats_fetched = 0xD317DB4;
	// network/playlists.cpp:64
	inline constexpr auto playlists_out_of_date = 0xD3278A0;
	// network/playlists.cpp:64
	inline constexpr auto playlists_loaded = 0xD3278A1;
	// network/playlists.cpp:65
	inline constexpr auto playlists_text = 0xD335940;
	// network/playlists.cpp:507
	inline constexpr auto rva_0d386738 = 0xD386738;
	// network/playlists.cpp:508
	inline constexpr auto rva_0d386760 = 0xD386760;
	// network/playlists.cpp:509
	inline constexpr auto rva_0d386768 = 0xD386768;
	// network/playlists.cpp:511
	inline constexpr auto rva_0d386840 = 0xD386840;
	// network/playlists.cpp:510
	inline constexpr auto rva_0d386878 = 0xD386878;
	// network/playlists.cpp:329
	inline constexpr auto rva_0d386948 = 0xD386948;
	// network/playlists.cpp:373
	inline constexpr auto rva_0d386950 = 0xD386950;
	// network/playlists.cpp:329
	inline constexpr auto rva_0d386960 = 0xD386960;
	// diagnostics/join.cpp:320
	inline constexpr auto rva_0d46c238 = 0xD46C238;
	// dedicated/bots/bot_movement.cpp:153
	inline constexpr auto client_workers_enabled = 0xD5CC200;
	// dedicated/bots/bot_movement.cpp:154
	inline constexpr auto client_workers_count = 0xD5CC204;
	// dedicated/bots/bot_movement.cpp:155
	inline constexpr auto client_workers_storage = 0xD5CC208;
	// dedicated/bots/bot_movement.cpp:104
	inline constexpr auto server_clients = 0xE17F690;
	// dedicated/bots/bot_movement.cpp:101
	inline constexpr auto allocated_clients = 0xE17FCD0;
	// auth/profile/challenge_transport.cpp:107
	inline constexpr auto rva_0e17fdc8 = 0xE17FDC8;
	// dedicated/bots/bot_movement.cpp:301
	inline constexpr auto server_state = 0xE17FE10;
	// diagnostics/join.cpp:320
	inline constexpr auto rva_0e36e910 = 0xE36E910;
	// auth/offline_auth.cpp:35
	inline constexpr auto offline_auth_content_finished = 0xE371231;
	// auth/offline_auth.cpp:30
	inline constexpr auto offline_auth_user = 0xE5C0730;
	// network/playlists.cpp:612
	inline constexpr auto rva_0ee56c58 = 0xEE56C58;
	// auth/offline_auth.cpp:43
	inline constexpr auto offline_auth_gamer_settings = 0xEF28AE0;
	// network/playlists.cpp:612
	inline constexpr auto rva_0ef28c0c = 0xEF28C0C;
	// auth/offline_auth.cpp:32
	inline constexpr auto offline_auth_platform_client = 0xF05AC10;
	// auth/offline_auth.cpp:33
	inline constexpr auto offline_auth_platform_xuid = 0xF05ACE8;
	// auth/offline_auth.cpp:34
	inline constexpr auto offline_auth_dev_name = 0xF05AD00;
	// auth/offline_auth.cpp:36
	inline constexpr auto offline_auth_last_bnet_error = 0xF05AD24;
	// auth/offline_auth.cpp:44
	inline constexpr auto offline_auth_lui_vm = 0x11868880;
	// auth/profile/local_playercard.cpp:56
	inline constexpr auto rva_12be4970 = 0x12BE4970;
	// auth/profile/local_playercard.cpp:69
	inline constexpr auto rva_12be4980 = 0x12BE4980;
	// auth/profile/local_playercard.cpp:133
	inline constexpr auto rva_12befa40 = 0x12BEFA40;
	// auth/profile/local_playercard.cpp:66
	inline constexpr auto rva_12c45bdb = 0x12C45BDB;
	// auth/profile/local_playercard.cpp:45
	inline constexpr auto rva_12c46248 = 0x12C46248;
	// Derived pool-slot addresses are catalogued with their native table.
	inline constexpr uintptr_t bap_physics_asset_pool_size = static_cast<uintptr_t>(bap_native_pool_size_table) + sizeof(std::uint32_t) * 3;
	inline constexpr uintptr_t bap_detail_collision_pool_size =
		static_cast<uintptr_t>(bap_native_pool_size_table) + sizeof(std::uint32_t) * 101;

}

namespace replaysymbols
{
	struct Symbol
	{
		uintptr_t rva, size;
		const char* name;
	};

	inline constexpr Symbol symbols[] = {
		{ 0x15E4FC0, 0x286, "CL_MainMP_CheckForResend" },
		{ 0x15CAA50, 0x50, "CL_Screen_Update" },
		{ 0x15CAAA0, 0x3EE, "CL_Screen_UpdateFrame" },
		{ 0xEB35A0, 0x7, "Replay_D3D12CreateDevice" },
		{ 0xEB39A0, 0xA4, "Replay_AGSCreateDevice" },
		{ 0x18F9460, 0x280, "R_DrawBspSurf" },
		{ 0x18F7110, 0x2AB, "R_DrawBspSurf_WORLD_LIT_LMAP" },
		{ 0x18D19C0, 0x130, "R_AddAllBspDrawSurfacesCamera" },
		{ 0x18D1FA0, 0x480, "R_AddBspSurfacesCameraLit" },
		{ 0x18F1EE0, 0x24D, "R_Umbra_SetAllVisible" },
		{ 0xE0C1C0, 0x422, "Load_Material" },
		{ 0xE0CED0, 0x304, "Load_MaterialTechnique" },
		{ 0xE0D2A0, 0x158, "Load_MaterialTechniqueSet" },
		{ 0xECE7E0, 0xBF, "Material_QueueTechniquePipelineCreation" },
		{ 0xEB1370, 0x2C4, "GScr_PlayerSpawn_GetSpawnpointForPlayer" },
		{ 0xE0DB80, 0x135, "Load_SpawnPointRecordList" },
		{ 0xDD1E90, 0xDD, "Load_ScriptStringList" },
		{ 0xDD3940, 0x26, "Load_scr_string_t" },
		{ 0xCE1780, 0x49, "BGScr_IsSubStr" },
		{ 0xDBCF30, 0x138, "Load_ScriptableMapEnts" },
		{ 0x12EC5F0, 0x66, "ScriptableCl_SpatialInitForClient" },
		{ 0x12EDFB0, 0x2C4, "ScriptableCl_UpdateSharedInstanceStates" },
		{ 0x19864E0, 0x137, "R_UpdateActiveStage" },
		{ 0x139A770, 0x320, "SpatialPartition_PopulationSort_SortByDistanceWithViewAngle" },
		{ 0x1399050, 0x94, "SpatialPartition_PopulationSort_GatherPartitions_ViewAngle" },
		{ 0xE54780, 0x1F, "DynEntCL_Spatial_SortByDistanceAndView" },
		{ 0xE0FD30, 0x2D9, "Load_clipMap_t" },
		{ 0xE05480, 0x99, "Load_StageArray" },
		{ 0xDDFAE0, 0x128, "Load_SpatialPartition_Population" },
		{ 0xDDFE70, 0x11F, "Load_SpatialPartition_Population_Tree" },
		{ 0xE303E0, 0x1E6, "Load_GfxWorldDrawVerts" },
		{ 0xE305D0, 0x1E5, "Load_GfxWorldSurfaces" },
		{ 0x1213500, 0xBB, "G_InitGlass" },
		{ 0xDF31E0, 0xA8, "Load_GlassWorld" },
		{ 0xDFD220, 0x145, "Load_G_GlassData" },
		{ 0x10EFBD0, 0x26B, "NetConstStrings_BuildStringMap" },
		{ 0x10F11B0, 0x230, "NetConstStrings_ProcessNetConstStringMap" },
		{ 0xCD5500, 0x83, "BG_Omnvar_ClearTables" },
		{ 0xCD5C80, 0x250, "BG_Omnvar_LoadTables" },
		{ 0xCD5F40, 0x530, "BG_Omnvar_RegisterFromStringTable" },
		{ 0x161AB50, 0x11A, "HavokPhysics_AddShapeList" },
		{ 0x161BFA0, 0x5A0, "HavokPhysics_CreateRigidBody" },
		{ 0x1625310, 0x2CA, "HavokPhysics_InstantiateStaticBody" },
		{ 0x108FF40, 0x116, "Physics_InstantiateStaticBody" },
		{ 0x161C770, 0x474, "HavokPhysics_CreateShapeCompound" },
		{ 0x1490C00, 0xA6, "WorldCollision_AddMapEnts" },
		{ 0x1490A90, 0x42, "WorldCollision_AddClipMap" },
		{ 0x162A540, 0x56, "HavokPhysics_SetMainShapeList" },
		{ 0x11AC760, 0x597, "DB_LinkXAssetEntry" },
		{ 0x11A9C90, 0x14D, "sub_1411A9C90" },
		{ 0xF61690, 0x2E, "Load_MapEntsAsset" },
		{ 0xE0BD90, 0xEF, "Load_MapEntsPtr" },
		{ 0xDBDCC0, 0x1322, "Load_XAssetHeader" },
		{ 0xDBDC70, 0x48, "Load_XAsset" },
		{ 0x11B27E0, 0x240, "sub_1411B27E0" },
		{ 0xF677A0, 0x1CD, "sub_140F677A0" },
		{ 0x11B0260, 0x462, "sub_1411B0260" },
		{ 0x11AFE20, 0x390, "sub_1411AFE20" },
		{ 0x11AF910, 0x9A, "DB_Thread" },
		{ 0x12D0190, 0xC0, "StartAddress" },
		{ 0xD86F00, 0x2B3, "DB_DiscError" },
		{ 0xD87470, 0x193, "DB_ReadXFileInflate" },
		{ 0xD87E40, 0x2A8, "DB_AuthLoad_Inflate" },
		{ 0xD89E10, 0x1F0, "DB_BlockDecode" },
		{ 0x11A7D30, 0x600, "DB_OpenFastfileHeaders" },
		{ 0x11A79F0, 0x33B, "DB_LoadFastfile" },
		{ 0x12AB1C0, 0x2E0, "Com_Error_Internal" },
		{ 0x12AB4A0, 0x30, "Com_Error_impl" },
		{ 0x12B0660, 0x104, "Com_PrintMessageInternal" },
		{ 0x12B0820, 0x92, "Com_Printf" },
		{ 0x12B36A0, 0x68, "Sys_Error_RecordText" },
		{ 0x13FF7A0, 0x10E, "Sys_Error" },
	};

	inline const char* Find(uintptr_t rva, uintptr_t& offset)
	{
		for (const auto& symbol : symbols)
			if (rva >= symbol.rva && rva - symbol.rva < symbol.size)
			{
				offset = rva - symbol.rva;
				return symbol.name;
			}
		return nullptr;
	}
}

