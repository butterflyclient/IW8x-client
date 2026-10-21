#include "auth/dvar_patches.h"
#include "auth/island_br_dvar_policy.hpp"

#include "common/console.h"

#include "engine/game.h"
#include "engine/inline_hook.h"
#include "engine/safemem.h"

#include "common/state.h"

#include "dedicated/dedicated.h"
#include "network/online_services.h"
#include "client/online_frontend.h"
#include "network/native_network.h"

#include <atomic>
#include <array>
#include <cstring>
#include <string>
namespace dvars
{
	namespace
	{
		struct FenceOverride
		{
			const char* token;
			const char* name;
			bool value;
		};

		// Startup fences and reversible local unlock switches for Replay.
		constexpr FenceOverride kFenceOverrides[] = {
			{ "MLNMPQOON", "cg_viewedSplashScreen", true },
			{ "MTSTMKPMRM", "ui_onlineRequired", false },
			// CODACCOUNT.IsEnabled gates the Activision greeting/login/signup UI.
			{ "MNMLRKRSSL", "enable_cod_account", false },
			{ "LPNMMPKRL", "com_lan_lobby_enabled", true },
			{ "RLSPOOTTT", "com_checkIfGameModeInstalled", false },
			{ "MROLPRPTPO", "com_force_premium", true },
			{ "LSTQOKLTRN", "force_offline_menus", true },
			// Target 1.20 hash table and IW8-1.20 reference confirm this companion switch.
			{ "MPSSOTQQPM", "force_offline_enabled", true },
			{ "OKLQKPPKPQ", "con_bindableGrave", false },
			{ "MOQQSONTOT", "con_restricted", false },
			{ "NRNSMTPL", "com_hide_console", false },
			// FenceGamerProfile uses this to accept and save the current EULA version.
			{ "LKSQOLNKLP", "lui_skip_boot_flow", true },
			// Replay's own name/token mapping (RVA 0xc68704/0xc68713). Keep the skip
			// active even when launching the EXE directly; launchers also execute seta.
			{ "NKMQMOKNRP", "lui_skip_season_video", true },
			{ "LMMRONPQMO", "lui_force_online_menus", false },
			{ "LNTOKPTKS", "lui_cod_points_enabled", true },
			{ "LSSRRSMNMR", "lui_dev_features_enabled", true },
			{ "LRKPTLNQTT", "lui_enable_magma_blade_layout", false },
			{ "LSPSKLPNQT", "lui_wz_tutorial_optional", true },
			{ "", "lui_enable_frontend_seasonal_content", true }, // not in 1.24 dump; match by name
			{ "LTOQRQMMLQ", "online_lan_cross_play", true },
			{ "NTTRLOPQKS", "xp_dec_dc", false },
			{ "NSPPTONLNP", "online_blueprints_enabled", false },
			{ "LPSPMQSNPQ", "systemlink", true },
			{ "LLPNKKORPT", "systemlink_host", true },
			// Hinatyu/1.67 reference forces this true (with force_offline_menus + lui_dev_features_enabled) â€”
			// "logged in to Xbox Live" flag some offline fences read. Harmless; completes the reference recipe.
			{ "LLOKQOSPPP", "xblive_loggedin", true },
			// CONDITIONS.ShouldCheckDLC() = Dvar.GetBool("LKSTRMKTML"); force FALSE so the offline MP
			// mode-fence skips the (erroneously-failing offline) "missing MP DLC packs" check.
			{ "LKSTRMKTML", "should_check_dlc", false },

			// Default off; SetUnlockAll changes these together with the Lua predicates.
			{ "OLKMKMTKRO", "unlockAllItems", false },
			{ "MNLPOPMMSK", "force_unlock_all_attachments", false },
			{ "LSPQSSPSOL", "force_unlock_all_attachment_lines", false },
			{ "NQRLNKMTSL", "force_unlock_all_killstreaks", false },
		};

		std::atomic<game::Dvar_RegisterBool_t> g_original{ nullptr };
		// TU19 native Com_FrontEndScene_Init (0x10c66b2) registers this string through
		// 0x13e7a70. The converted Season 2 scene is mp_frontend2; keep its own name so
		// the engine resolves the matching techsets, language, scripts and streams.
		using RegisterStringFn = void*(__fastcall*)(const char*, const char*, unsigned, const char*);
		std::atomic<RegisterStringFn> g_stringOriginal{ nullptr };
		constexpr uint8_t kRegisterStringBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec,
			0x60 };
		bool g_onlineFrontend = false;
		bool g_dedicated = false;
		uintptr_t g_base = 0;
		std::atomic<bool> g_unlockAll{ false };
		std::array<std::atomic<void*>, 4> g_unlockDvars{};
		constexpr const char* kUnlockNames[] = { "unlockAllItems", "force_unlock_all_attachments", "force_unlock_all_attachment_lines",
			"force_unlock_all_killstreaks" };

