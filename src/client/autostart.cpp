#include "client/autostart.h"

#include "common/console.h"

#include "engine/commands.h"

#include "dedicated/dedicated.h"

#include "engine/game.h"
#include "engine/safemem.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
namespace autostart
{
	namespace
	{
		constexpr const char* kMapDvar = "NSQLTTMRMP";
		constexpr const char* kModeDvar = "MOLPOSLOMO";
			constexpr const char* kMap = "mp_shipment";
			constexpr const char* kMode = "war";

		uintptr_t g_base = 0;
		std::atomic<game::Lua_LoadFileFastFile_t> g_loadFile{ nullptr };
		std::atomic<void*> g_luaState{ nullptr };
		std::atomic<bool> g_complete{ false };
		std::atomic<bool> g_reportedLua{ false };
		std::chrono::steady_clock::time_point g_luaCapturedAt{};

		enum class Stage
		{
			WaitingForLua,
			OpenLocalMultiplayer,
			WaitingForSystemLinkSettings,
			QueueMap,
			WaitingForMap,
			StartMatch,
			Done,
		};
		Stage g_stage = Stage::WaitingForLua;
		unsigned g_stageFrames = 0;
		unsigned g_menuAttempts = 0;
		unsigned g_startAttempts = 0;

		template <size_t N>
		bool Matches(uintptr_t rva, const uint8_t (&expected)[N])
		{
			uint8_t actual[N]{};
			return safemem::ReadBytes(reinterpret_cast<const void*>(g_base + rva), actual, N) && std::memcmp(actual, expected, N) == 0;
		}

		bool ValidateRuntime()
		{
			return safemem::RegionExecutable(reinterpret_cast<const void*>(g_base + game::kLUIOpenMenuRVA)) &&
				Matches(game::kLUIOpenMenuRVA, game::kLUIOpenMenuPrologue) &&
				Matches(game::kLuaUIStartServerWrapperRVA, game::kLuaUIStartServerWrapperPrologue) &&
				Matches(game::kLuaGetTopRVA, game::kLuaGetTopPrologue) && Matches(game::kLuaSetTopRVA, game::kLuaSetTopPrologue) &&
				Matches(game::kLuaPushNumberRVA, game::kLuaPushNumberPrologue) &&
				Matches(game::kDvarGetStringRVA, game::kDvarGetStringPrologue) &&
				Matches(game::kDvarSetBoolInternalRVA, game::kDvarSetBoolInternalPrologue) &&
				Matches(game::kEnterCriticalRVA, game::kEnterCriticalPrologue) &&
				Matches(game::kLeaveCriticalRVA, game::kLeaveCriticalPrologue) &&
				Matches(game::kSystemLinkPredicateRVA, game::kSystemLinkPredicatePrologue) &&
				Matches(game::kGetSystemLinkPartyRVA, game::kGetSystemLinkPartyPrologue);
		}

		bool QueueCommand(const char* text)
		{
			return commands::Queue(g_base, text);
		}

		bool ReadDvar(const char* token, char* out, size_t outSize)
		{
			const auto getString = reinterpret_cast<game::Dvar_GetString_t>(g_base + game::kDvarGetStringRVA);
			return safemem::ReadString(getString(token), out, outSize) != 0;
		}

		bool MapReady()
		{
			char map[64]{};
			char mode[32]{};
				return ReadDvar(kMapDvar, map, sizeof(map)) && ReadDvar(kModeDvar, mode, sizeof(mode)) && std::strcmp(map, kMap) == 0 &&
					std::strcmp(mode, kMode) == 0;
		}

		void* GetSystemLinkParty()
		{
			const auto getParty = reinterpret_cast<game::GetSystemLinkParty_t>(g_base + game::kGetSystemLinkPartyRVA);
			return getParty();
		}

