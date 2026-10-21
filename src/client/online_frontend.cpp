#include "client/clan_tags.h"

#include "engine/addresses.hpp"

#include "diagnostics/weapons/weapon_files.h"
#include "client/weapons/weapon_loadouts.h"
#include "auth/profile/local_progression.h"
#include "auth/profile/local_challenges.h"
#include "auth/profile/xp_tokens.h"
#include "auth/profile/local_stats.h"
#include "auth/profile/local_settings.h"
#include "network/backend/customization_names.h"

#include "engine/noclip.h"

#include "diagnostics/night/night_loot.h"

#include "engine/camo_changer.h"

#include "client/client_console.h"

#include <ctime>

#include "auth/dvar_patches.h"
#include "client/online_frontend.h"
#include "network/backend/tournament_backend.h"
#include "client/frontend/native_commerce.h"
#include "client/lua/lua_injection.h"
#include "network/online_services.h"
#include "network/backend/native_matchmaking.h"
#include "network/social/social_runtime.h"
#include "network/commerce/store_backend.h"
#include "network/backend/frontend_backend.h"
#include "network/commerce/custom_mod_backend.h"
#include "network/backend/name_validation.h"
#include "network/native_network.h"
#include "network/playlists.h"
#include "auth/offline_auth.h"

#include "engine/game.h"

#include "common/console.h"

#include "engine/commands.h"
#include "engine/safemem.h"

#include <cmath>
#include <cstring>
#include <unordered_map>
#include <atomic>
#include <sstream>
namespace online_frontend
{
	namespace
	{
		uintptr_t g_base = 0;
		bool g_enabled = false;
		std::atomic<bool> g_local_play{ false };
		std::atomic<bool> g_unlockAll{ false }; // session setting survives frontend/gameplay VM reloads
		bool g_mapsCached = false;
		std::unordered_map<std::string, bool> g_installedMaps;
		using Callback = int (*)(void*);

		struct Registration
		{
			const char* name;
			Callback callback;
		};

		template <class T>
		T Function(uintptr_t rva)
		{
			return reinterpret_cast<T>(g_base + rva);
		}