		int UnlockIndex(const FenceOverride* entry)
		{
			if (entry)
				for (int i = 0; i < 4; ++i)
					if (!strcmp(entry->name, kUnlockNames[i])) return i;
			return -1;
		}

		void* __fastcall RegisterString(const char* name, const char* value, unsigned flags, const char* description)
		{
#ifndef REPLAY_DISABLE_CUSTOM_CONTENT
			if (!g_dedicated && name && (!strcmp(name, "LORSOSSMRS") || !strcmp(name, "frontEndFastfileName")))
			{
				wchar_t path[MAX_PATH]{}, scene[32]{};
				if (native_network::ConfigurationPath(path, MAX_PATH))
					GetPrivateProfileStringW(L"ui", L"frontend_scene", L"1.16", scene, 32, path);
				if (!scene[0] || !wcscmp(scene, L"1.16"))
				{
					value = "mp_frontend2";
					LOG_INFO("FrontendScene120", "selected offline-converted Season 2 / 1.16 scene: mp_frontend2");
				}
				else if (!wcscmp(scene, L"1.24"))
				{
					value = "mp_frontend5";
					LOG_INFO("FrontendScene120", "selected offline-converted 1.24 scene: mp_frontend5");
				}
				else if (!wcscmp(scene, L"1.28-test"))
				{
					value = "mp_frontend6";
					LOG_INFO("FrontendScene120", "selected isolated 1.28 test scene: mp_frontend6");
				}
				else if (!wcscmp(scene, L"1.28-halloween"))
				{
					value = "mp_frontend6b";
					LOG_INFO("FrontendScene120", "selected original 1.28 Halloween scene: mp_frontend6b");
					// Original mp_frontend6b/main reads this exact source token with
					// getdvarint before initializing its seasonal scriptable state.
					// Replay has no corresponding registrar. Keep the original script
					// and supply its bool only for this selected client scene. Its
					// play_fx function uses the same value for pumpkins and scarecrows.
					auto* seasonal =
						g_original.load(std::memory_order_acquire)("MONKPPPQR", true, 0, "Enable original 1.28 Halloween scene props");
					LOG_INFO("FrontendScene120", "original Halloween seasonal-content setting registered=%d", seasonal != nullptr);
				}
				else
				{
					LOG_INFO("FrontendScene120", "using stock frontend (ui.frontend_scene=%ls)", scene);
				}
			}
#endif
			return g_stringOriginal.load(std::memory_order_acquire)(name, value, flags, description);
		}

		const FenceOverride* FindOverride(const char* name)
		{
			if (!name || !*name) return nullptr;

			for (const auto& entry : kFenceOverrides)
			{
				if ((entry.token[0] && std::strcmp(name, entry.token) == 0) || std::strcmp(name, entry.name) == 0) return &entry;
			}
			return nullptr;
		}

