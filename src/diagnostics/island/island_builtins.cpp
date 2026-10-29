#include "diagnostics/island/island_builtins.hpp"

#include "engine/image.hpp"
#include "engine/addresses.hpp"
#include "engine/safemem.h"
#include "gsc/internal/port_tables.hpp"
#include "gsc/compiled/island_damage_native_abi.hpp"
#include "diagnostics/night/night_files.h"
#include "common/console.h"

#include <array>
#include <algorithm>
#include <atomic>

namespace island_builtins
{
	namespace
	{
		bool island_water_query_verified{};
		bool island_reserved_count_verified{};
			bool island_damage_protection_verified{};
		constexpr std::uintptr_t replay_reserved_count_rva = 0xD591AC8;
			constexpr std::uintptr_t replay_level_time_rva = 0xBC21730;
			// Source1.64 native GSC method 0x87D3 reports a time-limited
			// player-damage shield. Replay1.20 keeps equivalent state at
			// gclient+0x5F54 (flag), +0xF81C (expiry) and level.time at
			// 0xBC21730, used directly by finishplayerdamage 0x1201490.
			// Unlike the plugin's undefined default, native int0/int1 can be
			// consumed by the following OP_CastBool without a VM runtime error.
			void timed_damage_protection(void* context, std::uint64_t packed_self)
			{
				using namespace game::addresses::gsc_api;
				const auto add_integer = reinterpret_cast<void (*)(void*, int)>(game::at(Scr_AddInt));
				const auto add_undefined = reinterpret_cast<void (*)(void*)>(game::at(Scr_AddUndefined));
				const auto slot = static_cast<std::uint32_t>(packed_self);
				const auto entity_class = static_cast<std::uint8_t>(packed_self >> 32);
				if (!island_damage_protection_verified || !night_files::DedicatedIslandBR() ||
					entity_class != 0 || slot >= 200)
				{
					add_undefined(context);
					return;
				}
				std::uintptr_t pool{}, in_use{};
				std::uintptr_t client{};
				int owner{}, flag{}, expiry{}, now{};
				const bool valid = safemem::ReadBytes(game::at(0xBC20F00), &pool, sizeof(pool)) && pool &&
					safemem::ReadBytes(game::at(entity_in_use_pointer), &in_use, sizeof(in_use)) && in_use &&
					[&]()
					{
						std::uint8_t occupied{};
						const auto entity = reinterpret_cast<const std::uint8_t*>(pool + 0x5A0ull * slot);
						return safemem::ReadBytes(reinterpret_cast<const void*>(in_use + slot), &occupied, 1) && occupied == 1 &&
							safemem::ReadBytes(entity + 0x150, &client, sizeof(client)) && client &&
							safemem::ReadBytes(reinterpret_cast<const void*>(client + 0x1C0), &owner, sizeof(owner)) &&
							owner == static_cast<int>(slot) &&
							safemem::ReadBytes(reinterpret_cast<const void*>(client + 0x5F54), &flag, sizeof(flag)) &&
							safemem::ReadBytes(reinterpret_cast<const void*>(client + 0xF81C), &expiry, sizeof(expiry)) &&
							safemem::ReadBytes(game::at(replay_level_time_rva), &now, sizeof(now));
					}();
				if (!valid)
				{
					// Fail closed for stale/non-player references; never synthesize
					// a false "unprotected" status for an invalid native entity.
					add_undefined(context);
					return;
				}
				add_integer(context,
					gsc::island_damage_native_abi::timed_protection_active(flag, expiry, now) ? 1 : 0);
			}

			bool verify_replay_timed_damage_protection() noexcept
			{
				// Actual Replay1.20.4 IDA instructions: compare the flag to 0,
				// read g_level.time, compare expiry and reject when expiry>now.
				constexpr std::array<std::uint8_t, 7> flag_compare{
					0x44, 0x39, 0xA9, 0x54, 0x5F, 0x00, 0x00
				};
				constexpr std::array<std::uint8_t, 6> load_level_time{
					0x8B, 0x05, 0xEF, 0x01, 0xA2, 0x0A
				};
				constexpr std::array<std::uint8_t, 12> expiry_compare{
					0x39, 0x81, 0x1C, 0xF8, 0x00, 0x00, 0x0F, 0x8F, 0x24, 0x09, 0x00, 0x00
				};
				std::array<std::uint8_t, flag_compare.size()> f{};
				std::array<std::uint8_t, load_level_time.size()> t{};
				std::array<std::uint8_t, expiry_compare.size()> e{};
				return safemem::ReadBytes(game::at(0x1201532), f.data(), f.size()) && f == flag_compare &&
					safemem::ReadBytes(game::at(0x120153B), t.data(), t.size()) && t == load_level_time &&
					safemem::ReadBytes(game::at(0x1201541), e.data(), e.size()) && e == expiry_compare;
			}
		// Original 1.64.5 GSC function 0x437 (RVA 0x34C6420):
		//   Scr_GetVector(ctx, 0, &point);
		//   Scr_AddBool(ctx, Physics_QueryPointContents(0, point, -1, 0x20));
		// Unlike the five-argument GSC physics_querypoint, the native physics
		// operation itself is structurally equivalent in Replay 1.20.4:
		//   source 0x305EA50 -> Replay 0x1098C50, both call the matching
		//   Physics_QueryPoint wrapper 0x304BED0 -> 0x10908E0.
		// Preserve the original world, ignored-entity and water mask exactly.
		void shallow_water_at_point(void* context)
		{
			float position[3]{};
			reinterpret_cast<void (*)(void*, unsigned int, float*)>(
				game::at(game::addresses::gsc_api::Scr_GetVector))(context, 0, position);
			const auto contents = reinterpret_cast<int (*)(int, const float*, int, unsigned int)>(
				game::at(game::addresses::gsc_api::Physics_QueryPointContents))(0, position, -1, 0x20);
			reinterpret_cast<void (*)(void*, int)>(game::at(game::addresses::gsc_api::Scr_AddBool))(
				context, contents != 0);
		}