		bool StartWasAccepted(void* party)
		{
			if (!party) return false;
			uint8_t requested = 0;
			const auto* flag = reinterpret_cast<const uint8_t*>(party) + game::kPartyStartRequestedOffset;
			return safemem::ReadBytes(flag, &requested, sizeof(requested)) && requested != 0;
		}

		struct BoolDvar
		{
			const uint8_t* address = nullptr;
			bool value = false;
		};

		bool ReadBoolDvar(uintptr_t pointerRva, BoolDvar& dvar)
		{
			dvar = {};
			uint8_t type = 0;
			uint8_t value = 0;
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(g_base + pointerRva), &dvar.address, sizeof(dvar.address)) ||
				!dvar.address || !safemem::ReadBytes(dvar.address + game::kDvarTypeOffset, &type, sizeof(type)) ||
				type != 0 || // DVAR_TYPE_BOOL
				!safemem::ReadBytes(dvar.address + game::kDvarCurrentOffset, &value, sizeof(value)))
				return false;
			dvar.value = value != 0;
			return true;
		}

		void LogSystemLinkSettings()
		{
			BoolDvar systemLink{}, lanLobby{};
			const bool haveSystemLink = ReadBoolDvar(game::kSystemLinkDvarRVA, systemLink);
			const bool haveLanLobby = ReadBoolDvar(game::kLanLobbyEnabledDvarRVA, lanLobby);
			LOG_WARN("AutoStart", "system-link settings: systemlink=%s, com_lan_lobby_enabled=%s",
				haveSystemLink ? (systemLink.value ? "true" : "false") : "unavailable",
				haveLanLobby ? (lanLobby.value ? "true" : "false") : "unavailable");
		}

		bool ConfigureSystemLink()
		{
			BoolDvar systemLink{}, lanLobby{};
			if (!ReadBoolDvar(game::kSystemLinkDvarRVA, systemLink) || !ReadBoolDvar(game::kLanLobbyEnabledDvarRVA, lanLobby)) return false;

			// Registration overrides only change defaults. Apply the current values
			// at the menu transition through the engine setter (preserving its dvar
			// bookkeeping), since startup/menu code can reset systemlink afterwards.
			const auto setBool = reinterpret_cast<game::Dvar_SetBool_Internal_t>(g_base + game::kDvarSetBoolInternalRVA);
			if (!systemLink.value) setBool(systemLink.address, true);
			if (!lanLobby.value) setBool(lanLobby.address, true);

			BoolDvar currentSystemLink{}, currentLanLobby{};
			if (!ReadBoolDvar(game::kSystemLinkDvarRVA, currentSystemLink) ||
				!ReadBoolDvar(game::kLanLobbyEnabledDvarRVA, currentLanLobby) || !currentSystemLink.value || !currentLanLobby.value)
				return false;

			if (!systemLink.value || !lanLobby.value)
			{
				LOG_INFO("AutoStart", "applied system-link settings: systemlink=%d->1, com_lan_lobby_enabled=%d->1",
					static_cast<int>(systemLink.value), static_cast<int>(lanLobby.value));
			}
			return true;
		}

		bool OpenLocalMultiplayer()
		{
			// The native opener uses the global LUI VM, not the state captured from
			// the fastfile loader. Wait until that global is available as well.
			void* luiVM = nullptr;
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(g_base + game::kLUILuaVMRVA), &luiVM, sizeof(luiVM)) || !luiVM)
				return false;

			if (!ConfigureSystemLink()) return false;

			const auto openMenu = reinterpret_cast<game::LUI_OpenMenu_t>(g_base + game::kLUIOpenMenuRVA);
			openMenu(0, "MPSystemLinkMenu", 0, 0, 0);
			return true;
		}

		int __fastcall DetourLoadFile(void* luaState, const char* filename)
		{
			if (luaState)
			{
				g_luaState.store(luaState, std::memory_order_release);
				if (!g_reportedLua.exchange(true, std::memory_order_relaxed))
				{
					g_luaCapturedAt = std::chrono::steady_clock::now();
					char name[160]{};
					safemem::ReadString(filename, name, sizeof(name));
					LOG_INFO("AutoStart", "captured Replay LUI lua_State from fastfile loader%s%s", name[0] ? ": " : "", name);
				}
			}

			const auto original = g_loadFile.load(std::memory_order_acquire);
			return original ? original(luaState, filename) : 6;
		}

		void Advance(Stage next)
		{
			g_stage = next;
			g_stageFrames = 0;
		}
	}

	hook::Status Install(uintptr_t moduleBase)
	{
		if (dedicated::Enabled()) return hook::Status::Installed;
		if (g_loadFile.load(std::memory_order_acquire)) return hook::Status::Installed;

		g_base = moduleBase;
		if (!ValidateRuntime())
		{
			LOG_ERR("AutoStart", "Replay Lua/frontend target bytes do not match the supported build");
			return hook::Status::Failed;
		}

		auto* target = reinterpret_cast<void*>(moduleBase + game::kLuaLoadfileFastfileRVA);
		const auto status =
			hook::Install(target, &DetourLoadFile, game::kLuaLoadfileFastfilePrologue, game::kLuaLoadfileFastfileStolen, g_loadFile);
		if (status == hook::Status::Installed)
		{
					LOG_INFO("AutoStart", "Shipment autostart armed: Lua loader RVA 0x%llX, UI_StartServer wrapper RVA 0x%llX",
						static_cast<unsigned long long>(game::kLuaLoadfileFastfileRVA),
				static_cast<unsigned long long>(game::kLuaUIStartServerWrapperRVA));
		}
		return status;
	}

	void Pump()
	{
		if (dedicated::Enabled()) return;
		if (g_complete.load(std::memory_order_acquire) || !g_base) return;

		++g_stageFrames;
		void* luaState = g_luaState.load(std::memory_order_acquire);
		if (!luaState)
		{
			g_stage = Stage::WaitingForLua;
			return;
		}

		if (g_luaCapturedAt.time_since_epoch().count() != 0 &&
			std::chrono::steady_clock::now() - g_luaCapturedAt < std::chrono::seconds(10))
			return;

		const auto systemLink = reinterpret_cast<game::SystemLinkPredicate_t>(g_base + game::kSystemLinkPredicateRVA);

		// Do not gate this transition on Replay's old native IsFrontEnd predicate.
		// Call the native LUI_OpenMenu entry point directly once the LUI VM exists.
		if (g_stage == Stage::WaitingForLua) Advance(Stage::OpenLocalMultiplayer);

		if (g_stage == Stage::OpenLocalMultiplayer)
		{
			// Configure the current LAN dvars before requesting the menu. Merely
			// sending restoreMenu does not enable system-link mode.
			if (g_menuAttempts && g_stageFrames < 60) return;
			g_stageFrames = 0;
			++g_menuAttempts;

			if (OpenLocalMultiplayer())
			{
				LOG_INFO("AutoStart", "LUI_OpenMenu(0, MPSystemLinkMenu, 0, 0, 0) called");
				Advance(Stage::WaitingForSystemLinkSettings);
			}
			else if (g_menuAttempts == 1 || (g_menuAttempts % 10) == 0)
			{
				LOG_WARN("AutoStart", "LUI VM/system-link settings not ready (attempt %u)", g_menuAttempts);
				LogSystemLinkSettings();
			}
			return;
		}

		if (g_stage == Stage::WaitingForSystemLinkSettings)
		{
			if (systemLink())
			{
				LOG_INFO("AutoStart", "systemlink and com_lan_lobby_enabled current values confirmed");
				Advance(Stage::QueueMap);
			}
			else if (g_stageFrames > 600)
			{
				LogSystemLinkSettings();
				LOG_WARN("AutoStart", "system-link settings were reset after the menu request; reapplying before retry");
				Advance(Stage::OpenLocalMultiplayer);
			}
			return;
		}

		if (g_stage == Stage::QueueMap)
		{
			if (MapReady())
			{
						LOG_INFO("AutoStart", "Shipment/map mode already selected");
						Advance(Stage::StartMatch);
					}
					else if (QueueCommand("set NSQLTTMRMP mp_shipment\nset MOLPOSLOMO war\n"))
					{
						LOG_INFO("AutoStart", "queued ui_mapname=mp_shipment and mode=war through native Cbuf");
						Advance(Stage::WaitingForMap);
					}
			return;
		}

		if (g_stage == Stage::WaitingForMap)
		{
			if (MapReady())
			{
						LOG_INFO("AutoStart", "Shipment selection confirmed by Replay dvar readback");
				Advance(Stage::StartMatch);
			}
			else if (g_stageFrames > 300)
			{
						LOG_WARN("AutoStart", "Shipment dvar readback timed out; re-queueing selection");
				Advance(Stage::QueueMap);
			}
			return;
		}

		if (g_stage != Stage::StartMatch) return;

		// UI_StartServer itself checks this exact Replay predicate before it
		// obtains the system-link party. Mirror that order here before touching the
		// party accessor or its request flag.
		if (!systemLink()) return;

		// Resolve the same party pointer UI_StartServer will use while the verified
		// predicate is true, then keep that pointer for the synchronous acceptance
		// check even if the start call begins changing frontend state.
		void* party = GetSystemLinkParty();
		if (!party) return;
		if (StartWasAccepted(party))
		{
			g_complete.store(true, std::memory_order_release);
			g_stage = Stage::Done;
			LOG_INFO("AutoStart", "Replay already reports a pending start request; native map loading continues");
			return;
		}

		// The registered C wrapper indexes its first Lua argument at stack slot 1.
		// Invoke it only while the idle frontend stack is empty; this avoids
		// overwriting or re-indexing any live Lua call frame.
		const auto getTop = reinterpret_cast<game::Lua_GetTop_t>(g_base + game::kLuaGetTopRVA);
		if (getTop(luaState) != 0) return;

		// Retry no faster than once every 30 frontend frames if the party rejects
		// the request while the system-link lobby is still settling.
		if (g_startAttempts && g_stageFrames < 30) return;
		g_stageFrames = 0;
		++g_startAttempts;

		const auto pushNumber = reinterpret_cast<game::Lua_PushNumber_t>(g_base + game::kLuaPushNumberRVA);
		const auto setTop = reinterpret_cast<game::Lua_SetTop_t>(g_base + game::kLuaSetTopRVA);
		const auto startServer = reinterpret_cast<game::Lua_UIStartServer_t>(g_base + game::kLuaUIStartServerWrapperRVA);

		pushNumber(luaState, 0.0); // controller/local-client argument #1
		startServer(luaState); // optional boolean argument #2 is omitted -> false
		setTop(luaState, 0);

		if (StartWasAccepted(party))
		{
			g_complete.store(true, std::memory_order_release);
			g_stage = Stage::Done;
					LOG_INFO("AutoStart", "stock Lua UI_StartServer path accepted Shipment start request; native map loading continues");
		}
		else if (g_startAttempts == 1 || (g_startAttempts % 10) == 0)
		{
			int host = -1, inParty = -1, active = -1;
			const auto* data = static_cast<const uint8_t*>(party);
			safemem::ReadBytes(data + game::kPartyHostOffset, &host, sizeof(host));
			safemem::ReadBytes(data + game::kPartyInPartyOffset, &inParty, sizeof(inParty));
			safemem::ReadBytes(data + game::kPartyActiveOffset, &active, sizeof(active));
			LOG_WARN("AutoStart", "UI_StartServer request not accepted (attempt %u): party active=%d, inParty=%d, host=%d", g_startAttempts,
				active, inParty, host);
		}
	}

	bool Complete()
	{
		return g_complete.load(std::memory_order_acquire);
	}
}
