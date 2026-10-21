#include "auth/profile/local_progression.h"

#include "engine/addresses.hpp"

#include "auth/profile/local_stats.h"
#include "auth/offline_auth.h"

#include "engine/game.h"
#include "engine/safemem.h"

#include "common/console.h"

#include "auth/profile/progression_catalog.h"
#include "auth/profile/challenge_transport.h"
#include "auth/profile/local_challenges.h"
#include "network/profile/progression_backend.h"
#include "auth/profile/weapon_reference.h"

#include "engine/resident_string_tables.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
namespace local_progression
{
	namespace
	{
		struct Context
		{
			uint8_t* buffer;
			int size;
			int pad;
			const uint8_t* definition;
			void* callback;
			void* userdata;
			bool obfuscated;
			uint8_t padding[3];
			uint32_t seed;
		};

		struct State
		{
			bool valid;
			uint8_t pad[3];
			int offset;
			int arrayIndex;
			int pad2;
			const uint8_t* member;
			const uint8_t* definition;
		};

		static_assert(sizeof(Context) == 48 && offsetof(Context, seed) == 44);
		static_assert(sizeof(State) == 32 && offsetof(State, member) == 16);
		uintptr_t g_base = 0;
		bool g_dedicated = false, g_installed = false;
		ULONGLONG g_nextPoll = 0;
		View g_view;
		int g_totalXP = -1, g_maxXP = 0, g_season = -1;
		std::vector<int> g_militaryThresholds, g_seasonThresholds;
		progression::Catalog g_catalog;
		std::atomic<bool> g_catalogReady{ false };
		std::map<int, std::vector<int>> g_rankGrants;
		std::map<std::string, std::map<int, std::vector<int>>> g_weaponGrants;
		std::map<std::string, int> g_weaponXP;
		ULONGLONG g_nextWeaponPoll = 0;
		using ReportFn = int64_t (*)(void*, uint64_t);
		std::atomic<ReportFn> g_report{ nullptr };

		template <class F>
		F Native(uintptr_t rva)
		{
			return reinterpret_cast<F>(g_base + rva);
		}

		template <class T>
		T Read(uintptr_t rva)
		{
			return *reinterpret_cast<const T*>(g_base + rva);
		}

		bool CatalogReady()
		{
			if (g_catalogReady.load(std::memory_order_acquire)) return true;
			// One resident index for the entire catalog, outside gameplay callbacks.
			// Missing DLC tables never invoke the engine's fatal/default-asset path.
			const auto started = GetTickCount64();
			if (Read<int>(0x2458160 + 54 * 4) != sizeof(resident_tables::Asset)) return false;
			resident_tables::Reader tables(Read<const resident_tables::Asset*>(0xb8f5a80 + 54 * 8), Read<int>(0x23fbba0 + 54 * 4));
			if (g_catalog.Load(
					[&](const std::string& name)
					{
						return tables.Read(name);
					}))
			{
				for (const auto& [id, rank] : g_catalog.military)
					if (!g_catalog.initial.contains(id)) g_rankGrants[rank].push_back(id);
				for (const auto& [id, unlock] : g_catalog.weaponUnlocks)
					if (!g_catalog.initial.contains(id)) g_weaponGrants[unlock.weapon][unlock.rank].push_back(id);
				g_catalogReady.store(true, std::memory_order_release);
				LOG_INFO("Progression",
					"native earned catalog ready | defaults=%zu rankItems=%zu weapons=%zu weaponItems=%zu prepareMs=%llu indexed=%zu memoryQueries=%zu",
					g_catalog.initial.size(), g_catalog.military.size(), g_catalog.weapons.size(), g_catalog.weaponUnlocks.size(),
					GetTickCount64() - started, tables.Indexed(), tables.Queries());
			}
			return g_catalogReady;
		}

