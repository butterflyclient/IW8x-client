#include "diagnostics/island/island_builtins.hpp"

#include "engine/image.hpp"
#include "engine/addresses.hpp"
#include "engine/safemem.h"
#include "gsc/internal/port_tables.hpp"

#include <array>
#include <algorithm>

namespace island_builtins
{
	namespace
	{
		bool island_water_query_verified{};
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
				if (id == 0x437 && island_water_query_verified)
					return reinterpret_cast<std::uintptr_t>(&shallow_water_at_point);
			return 0;
		}
	}

	void install()
	{
			island_water_query_verified = verify_replay_water_query();
		iw8x::gsc::internal::port_tables::function_at_slot = &function_at_slot;
	}
}