		template <size_t N>
		bool Matches(uintptr_t rva, const uint8_t (&bytes)[N])
		{
			uint8_t actual[N]{};
			return safemem::RegionExecutable(reinterpret_cast<void*>(g_base + rva)) &&
				safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), actual, N) && !memcmp(actual, bytes, N);
		}

		int Boolean(void* state, bool value)
		{
			Function<void (*)(void*, int)>(game::kLuaPushBooleanRVA)(state, value);
			return 1;
		}

		bool Integer(void* state, int index, int& out)
		{
			if (!Function<int (*)(void*, int)>(0x20839e0)(state, index)) return false;
			const double number = Function<double (*)(void*, int)>(0x2084a10)(state, index);
			if (!std::isfinite(number) || number < 0 || number > 200 || number != std::floor(number)) return false;
			out = static_cast<int>(number);
			return true;
		}

		int Enabled(void* state)
		{
			return Boolean(state, g_enabled);
		}

		int WeaponPilots(void* state)
		{
			return Boolean(state, weapon_files::Enabled());
		}

		int WeaponPorted(void* state)
		{
			size_t length = 0;
			const char* ref = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 1, &length);
			return Boolean(state, ref && length && length < 64 && weapon_loadouts::Category(std::string_view(ref, length)));
		}

		int DefaultEscapeHUDTable(void* state)
		{
			return Boolean(state, night_loot::UseDefaultEscapeHUDTable());
		}

		int EscapeHUDArt(void* state)
		{
			return Boolean(state, night_loot::UseEscapeHUDArt());
		}

		int SkipIntros(void* state)
		{
			return Boolean(state, SkipIntroCinematics());
		}

		int NoclipState(void* state)
		{
			bool enabled = false;
			const bool available = noclip::LocalState(&enabled);
			Boolean(state, available);
			Boolean(state, enabled);
			return 2;
		}

		int SetNoclip(void* state)
		{
			int enabled = -1;
			return Boolean(state, Integer(state, 1, enabled) && enabled <= 1 && clientconsole::RequestNoclip(enabled == 1));
		}

		int UnlockAll(void* state)
		{
			return Boolean(
				state, frontend_backend::Required() ? local_challenges::UnlockEnabled() : g_unlockAll.load(std::memory_order_relaxed));
		}

		int SetUnlockAll(void* state)
		{
			int enabled = -1;
			return Boolean(state, Integer(state, 1, enabled) && enabled <= 1 && local_challenges::SetUnlock(enabled == 1));
		}

		int PlaylistReady(void* state)
		{
			return Boolean(state, playlists::Ready());
		}

		int PreparePlayerData(void* state)
		{
			if (!g_enabled || !offlineauth::InitializeProfile()) return Boolean(state, false);
			playlists::Pump();
			return Boolean(state, playlists::Ready() && local_stats::RemoteReady());
		}

		int prepare_local_player_data(void* state)
		{
			int controller = -1;
			// Local menus use the same restored active DDL bank as the online UI.
			// They need valid contexts, not a remote profile/playlist synchronization.
			return Boolean(state, Integer(state, 1, controller) && controller == 0 && offlineauth::InitializeProfile());
		}

		int local_play_state(void* state)
		{
			return Boolean(state, local_play());
		}

		int select_local_play(void* state)
		{
			int enabled = -1;
			if (!Integer(state, 1, enabled) || enabled > 1) return Boolean(state, false);
			set_local_play(enabled != 0);
			return Boolean(state, true);
		}

		int Searching(void* state)
		{
			return Boolean(state, online_services::IsSearching());
		}

		void PushString(void* state, const std::string& value)
		{
			Function<void (*)(void*, const char*)>(0x2084120)(state, value.c_str());
		}

		int ClientSettingsStatus(void* state)
		{
			const auto view = local_challenges::UnlockStatus();
			Boolean(state, view.ready);
			Boolean(state, view.pending);
			Boolean(state, view.enabled);
			PushString(state, view.status);
			PushString(state, std::to_string(view.revision));
			return 5;
		}

		int CamoStatus(void* state)
		{
			PushString(state, camo_changer::Status());
			PushString(state, camo_changer::Catalog());
			return 2;
		}

		int CamoCommand(void* state)
		{
			size_t n = 0, m = 0;
			const auto* action = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 1, &n);
			const auto* camo = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 2, &m);
			return Boolean(state,
				action && n <= 8 && m <= 64 &&
					camo_changer::Submit(std::string_view(action, n), camo ? std::string_view(camo, m) : std::string_view{}));
		}

		int ProfileStatus(void* state)
		{
			const auto view = local_stats::SyncStatus();
			Boolean(state, view.ready);
			Boolean(state, view.pending);
			Boolean(state, view.conflict);
			PushString(state, view.status);
			return 4;
		}

		int ResolveProfile(void* state)
		{
			int choice = -1;
			return Boolean(state, Integer(state, 1, choice) && choice <= 1 && local_stats::ResolveConflict(choice == 1));
		}

		int PreferencesStatus(void* state)
		{
			PushString(state, local_settings::Status());
			return 1;
		}

		int ResolvePreferences(void* state)
		{
			int choice = -1;
			return Boolean(state, Integer(state, 1, choice) && choice <= 1 && local_settings::Resolve(choice == 1));
		}

		int CustomizationNameValid(void* state)
		{
			size_t n = 0;
			const auto* text = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 1, &n);
			int kind = -1;
			constexpr size_t limits[] = { customization_names::LoadoutBytes, customization_names::ClanBytes,
				customization_names::ModBytes };
			return Boolean(
				state, text && Integer(state, 2, kind) && kind <= 2 && customization_names::Valid(std::string_view(text, n), limits[kind]));
		}

		int TrialTickets(void* state)
		{
			const auto view = local_challenges::CommerceStatus();
			Boolean(state, view.ready);
			Boolean(state, view.pending);
			PushString(state, std::to_string(local_challenges::TokenQuantity(xp_tokens::TrialTicket)));
			return 3;
		}

		int GrantTrialTickets(void* state)
		{
			return Boolean(state, local_challenges::Purchase("supply", "trialtickets5", 0));
		}

		int Tournament(void* state)
		{
			size_t n = 0;
			const auto* op = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 1, &n);
			if (!op || !n || n > 16) return 0;
			const std::string action(op, n);
			if (action == "join") return Boolean(state, tournament_backend::Join());
			if (action == "playlist")
			{
				for (int category = 0; category < 26; ++category)
					for (int child = 0; child < 20; ++child)
						if (playlists::ResolveCategoryPlaylist(category, child) == tournament120::Playlist)
						{
							PushString(state, std::to_string(category));
							PushString(state, std::to_string(child));
							return 2;
						}
				return 0;
			}
			size_t m = 0;
			const auto* arg = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 2, &m);
			if (!arg || !m || m > 128) return 0;
			if (action == "command") return Boolean(state, tournament_backend::Command(std::string(arg, m)));
			if (action == "get")
			{
				PushString(state, tournament_backend::Field(std::string(arg, m)));
				return 1;
			}
			return 0;
		}

		int AfterAction(void* state)
		{
			size_t a = 0, b = 0;
			const auto read = Function<const char* (*)(void*, int, size_t*)>(0x2084950);
			const auto* op = read(state, 1, &a);
			if (!op || !a || a > 16) return 0;
			const auto* arg = read(state, 2, &b);
			if (b > 64) return 0;
			const auto values = local_challenges::AfterAction(std::string(op, a), arg ? std::string(arg, b) : std::string{});
			for (const auto& value : values)
				PushString(state, value);
			return int(values.size());
		}

		int CustomModsStatus(void* state)
		{
			const auto view = custom_mod_backend::Status();
			Boolean(state, view.ready);
			Boolean(state, view.pending);
			PushString(state, std::to_string(view.revision));
			PushString(state, view.status);
			return 4;
		}

		int ValidateModName(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0) return Boolean(state, false);
			size_t length = 0;
			const char* text = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 2, &length);
			return Boolean(state, g_enabled && text && length <= 48 && name_validation::Submit(std::string(text, length)));
		}

		int ModNameStatus(void* state)
		{
			const auto view = name_validation::Status();
			Boolean(state, view.pending);
			Boolean(state, view.complete);
			Boolean(state, view.allowed);
			PushString(state, view.reason);
			return 4;
		}

		int CustomModField(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0) return 0;
			size_t length = 0;
			const char* path = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 2, &length);
			if (!path || length > 200) return 0;
			PushString(state, custom_mod_backend::Field(std::string(path, length)));
			return 1;
		}

		int CustomModChange(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0) return Boolean(state, false);
			size_t length = 0;
			const char* bytes = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 2, &length);
			return Boolean(state, g_enabled && bytes && length <= 8192 && custom_mod_backend::Change(std::string_view(bytes, length)));
		}

		int Progression(void* state)
		{
			int controller = 0, season = 0;
			if (!Integer(state, 1, controller) || controller != 0 || !Integer(state, 2, season) || season > 6) return 0;
			const auto view = local_progression::Get(season);
			if (!view.ready) return 0;
			PushString(state, std::to_string(view.xp));
			PushString(state, std::to_string(view.rank));
			PushString(state, std::to_string(view.seasonXP));
			PushString(state, std::to_string(view.seasonRank));
			return 4;
		}

		int OwnsItem(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0 || !Function<int (*)(void*, int)>(0x20839e0)(state, 2))
				return Boolean(state, false);
			const double id = Function<double (*)(void*, int)>(0x2084a10)(state, 2);
			return Boolean(
				state, std::isfinite(id) && id > 0 && id <= 100000000 && id == std::floor(id) && local_progression::OwnsItem(int(id)));
		}

		int WeaponProgression(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0) return 0;
			const auto* weapon = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 2, nullptr);
			const auto view = local_progression::GetWeapon(weapon);
			if (!view.ready) return 0;
			PushString(state, std::to_string(view.xp));
			PushString(state, std::to_string(view.rank));
			return 2;
		}

		int ChallengeProgress(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0 || !Function<int (*)(void*, int)>(0x20839e0)(state, 2)) return 0;
			const double id = Function<double (*)(void*, int)>(0x2084a10)(state, 2);
			if (!std::isfinite(id) || id < 1 || id > 10000000 || id != std::floor(id)) return 0;
			const int progress = local_challenges::Progress(int(id));
			if (progress < 0) return 0;
			PushString(state, std::to_string(progress));
			return 1;
		}

		int ChallengeAssignments(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0) return 0;
			const auto* kind = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 2, nullptr);
			PushString(state, local_challenges::Assignments(kind));
			return 1;
		}

		int ChallengeUTC(void* state)
		{
			PushString(state, std::to_string(local_challenges::UTC()));
			return 1;
		}

		int BattlePassProgression(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0 || local_challenges::XP() < 0) return 0;
			PushString(state, std::to_string(local_challenges::BattleXP()));
			Boolean(state, local_challenges::Premium());
			PushString(state, std::to_string(local_challenges::BattleCP()));
			return 3;
		}

		int StoreStatus(void* state)
		{
			const auto view = store_backend::Get();
			Boolean(state, view.ready);
			PushString(state, std::to_string(view.revision));
			PushString(state, std::to_string(view.rows.size()));
			return 3;
		}

		int CommerceStatus(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0) return 0;
			const auto v = local_challenges::CommerceStatus();
			Boolean(state, v.ready);
			Boolean(state, v.pending);
			PushString(state, std::to_string(v.balance));
			PushString(state, v.status);
			PushString(state, v.product);
			return 5;
		}

		int CurrencyPack(void* state)
		{
			int index = -1;
			if (!Integer(state, 1, index)) return 0;
			const auto [id, amount] = local_challenges::CurrencyPack(index);
			if (id.empty()) return 0;
			PushString(state, id);
			PushString(state, std::to_string(amount));
			return 2;
		}

		int Purchase(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0) return Boolean(state, false);
			const auto* kind = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 2, nullptr);
			const auto* product = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 3, nullptr);
			const double price = Function<double (*)(void*, int)>(0x2084a10)(state, 4);
			return Boolean(state,
				kind && product && std::isfinite(price) && price >= 0 && price <= 1000000 && price == std::floor(price) &&
					local_challenges::Purchase(kind, product, int(price)));
		}

		int StoreCategory(void* state)
		{
			int index = -1;
			if (!Integer(state, 1, index)) return 0;
			const auto view = store_backend::Get();
			if (!view.ready || size_t(index) >= view.rows.size()) return 0;
			const auto& row = view.rows[index];
			PushString(state, row.id);
			PushString(state, row.name);
			PushString(state, row.bundles);
			return 3;
		}

		int NextProgressionNotice(void* state)
		{
			int controller = -1, kind = -1;
			if (!Integer(state, 1, controller) || controller != 0 || !Integer(state, 2, kind) || kind < 0 || kind > 1) return 0;
			const auto notice = local_challenges::NextNotice(kind == 1);
			if (!notice.sequence) return 0;
			PushString(state, std::to_string(notice.sequence));
			PushString(state, notice.event);
			PushString(state, std::to_string(notice.value));
			PushString(state, notice.weapon);
			std::string items;
			for (int id : notice.items)
				items += std::to_string(id) + ",";
			PushString(state, items);
			return 5;
		}

		int AckProgressionNotice(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0) return Boolean(state, false);
			size_t length = 0;
			const auto* text = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 2, &length);
			uint64_t sequence = 0;
			if (!text || !length || length > 20) return Boolean(state, false);
			const auto parsed = std::from_chars(text, text + length, sequence);
			return Boolean(
				state, parsed.ec == std::errc{} && parsed.ptr == text + length && sequence && local_challenges::AckNotice(sequence));
		}

		int ActiveMission(void* state)
		{
			int controller = -1, category = 0;
			if (!Integer(state, 1, controller) || controller != 0 || !Integer(state, 2, category)) return 0;
			PushString(state, std::to_string(local_challenges::Mission(category)));
			return 1;
		}

		int ActivateMission(void* state)
		{
			int controller = -1;
			if (!Integer(state, 1, controller) || controller != 0 || !Function<int (*)(void*, int)>(0x20839e0)(state, 2))
				return Boolean(state, false);
			const double id = Function<double (*)(void*, int)>(0x2084a10)(state, 2);
			return Boolean(
				state, std::isfinite(id) && id >= 1 && id <= 10000000 && id == std::floor(id) && local_challenges::Activate(int(id)));
		}

		int QueueState(void* state)
		{
			const auto view = online_services::GetQueueView();
			Boolean(state, view.valid);
			PushString(state, std::to_string(unsigned(view.phase)));
			PushString(state, std::to_string(view.seconds));
			PushString(state, std::to_string(view.players.size()));
			return 4;
		}

		int MatchmakingPing(void* state)
		{
			if (!native_matchmaking::Enabled() || !native_matchmaking::Searching()) return 0;
			const int ping = social_runtime::MatchmakingPing();
			if (ping < 0) return 0;
			PushString(state, std::to_string(ping));
			return 1;
		}

		int MatchStartCountdown(void* state)
		{
			int remaining = 0;
			if (!native_matchmaking::StartCountdown(remaining)) return 0;
			PushString(state, std::to_string(remaining));
			return 1;
		}

		int QueuePlayer(void* state)
		{
			int index = -1;
			const auto view = online_services::GetQueueView();
			if (!Integer(state, 1, index) || !view.valid || size_t(index) >= view.players.size()) return 0;
			const auto& player = view.players[index];
			PushString(state, player.name);
			PushString(state, player.clan);
			PushString(state, std::to_string(player.xuid));
			PushString(state, std::to_string(player.rank));
			PushString(state, std::to_string(player.prestige));
			PushString(state, std::to_string(player.emblem));
			PushString(state, std::to_string(player.background));
			Boolean(state, player.xuid == offlineauth::GetIdentity().xuid);
			return 8;
		}

		int QueueCard(void* state)
		{
			const auto* clan = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 1, nullptr);
			int values[4]{};
			for (int i = 0; i < 4; ++i)
				if (Function<int (*)(void*, int)>(0x20839e0)(state, i + 2))
				{
					const double n = Function<double (*)(void*, int)>(0x2084a10)(state, i + 2);
					if (std::isfinite(n) && n >= 0 && n <= 1000000) values[i] = int(n);
				}
			online_services::SetQueueCard(clan ? clan : "", values[0], values[1], values[2], values[3]);
			return 0;
		}

		int SilentJoin(void* state)
		{
			return Boolean(state, online_services::SilentMatchJoin());
		}

		int RestoringParty(void* state)
		{
			return Boolean(state, online_services::RestoringFrontendParty());
		}

		int InDedicatedMatch(void* state)
		{
			return Boolean(state, online_services::InDedicatedMatch());
		}

		void ScanMaps()
		{
			if (g_mapsCached || !g_base) return;
			uintptr_t filesystem = 0;
			int count = 0;
			// Lobby.DEFCCIGHCF -> 1BA10D0 -> 10C7700 reads this arena count.
			// The arena table is populated AFTER the first luistartup. Wait for it;
			// do not treat an empty startup enumeration as a completed inventory.
			if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0xb8f6cb8), &filesystem, sizeof(filesystem)) || !filesystem ||
				!safemem::ReadBytes(reinterpret_cast<void*>(g_base + 0xc4fd8b4), &count, sizeof(count)) || count < 1 || count > 512)
				return;
			std::vector<std::string> names;
			for (int i = 0; i < count; ++i)
			{
				char name[64]{};
				// CJEHDHDGBH -> 1BA10F0 -> 10C77D0: arena stride1240, map name+32.
				if (!safemem::ReadString(reinterpret_cast<const char*>(g_base + 0xc4fd8d8 + i * 1240), name, sizeof(name))) return;
				const std::string value(name);
				if (!value.starts_with("mp_") || value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
					continue;
				names.push_back(value);
			}
			if (names.empty()) return;
			unsigned present = 0;
			for (const auto& name : names)
			{
				if (g_installedMaps.contains(name)) continue;
				bool available = true;
				// TACT archives can list maps whose files were not downloaded. Query
				// the mounted filesystem exactly once, never while building a menu.
				for (const auto* prefix : { "", "ww_", "techsets_" })
				{
					const auto zone = std::string(prefix) + name;
					available &= Function<bool (*)(const char*)>(0x11a7940)(zone.c_str());
				}
				g_installedMaps.emplace(name, available);
				if (available) ++present;
				LOG_INFO("Maps", "startup inventory: %s %s", name.c_str(), available ? "present" : "missing fastfiles");
			}
			g_mapsCached = true;
			LOG_INFO("Maps", "one-time local selector inventory complete: %u/%zu maps", present, g_installedMaps.size());
		}

		int MapInstalled(void* state)
		{
			const char* name = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 1, nullptr);
			if (!g_mapsCached) return Boolean(state, true);
			const auto found = name ? g_installedMaps.find(name) : g_installedMaps.end();
			return Boolean(state, found != g_installedMaps.end() && found->second);
		}

		int BackendReady(void* state)
		{
			const auto status = online_services::GetStatus();
			return Boolean(state, status.running && status.accountStatus == 1 && !status.updateRequired);
		}

		int Start(void* state)
		{
			int category = -1;
			const int top = Function<int (*)(void*)>(game::kLuaGetTopRVA)(state);
			if (!g_enabled || local_play() || top < 2 || top > 21 || !Integer(state, 1, category)) return Boolean(state, false);
			std::vector<int> ids;
			for (int arg = 2; arg <= top; ++arg)
			{
				int child = -1;
				if (!Integer(state, arg, child) || child == 200) return Boolean(state, false);
				const int id = playlists::ResolveCategoryPlaylist(category, child);
				if (id < 0) return Boolean(state, false);
				ids.push_back(id);
			}
			const bool accepted = online_services::StartMatchmaking(ids);
			LOG_INFO(
				"OnlineUI", "playlist action category=%d choices=%zu first_id=%d accepted=%d", category, ids.size(), ids.front(), accepted);
			return Boolean(state, accepted);
		}

		int HasCategoryChild(void* state)
		{
			int category = -1, child = -1;
			return Boolean(
				state, Integer(state, 1, category) && Integer(state, 2, child) && playlists::ResolveCategoryPlaylist(category, child) >= 0);
		}

		int Cancel(void*)
		{
			if (g_enabled) online_services::CancelMatchmaking();
			return 0;
		}

		int Disconnect(void* state)
		{
			// Keep the pause action on the game's own disconnect path so party and
			// connection teardown follow the same route as the console command.
			return Boolean(state, g_enabled && commands::Queue(g_base, "disconnect\n"));
		}

		int Log(void* state)
		{
			char text[768]{};
			const auto* value = Function<const char* (*)(void*, int, size_t*)>(0x2084950)(state, 1, nullptr);
			if (safemem::ReadString(value, text, sizeof(text))) LOG_INFO("OnlineUI", "%s", text);
			return 0;
		}

		void Register(void* state)
		{
			online_services::OnLuaStartup();
			if (g_enabled) native_commerce::Register(state);
			static const Registration entries[] = { { "Enabled", Enabled }, { "PlaylistReady", PlaylistReady },
				{ "BackendReady", BackendReady }, { "UseDefaultEscapeHUDTable", DefaultEscapeHUDTable },
				{ "UseEscapeHUDArt", EscapeHUDArt }, { "SkipIntroCinematics", SkipIntros },
				{ "ClientSettingsStatus", ClientSettingsStatus }, { "NoclipState", NoclipState }, { "SetNoclip", SetNoclip },
				{ "CamoStatus", CamoStatus }, { "CamoCommand", CamoCommand }, { "AfterAction", AfterAction }, { "Tournament", Tournament },
				{ "ProfileStatus", ProfileStatus }, { "ResolveProfile", ResolveProfile }, { "PreferencesStatus", PreferencesStatus },
				{ "ResolvePreferences", ResolvePreferences }, { "CustomizationNameValid", CustomizationNameValid },
				{ "TrialTickets", TrialTickets }, { "GrantTrialTickets", GrantTrialTickets }, { "CustomModsStatus", CustomModsStatus },
				{ "CustomModField", CustomModField }, { "CustomModChange", CustomModChange }, { "ValidateModName", ValidateModName },
				{ "ModNameStatus", ModNameStatus }, { "PreparePlayerData", PreparePlayerData },
				{ "PrepareLocalPlayerData", prepare_local_player_data }, { "LocalPlay", local_play_state }, { "SetLocalPlay", select_local_play },
				{ "WeaponPilots", WeaponPilots },
				{ "WeaponPorted", WeaponPorted }, { "UnlockAll", UnlockAll }, { "SetUnlockAll", SetUnlockAll },
				{ "Progression", Progression }, { "OwnsItem", OwnsItem }, { "WeaponProgression", WeaponProgression },
				{ "ChallengeProgress", ChallengeProgress }, { "ChallengeAssignments", ChallengeAssignments },
				{ "ChallengeUTC", ChallengeUTC }, { "ActiveMission", ActiveMission }, { "ActivateMission", ActivateMission },
				{ "BattlePassProgression", BattlePassProgression }, { "CommerceStatus", CommerceStatus }, { "CurrencyPack", CurrencyPack },
				{ "Purchase", Purchase }, { "StoreStatus", StoreStatus }, { "StoreCategory", StoreCategory },
				{ "NextProgressionNotice", NextProgressionNotice }, { "AckProgressionNotice", AckProgressionNotice },
				{ "MapInstalled", MapInstalled }, { "QueueState", QueueState }, { "QueuePlayer", QueuePlayer }, { "QueueCard", QueueCard },
				{ "SilentJoin", SilentJoin }, { "MatchmakingPing", MatchmakingPing }, { "MatchStartCountdown", MatchStartCountdown },
				{ "RestoringParty", RestoringParty }, { "InDedicatedMatch", InDedicatedMatch }, { "Start", Start },
				{ "HasCategoryChild", HasCategoryChild }, { "Cancel", Cancel }, { "Disconnect", Disconnect }, { "IsSearching", Searching },
				{ "Log", Log }, { nullptr, nullptr } };
			const int top = Function<int (*)(void*)>(game::kLuaGetTopRVA)(state);
			Function<void (*)(void*, const char*, const Registration*)>(0x20885c0)(state, "ReplayOnline", entries);
			Function<void (*)(void*, int)>(game::kLuaSetTopRVA)(state, top);
			lua_injection::RegisterMenu1645(state);
			LOG_INFO("OnlineUI", "ReplayOnline registered on startup VM; configured=%d", g_enabled);
		}
	}

	bool local_play()
	{
		// Online dedicated admission temporarily sets systemlink too. Its party
		// and presentation adapters must keep the service route until it ends.
		return g_local_play.load(std::memory_order_acquire) && !online_services::SilentMatchJoin() &&
			!online_services::InDedicatedMatch() && !native_matchmaking::Joining();
	}

	void set_local_play(bool enabled)
	{
		if (g_local_play.load(std::memory_order_acquire) == enabled) return;
		if (enabled) online_services::CancelMatchmaking();
		if (g_local_play.exchange(enabled, std::memory_order_acq_rel) == enabled) return;
		if (!enabled && g_base)
		{
			for (const auto rva : { game::kSystemLinkDvarRVA, game::kSystemLinkHostDvarRVA })
			{
				uintptr_t dvar = 0;
				uint8_t type = 255;
				if (safemem::ReadBytes(reinterpret_cast<void*>(g_base + rva), &dvar, sizeof(dvar)) && dvar &&
					safemem::ReadBytes(reinterpret_cast<void*>(dvar + game::kDvarTypeOffset), &type, sizeof(type)) && type == 0)
					Function<game::Dvar_SetBool_Internal_t>(game::kDvarSetBoolInternalRVA)(reinterpret_cast<void*>(dvar), false);
			}
		}
		LOG_INFO("OnlineUI", "selected %s gameplay route; online services retained", enabled ? "local/system-link" : "online");
	}

	void Pump()
	{
		ScanMaps();
		native_commerce::Pump();
		// Execute once on the main-thread Cbuf after startup registration. A later
		// manual console change remains respected; VM/menu reloads do not spam it.
		static bool overlayQueued = false;
		if (!overlayQueued && commands::Queue(g_base, "set lui_showBuildAndServerText 1\n"))
		{
			overlayQueued = true;
			LOG_INFO("OnlineUI", "startup command: lui_showBuildAndServerText 1");
		}
	}

	bool SkipIntroCinematics()
	{
		static const bool enabled = []
		{
			wchar_t path[MAX_PATH]{};
			return !native_network::ConfigurationPath(path, MAX_PATH) ||
				GetPrivateProfileIntW(L"ui", L"skip_intro_cinematics", 1, path) != 0;
		}();
		return enabled;
	}

	bool HandleCommand(std::string_view command)
	{
		std::istringstream args{ std::string(command) };
		std::string name, target, value, extra;
		args >> name;
		if (name != "unlock") return false;
		if (!(args >> target >> value) || target != "all" || (value != "0" && value != "1") || (args >> extra))
		{
			OP_INFO("Usage: unlock all <0|1> | current=%d",
				int(frontend_backend::Required() ? local_challenges::UnlockEnabled() : g_unlockAll.load()));
			return true;
		}
		if (frontend_backend::Required())
		{
			OP_INFO("Unlock policy request %s; access changes after the server saves and acknowledges it",
				local_challenges::SetUnlock(value == "1") ? "queued" : "not ready");
			return true;
		}
		g_unlockAll.store(value == "1", std::memory_order_relaxed);
		dvars::SetUnlockAll(value == "1");
		OP_INFO("Unlock all %s | reopen the loadout/operator menu to refresh", value == "1" ? "ON" : "OFF");
		return true;
	}

	hook::Status Install(uintptr_t base)
	{
		g_base = base;
		// Replay LuaJIT ABI verified through native registration/playlist wrappers;
		// evidence in ida-reports/replay_online_bridge_abi.json.
		const uint8_t registration[] = { 0x45, 0x33, 0xc9, 0xe9, 0xe8, 0xfb, 0xff, 0xff };
		const uint8_t number[] = { 0x48, 0x83, 0xec, 0x28, 0xe8, 0x47, 0xde, 0xff, 0xff };
		const uint8_t isNumber[] = { 0x48, 0x83, 0xec, 0x28, 0xe8, 0x77, 0xee, 0xff, 0xff };
		const uint8_t string[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x49 };
		const uint8_t exists[] = { 0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x81, 0xec, 0x40, 0x01, 0, 0, 0x48, 0x8b, 0x05 };
		const uint8_t pushString[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xd9 };
		if (!Matches(0x20885c0, registration) || !Matches(0x2084a10, number) || !Matches(0x20839e0, isNumber) ||
			!Matches(0x2084950, string) || !Matches(0x11a7940, exists) || !Matches(0x2084120, pushString) ||
			!Matches(game::kLuaGetTopRVA, game::kLuaGetTopPrologue) || !Matches(game::kLuaSetTopRVA, game::kLuaSetTopPrologue) ||
			!Matches(game::kLuaPushBooleanRVA, game::kLuaPushBooleanPrologue))
			return hook::Status::NotReady;
		g_enabled = online_services::Configured();
		if (g_enabled)
		{
			const auto commerce = native_commerce::Install(base);
			if (commerce != hook::Status::Installed) return commerce;
			const auto status = clan_tags::Install(base);
			if (status != hook::Status::Installed) return status;
		}
		lua_injection::SetStartupCallback(Register);
		return hook::Status::Installed;
	}
}