		bool verify_replay_water_query() noexcept
		{
			// Fail closed if the actual Replay point-contents implementation
			// differs. Never jump into an assumed address from a newer build.
			constexpr std::array<unsigned char, 27> entry{
				0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24,
				0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x56,
				0x41, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x41, 0x8B, 0xF9
			};
			std::array<unsigned char, entry.size()> actual{};
			return safemem::ReadBytes(
				game::at(game::addresses::gsc_api::Physics_QueryPointContents), actual.data(), actual.size()) &&
				std::equal(actual.begin(), actual.end(), entry.begin());
		}

		bool verify_replay_reserved_count() noexcept
		{
			// IsReserved and the cache query use the same engine-owned boundary
			// between reserved/runtime instances and fixed map instances.
			constexpr std::array<unsigned char, 38> is_reserved{
				0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8,
				0x92, 0xED, 0x01, 0x00, 0x33, 0xD2, 0x48, 0x8B, 0xCB, 0x3B,
				0x05, 0x7F, 0x55, 0x35, 0x0C, 0x0F, 0x92, 0xC2, 0x48, 0x83,
				0xC4, 0x20, 0x5B, 0xE9, 0x0A, 0x66, 0x0E, 0x00
			};
			constexpr std::array<unsigned char, 6> cache_compare{ 0x3B, 0x0D, 0xBA, 0x6A, 0x28, 0x0C };
			constexpr std::array<unsigned char, 6> cache_subtract{ 0x2B, 0x1D, 0x7C, 0x6A, 0x28, 0x0C };
			std::array<unsigned char, is_reserved.size()> actual_reserved{};
			std::array<unsigned char, cache_compare.size()> actual_compare{};
			std::array<unsigned char, cache_subtract.size()> actual_subtract{};
			return safemem::ReadBytes(game::at(0x123C530), actual_reserved.data(), actual_reserved.size()) &&
				actual_reserved == is_reserved &&
				safemem::ReadBytes(game::at(0x130B008), actual_compare.data(), actual_compare.size()) &&
				actual_compare == cache_compare &&
				safemem::ReadBytes(game::at(0x130B046), actual_subtract.data(), actual_subtract.size()) &&
				actual_subtract == cache_subtract;
		}

		void reserved_scriptable_capacity(void* context)
		{
			// Source 0x434 / RVA 0x34B7B60 returns its total reserved count at
			// 0x1343CC90. Replay's matching world count changes with map lifetime;
			// read it on each call. The free-list/remaining getter is different.
			const auto count = *game::at<const std::uint32_t>(replay_reserved_count_rva);
			reinterpret_cast<void (*)(void*, int)>(game::at(game::addresses::gsc_api::Scr_AddInt))(
				context, static_cast<int>(count));
			static std::atomic_flag reported{};
			if (!reported.test_and_set(std::memory_order_relaxed))
				LOG_INFO("IslandLoot", "native total reserved Scriptable capacity=%u (source builtin 0x434)", count);
		}

		void rank_table(void* context)
		{
			// Source34A4590 ->30D3310(0,false): crosstitle_ranktables.csv,
			// current-title row, column2. Replay supports IW8; its native
			// Com_GetRankDataMP uses the same mp/rankTable.csv dependency.
			reinterpret_cast<void (*)(void*, const char*)>(game::at(game::addresses::gsc_api::Scr_AddString))(
				context, "mp/ranktable.csv");
		}

		void rank_icon_table(void* context)
		{
			// Source34A45C0 selects column3 in the same original IW8 row.
			reinterpret_cast<void (*)(void*, const char*)>(game::at(game::addresses::gsc_api::Scr_AddString))(
				context, "mp/rankicontable.csv");
		}

		std::uintptr_t function_at_slot(const std::uint32_t slot)
		{
			const auto id = game::addresses::gsc_api::stock_function_base + game::addresses::gsc_api::stock_function_count + slot;
			if (id == 0x428) return reinterpret_cast<std::uintptr_t>(&rank_table);
			if (id == 0x429) return reinterpret_cast<std::uintptr_t>(&rank_icon_table);
			if (id == 0x434 && island_reserved_count_verified)
				return reinterpret_cast<std::uintptr_t>(&reserved_scriptable_capacity);
				if (id == 0x437 && island_water_query_verified)
					return reinterpret_cast<std::uintptr_t>(&shallow_water_at_point);
			return 0;
		}
			std::uintptr_t method_at_slot(const std::uint32_t slot)
			{
				constexpr auto method_id = 0x87D3u;
				const auto requested_id = game::addresses::gsc_api::stock_method_base +
					game::addresses::gsc_api::stock_method_count + slot;
				return island_damage_protection_verified && requested_id == method_id
					? reinterpret_cast<std::uintptr_t>(&timed_damage_protection) : 0;
			}
	}

	void install()
	{
			island_water_query_verified = verify_replay_water_query();
		island_reserved_count_verified = verify_replay_reserved_count();
			island_damage_protection_verified = verify_replay_timed_damage_protection();
		iw8x::gsc::internal::port_tables::function_at_slot = &function_at_slot;
			iw8x::gsc::internal::port_tables::method_at_slot = &method_at_slot;
			if (island_damage_protection_verified)
				LOG_INFO("IslandDamage", "native timed damage protection method 0x87D3 restored with verified Replay fields");
			else LOG_WARN("IslandDamage", "native timed damage protection method 0x87D3 skipped: Replay signature mismatch");
	}
}