		void* __fastcall Detour(const char* name, bool value, unsigned int flags, const char* description)
		{
			state::boolDvarsSeen.fetch_add(1, std::memory_order_relaxed);
			char safeName[96]{};
			safemem::ReadString(name, safeName, sizeof(safeName));

			const auto* overrideValue = FindOverride(safeName);
			bool finalValue = overrideValue ? overrideValue->value : value;
			if (g_dedicated && overrideValue && !strcmp(overrideValue->name, "systemlink")) finalValue = false;
			// These choose the frontend route, not transport or master connectivity.
			// Keep the established offline/dedicated policy when no master is configured.
			if (g_onlineFrontend && overrideValue)
			{
				const auto* key = overrideValue->name;
				if (!strcmp(key, "force_offline_menus") || !strcmp(key, "force_offline_enabled"))
					finalValue = false;
				// LAN flags are selected by stock menu actions; backend configuration
				// must not force them off when a local game mode registers its dvars.
				else if (!strcmp(key, "systemlink") || !strcmp(key, "systemlink_host") || !strcmp(key, "lui_force_online_menus") ||
					!strcmp(key, "com_force_premium") || !strcmp(key, "xblive_loggedin"))
					finalValue = value; // native defaults; the provider owns readiness/entitlements
				if (!strcmp(key, "systemlink") && online_frontend::local_play()) finalValue = true;
			}

			const int unlockIndex = UnlockIndex(overrideValue);
			if (g_onlineFrontend && flags == 0x14 && !strcmp(safeName, "LQKPQMPRQN"))
			{
				const auto hash = reinterpret_cast<uint32_t (*)(const char*)>(g_base + 0x13e66c0)(name);
				const auto existing = reinterpret_cast<uintptr_t (*)(uint32_t)>(g_base + 0x13e62c0)(hash);
				uint32_t oldFlags = 0;
				uint8_t type = 0;
				if (existing && safemem::ReadBytes(reinterpret_cast<void*>(existing + 24), &oldFlags, 4) &&
					safemem::ReadBytes(reinterpret_cast<void*>(existing + 29), &type, 1) && oldFlags == 0x100 && type == 9)
				{
					// TU19 CG registrar1796A60 owns this script bool (flags0x14).
					// During native BR handoff it is deregistered, then playlist 'set'
					// recreates it as external string0x100. Reregister converts its type
					// but checks script permission before merging the native flags.
					// Restore only that verified permission; let native conversion keep
					// the value and enforce all remaining type/permission checks.
					const uint32_t scriptExternal = oldFlags | 0x10;
					if (safemem::WriteBytes(reinterpret_cast<void*>(existing + 24), &scriptExternal, 4))
						LOG_INFO("Fence", "restored native script permission for external cg_everyoneHearsEveryone");
				}
			}
			if (overrideValue && !strcmp(overrideValue->name, "lui_skip_season_video")) finalValue = online_frontend::SkipIntroCinematics();
			if (unlockIndex >= 0) finalValue = g_unlockAll.load(std::memory_order_relaxed);
			void* result = g_original.load(std::memory_order_acquire)(name, finalValue, flags, description);
			if (unlockIndex >= 0) g_unlockDvars[unlockIndex].store(result, std::memory_order_release);
			if (overrideValue) LOG_INFO("Fence", "%s -> %s", overrideValue->name, finalValue ? "true" : "false");
			return result;
		}
	}

	unsigned RestoreIslandBRScriptPermissions()
	{
#ifndef IW8X_ISLAND_PORT_PROBE
		return 0;
#else
		// Native 1.20.4 analog of the existing TU19 cg_everyoneHearsEveryone
		// external-script permission repair above. This is called only for
		// selected Island dedicated BR after native playlist config has run.
		// Do not change a typed engine dvar, other flags, values or ownership.
		if (!g_base || !g_dedicated) return 0;
		char mode[2]{};
		if (GetEnvironmentVariableA("IW8X_ISLAND_DVAR_WRITE_164", mode, sizeof(mode)) != 1 || mode[0] != '1')
			return 0;
		const auto lookup_hash = reinterpret_cast<uint32_t (*)(const char*)>(g_base + 0x13e66c0);
		const auto lookup_dvar = reinterpret_cast<uintptr_t (*)(uint32_t)>(g_base + 0x13e62c0);
		unsigned restored = 0;
		for (const auto name : island_br_policy::source_script_settings)
		{
			const std::string owned{name};
			const auto record = lookup_dvar(lookup_hash(owned.c_str()));
			if (!record) continue;
			uint32_t flags{};
			uint8_t type{};
			if (!safemem::ReadBytes(reinterpret_cast<const void*>(record + 24), &flags, sizeof(flags)) ||
				!safemem::ReadBytes(reinterpret_cast<const void*>(record + 29), &type, sizeof(type)) ||
				!island_br_policy::restore_allowed(name, flags, type)) continue;
			const uint32_t permitted = flags | island_br_policy::script_permission_flag;
			if (safemem::WriteBytes(reinterpret_cast<void*>(record + 24), &permitted, sizeof(permitted)))
			{
				++restored;
				LOG_INFO("Fence", "Island original BR script permission restored: %s external flags=0x%X -> 0x%X",
					owned.c_str(), flags, permitted);
			}
		}
		return restored;
#endif
	}

	void SetUnlockAll(bool enabled)
	{
		g_unlockAll.store(enabled, std::memory_order_relaxed);
		for (auto& entry : g_unlockDvars)
			if (void* dvar = entry.load(std::memory_order_acquire))
				reinterpret_cast<game::Dvar_SetBool_Internal_t>(g_base + game::kDvarSetBoolInternalRVA)(dvar, enabled);
	}

	hook::Status Install(uintptr_t moduleBase)
	{
		g_base = moduleBase;
		const bool alreadyInstalled = g_original.load(std::memory_order_acquire) != nullptr;
		if (!alreadyInstalled)
		{
			g_dedicated = dedicated::Enabled();
			g_onlineFrontend = !dedicated::Enabled() && online_services::Configured();
		}
		auto* target = reinterpret_cast<void*>(moduleBase + game::kDvarRegisterBoolRVA);
		const auto status = hook::Install(target, &Detour, game::kDvarRegisterBoolPrologue, game::kDvarRegisterBoolStolen, g_original);
		if (status == hook::Status::Installed && !alreadyInstalled)
			LOG_INFO("Fence", "Dvar_RegisterBool hook installed at RVA 0x%llX; %zu fence overrides active",
				static_cast<unsigned long long>(game::kDvarRegisterBoolRVA), sizeof(kFenceOverrides) / sizeof(kFenceOverrides[0]));
		if (status != hook::Status::Installed || g_dedicated) return status;
		return hook::Install(reinterpret_cast<void*>(moduleBase + 0x13e7a70), &RegisterString, kRegisterStringBytes,
			sizeof(kRegisterStringBytes), g_stringOriginal);
	}
}