		bool BindingsMatch()
		{
			struct Binding
			{
				uintptr_t rva;
				std::array<uint8_t, 16> bytes;
			};

			const Binding bindings[] = {
				{ 0x2051c70, { 0x33, 0xc0, 0xc7, 0x41, 0x08, 0xff, 0xff, 0xff, 0xff, 0x48, 0x89, 0x41, 0x10, 0x89, 0x41, 0x04 } },
				{ 0x2052430, { 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7c, 0x24, 0x18, 0x41, 0x56, 0x48, 0x83, 0xec, 0x20 } },
				{ 0x2051bf0, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xfa, 0x48, 0x8b, 0xd9 } },
				{ 0x2052820, { 0x48, 0x8b, 0x41, 0x10, 0x4c, 0x8b, 0xc9, 0x83, 0x78, 0x24, 0x03, 0x75, 0x2c, 0x8b, 0x48, 0x1c } },
				{ 0x13e2b00, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x41, 0xb8, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xda, 0x48 } },
				{ 0x13e2ba0, { 0x40, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x48, 0x83, 0xec, 0x28, 0x45, 0x8b, 0xe9, 0x4d, 0x8b } },
				{ 0x2051750, { 0xe9, 0x5b, 0x26, 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
				{ 0x15c7940, { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57 } },
				{ 0x1370c60, { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x8b, 0xc1, 0x48, 0x8d, 0x1d, 0x21, 0xea, 0xe0, 0x0c, 0x8b } },
				{ 0x1370c20, { 0x8b, 0xc1, 0x48, 0x8d, 0x0d, 0x67, 0xea, 0xe0, 0x0c, 0x48, 0x8b, 0x04, 0xc1, 0x48, 0x05, 0xcc } },
				{ 0x10ca2c0, { 0x48, 0x63, 0xc1, 0x48, 0x8d, 0x0d, 0x6e, 0xaa, 0x45, 0x0b, 0x48, 0x8d, 0x04, 0x80, 0x48, 0x8b } },
				{ 0x10caab0, { 0x48, 0x8d, 0x15, 0x49, 0x55, 0xf3, 0xfe, 0x83, 0xf9, 0x05, 0x77, 0x5a, 0x48, 0x63, 0xc1, 0x8b } },
				{ 0x12a2110, { 0x48, 0x83, 0xec, 0x28, 0xe8, 0x47, 0x8a, 0xe2, 0xff, 0x83, 0xe8, 0x04, 0x48, 0x83, 0xc4, 0x28 } },
				{ 0x126eaa0, { 0x45, 0x8b, 0xd8, 0x44, 0x8b, 0xd2, 0x41, 0x83, 0xf9, 0x01, 0x75, 0x23, 0x48, 0x8b, 0x41, 0x20 } },
				{ 0x1373430, { 0x8b, 0xc1, 0x48, 0x8d, 0x0d, 0x57, 0xc2, 0xe0, 0x0c, 0x48, 0x8b, 0x04, 0xc1, 0xc6, 0x80, 0x28 } },
				{ 0x13761e0, { 0x48, 0x8b, 0x05, 0x19, 0x9c, 0xe0, 0x0c, 0x48, 0x8b, 0x91, 0x30, 0x61, 0x04, 0x00, 0x48, 0xf7 } },
			};
			for (const auto& b : bindings)
			{
				std::array<uint8_t, 16> actual{};
				if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + b.rva), actual.data(), actual.size()) || actual != b.bytes)
				{
					LOG_ERR("Progression", "binding mismatch at RVA %llX", static_cast<unsigned long long>(b.rva));
					return false;
				}
			}
			return true;
		}

		bool Field(Context& context, std::initializer_list<const char*> path, State& state)
		{
			State root{};
			Native<State* (*)(State*, const void*)>(0x2051c70)(&root, context.definition);
			if (!Native<bool (*)(const State*, State*, int, const char* const*)>(0x2052430)(
					&root, &state, static_cast<int>(path.size()), path.begin()) ||
				!state.valid || !state.member)
				return false;
			// Only integer fields are accepted; a schema mismatch must never write a struct.
			const int type = *reinterpret_cast<const int*>(state.member + 36);
			return type >= 0 && type <= 3;
		}

		int Value(Context& context, const State& state)
		{
			return Native<int (*)(const State*, Context*)>(0x2051bf0)(&state, &context);
		}

		bool Set(Context& context, const State& state, int value)
		{
			return Value(context, state) == value || Native<bool (*)(const State*, Context*, int)>(0x2052820)(&state, &context, value);
		}

		bool ClientContext(int group, Context& context)
		{
			return Native<bool (*)(Context*, int, int, int)>(0x15c7940)(&context, 0, 0, group);
		}

		struct DirtyData
		{
			void* bits;
			int offset;
			int pad = 0;
		};

		bool ServerContext(int slot, int group, Context& context, DirtyData& dirty)
		{
			const auto* definition = Native<const uint8_t* (*)(int)>(0x10ca2c0)(group);
			void* buffer = Native<void* (*)(int, int)>(0x1370c60)(slot, group);
			const int size = Native<int (*)(int)>(0x12a2110)(group);
			if (!definition || !buffer || size <= 0 || size > 65536) return false;
			dirty.bits = Native<void* (*)(int)>(0x1370c20)(slot);
			dirty.offset = Native<int (*)(int)>(0x10caab0)(group) + 4;
			// Same synchronous dirty-bit callback as Scr_SetPlayerData. Do not use the
			// game's error/reset wrapper: a missing/invalid stats packet must be preserved.
			return Native<bool (*)(void*, int, const void*, Context*, void*, void*)>(0x2051750)(
				buffer, size, definition, &context, reinterpret_cast<void*>(g_base + 0x126eaa0), &dirty);
		}

		int Number(const void* table, const char* key, int column)
		{
			const char* text = Native<const char* (*)(const void*, int, const char*, int)>(0x13e2ba0)(table, 0, key, column);
			if (!text || !*text) return -1;
			int value = -1;
			const auto result = std::from_chars(text, text + strlen(text), value);
			return result.ec == std::errc{} && !*result.ptr && value >= 0 ? value : -1;
		}

		bool MilitaryTable()
		{
			if (!g_militaryThresholds.empty()) return true;
			const void* table = nullptr;
			Native<void (*)(const char*, const void**)>(0x13e2b00)("mp/ranktable.csv", &table);
			if (!table) return false;
			const int maximum = Number(table, "maxrank", 1);
			if (maximum < 1 || maximum > 200) return false;
			std::vector<int> thresholds;
			for (int i = 0; i <= maximum; ++i)
			{
				const int xp = Number(table, std::to_string(i).c_str(), 2);
				if (xp < 0 || (!thresholds.empty() && xp <= thresholds.back())) return false;
				thresholds.push_back(xp);
			}
			const int cap = Number(table, std::to_string(maximum).c_str(), 7);
			if (cap <= thresholds.back()) return false;
			g_maxXP = cap;
			g_militaryThresholds = std::move(thresholds);
			LOG_INFO("Progression", "native military rank table ready | ranks=%zu capXP=%d", g_militaryThresholds.size(), cap);
			return true;
		}

		int Rank(const std::vector<int>& thresholds, int xp)
		{
			return std::max(0, static_cast<int>(std::upper_bound(thresholds.begin(), thresholds.end(), xp) - thresholds.begin()) - 1);
		}

		void RefreshView()
		{
			g_view.ready = true;
			g_view.xp = std::min(g_totalXP, g_maxXP);
			g_view.rank = Rank(g_militaryThresholds, g_view.xp);
			g_view.seasonXP = std::max(0, g_totalXP - g_maxXP);
			g_view.seasonRank = g_view.seasonXP > 0 && !g_seasonThresholds.empty() ? Rank(g_seasonThresholds, g_view.seasonXP) + 1 : 0;
			*reinterpret_cast<uint64_t*>(g_base + 0x4483d20) = g_view.xp;
			*reinterpret_cast<uint64_t*>(g_base + 0x4483d28) = g_view.rank;
			if (g_season >= 0 && g_season <= 6)
			{
				*reinterpret_cast<uint64_t*>(g_base + 0x4483d30 + 16 * g_season) = g_view.seasonXP;
				*reinterpret_cast<uint64_t*>(g_base + 0x4483d38 + 16 * g_season) = std::max(0, g_view.seasonRank - 1);
			}
		}

		int64_t Report(void* context, uint64_t entityRef)
		{
			// Capture only trusted native dedicated reports. The backend commits all
			// awards; client DDL is a presentation cache, never an XP authority.
			challenge_transport::BeginReport(entityRef);
			const auto result = g_report.load(std::memory_order_acquire)(context, entityRef);
			challenge_transport::EndReport();
			return result;
		}
	}

	hook::Status Install(uintptr_t base, bool dedicated)
	{
		if (g_installed) return hook::Status::Installed;
		g_base = base;
		g_dedicated = dedicated;
		if (!BindingsMatch()) return hook::Status::Failed;
#ifdef IW8X_CLIENT_ONLY
		if (challenge_transport::Install(base, false, &local_challenges::Receive) !=
#else
		if (challenge_transport::Install(base, dedicated, dedicated ? &progression_backend::Receive : &local_challenges::Receive) !=
#endif
			hook::Status::Installed)
			return hook::Status::Failed;
		if (dedicated)
		{
			constexpr uint8_t bytes[] = { 0x40, 0x53, 0x55, 0x57, 0x41, 0x55, 0x48, 0x81, 0xec, 0x48, 0x04, 0x00, 0x00, 0x48, 0x8b, 0x05 };
			const auto status = hook::Install(reinterpret_cast<void*>(base + 0x126da60), &Report, bytes, sizeof(bytes), g_report);
			if (status != hook::Status::Installed) return status;
		}
		g_installed = true;
		LOG_INFO("Progression", "earned-XP %s installed", dedicated ? "dedicated account awards" : "server snapshot display");
		return hook::Status::Installed;
	}

	bool PrepareServer()
	{
		if (!g_installed || !g_dedicated) return false;
		if (g_catalogReady.load(std::memory_order_acquire)) return true;
		static ULONGLONG nextAttempt = 0;
		const auto now = GetTickCount64();
		if (now < nextAttempt) return false;
		nextAttempt = now + 1000;
		return CatalogReady();
	}

	void Pump()
	{
		if (g_installed && !g_dedicated) local_challenges::ProjectUnlock();
		if (!local_stats::RemoteReady()) return;
		if (!g_installed || g_dedicated || !offlineauth::IsReady()) return;
		const auto now = GetTickCount64();
		if (now < g_nextPoll) return;
		g_nextPoll = now + 250;
		Context common{}, ranked{};
		State commonXP{}, rankedXP{};
		if (!ClientContext(2, common) || !ClientContext(3, ranked) ||
			!Field(common, { "commonData", "mpProgression", "playerLevel", "xp" }, commonXP) ||
			!Field(ranked, { "squadMembers", "player_xp" }, rankedXP) || !MilitaryTable())
			return;
		const int legacyTotal = std::max({ 0, g_totalXP, Value(common, commonXP), Value(ranked, rankedXP) });
		if (!CatalogReady()) return;
		std::map<std::string, int> legacyWeapons;
		if (local_challenges::XP() < 0)
			for (const auto& [weapon, definition] : g_catalog.weapons)
			{
				State mp{}, cp{};
				if (Field(common, { "commonData", "sharedProgression", "weaponLevel", weapon.c_str(), "mpXP" }, mp) &&
					Field(common, { "commonData", "sharedProgression", "weaponLevel", weapon.c_str(), "cpXP" }, cp))
					legacyWeapons[weapon] =
						int(std::min<int64_t>(definition.capXP, int64_t(std::max(0, Value(common, mp))) + std::max(0, Value(common, cp))));
			}
		if (!local_challenges::Initialize(legacyTotal, legacyWeapons)) return;
		local_challenges::Pump({});
		const int total = local_challenges::XP();
		if (total < 0) return;
		const bool changed = total != g_totalXP;
		g_totalXP = total;
		// Native GSC caps military pers.rankxp at maxXP-1. Preserve total separately
		// in common stats, so subsequent matches cannot discard excess earned XP.
		Set(common, commonXP, total);
		Set(ranked, rankedXP, std::min(total, g_maxXP - 1));
		RefreshView();
		local_challenges::ObserveRank("military", "player_ranked_up", g_view.rank, {}, g_rankGrants);
		if (g_season >= 0 && !g_seasonThresholds.empty())
			local_challenges::ObserveRank("season:" + std::to_string(g_season), "season_ranked_up", g_view.seasonRank - 1, {}, {});
		if (now >= g_nextWeaponPoll && CatalogReady())
		{
			g_nextWeaponPoll = now + 1000;
			for (const auto& [weapon, definition] : g_catalog.weapons)
			{
				State mp{}, cp{};
				if (Field(common, { "commonData", "sharedProgression", "weaponLevel", weapon.c_str(), "mpXP" }, mp) &&
					Field(common, { "commonData", "sharedProgression", "weaponLevel", weapon.c_str(), "cpXP" }, cp))
				{
					const auto xp = local_challenges::WeaponXP(weapon);
					Set(common, mp, xp);
					Set(common, cp, 0);
					g_weaponXP[weapon] = xp;
					local_challenges::ObserveRank(
						"weapon:" + weapon, "weapon_ranked_up", g_catalog.WeaponRank(weapon, xp), weapon, g_weaponGrants[weapon]);
				}
			}
		}
		if (changed)
		{
			// The normal stats pump saves at most once per two seconds. Do not force
			// a synchronous snapshot/FlushFileBuffers for every kill or XP tick.
			local_stats::Pump();
			LOG_INFO("Progression", "earned progression updated | totalXP=%d militaryRank=%d", total, g_view.rank + 1);
		}
	}

	View Get(int season)
	{
		if (!g_view.ready) return {};
		if (season >= 0 && season <= 6 && season != g_season)
		{
			const void* table = nullptr;
			const auto file = "mp/progression/elder" + std::to_string(season) + "_progression.csv";
			Native<void (*)(const char*, const void**)>(0x13e2b00)(file.c_str(), &table);
			std::vector<int> thresholds;
			if (table)
				for (int i = 1; i <= 100; ++i)
				{
					const int xp = Number(table, std::to_string(i).c_str(), 1);
					if (xp < 0 || (!thresholds.empty() && xp <= thresholds.back())) break;
					thresholds.push_back(xp);
				}
			g_season = season;
			g_seasonThresholds = std::move(thresholds);
			RefreshView();
		}
		return g_view;
	}

	WeaponView GetWeapon(const char* weapon)
	{
		if (!weapon || !g_view.ready || !g_catalogReady) return {};
		const auto found = g_weaponXP.find(weapon);
		if (found == g_weaponXP.end()) return {};
		return { true, found->second, g_catalog.WeaponRank(weapon, found->second) };
	}

	bool OwnsItem(int item)
	{
		if (!g_view.ready || !g_catalogReady || item <= 0) return false;
		return local_challenges::Owns(item) ||
			g_catalog.Owns(item, g_view.rank,
				[](const std::string& weapon)
				{
					const auto it = g_weaponXP.find(weapon);
					return it != g_weaponXP.end() ? it->second : 0;
				});
	}
}
