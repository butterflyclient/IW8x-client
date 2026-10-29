#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace gsc::island_attachment_caps
{
	// Replay 1.20.4 TU19 mp/statstable.csv has an empty source-compatible
	// max-attachments column (18) for all 70 native rows. Source 1.64's
	// scripts/mp/class.gsc::loadout_updateclasscustom reads this column and
	// clears an otherwise valid class's *entire* attachment array when empty.
	// The genuine Source1.64 mp/statstable.csv is in the archived
	// code_post_gfx.raw (SHA256 020f3f674498c4cca03c197d33c90cb06d0b29cdc3c7ce0fcbed35359cbb3d4a),
	// 227 rows x 63 columns. Its column18 is "5" for *all 57* shared MW19
	// weapon IDs (including melee and launcher rows), while later S4 weapons
	// have separate 9/10 caps. This exact-name list comes from Replay's pinned
	// original 70-row table, NOT a broad weapon-prefix wildcard.
	// The original five-attachment budget remains enforced by class.gsc; a
	// genuine nonempty cell always wins. Ported/unknown weapons and unrelated
	// equipment are excluded.
	inline constexpr std::array<std::string_view, 57> original_weapons{{
		"iw8_pi_golf21", "iw8_pi_papa320", "iw8_pi_decho", "iw8_pi_mike1911",
		"iw8_pi_cpapa", "iw8_pi_mike9", "iw8_sm_mpapa5", "iw8_sm_beta",
		"iw8_sm_augolf", "iw8_sm_papa90", "iw8_sm_mpapa7", "iw8_sm_uzulu",
		"iw8_sm_charlie9", "iw8_sm_smgolf45", "iw8_sm_victor", "iw8_ar_mike4",
		"iw8_me_riotshield", "iw8_knife", "iw8_fists", "iw8_knifestab",
		"iw8_ar_akilo47", "iw8_ar_asierra12", "iw8_ar_falpha", "iw8_ar_mcharlie",
		"iw8_ar_kilo433", "iw8_ar_falima", "iw8_ar_scharlie", "iw8_ar_tango21",
		"iw8_ar_sierra552", "iw8_ar_galima", "iw8_sn_mike14", "iw8_sn_kilo98",
		"iw8_la_rpapa7", "iw8_la_gromeo", "iw8_la_juliet", "iw8_la_kgolf", "iw8_la_mike32",
		"iw8_sn_sbeta", "iw8_sn_golf28", "iw8_sn_sksierra", "iw8_sn_alpha50",
		"iw8_sn_delta", "iw8_sn_hdromeo", "iw8_sn_xmike109", "iw8_sn_crossbow",
		"iw8_sh_dpapa12", "iw8_sh_oscar12", "iw8_sh_charlie725", "iw8_sh_romeo870",
		"iw8_sh_mike26", "iw8_lm_kilo121", "iw8_lm_pkilo", "iw8_lm_lima86",
		"iw8_lm_mgolf34", "iw8_lm_mgolf36", "iw8_lm_mkilo3", "iw8_lm_dblmg"
	}};

	constexpr const char* missing_column_limit(const std::string_view table, const int search_column,
		const std::string_view weapon, const int result_column, const std::string_view actual) noexcept
	{
		if (table != "mp/statstable.csv" || search_column != 4 || result_column != 18 || !actual.empty())
			return nullptr;
		for (const auto entry : original_weapons)
			if (entry == weapon) return "5";
		return nullptr;
	}

	static_assert(missing_column_limit("mp/statstable.csv", 4, "iw8_ar_mcharlie", 18, "") != nullptr);
	static_assert(missing_column_limit("mp/statstable.csv", 4, "iw8_sn_hdromeo", 18, "") != nullptr);
	static_assert(missing_column_limit("mp/statstable.csv", 4, "iw8_ar_t9soviet", 18, "") == nullptr);
	static_assert(missing_column_limit("mp/statstable.csv", 4, "iw8_me_riotshield", 18, "") != nullptr);
	static_assert(missing_column_limit("mp/statstable.csv", 4, "iw8_la_gromeo", 18, "") != nullptr);
	static_assert(missing_column_limit("mp/statstable.csv", 4, "iw8_ar_mcharlie", 18, "10") == nullptr);
	static_assert(missing_column_limit("mp/statstable.csv", 4, "iw8_ar_mcharlie", 17, "") == nullptr);
}
