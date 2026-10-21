#pragma once
#include "network/playlist_document.h"

#include <vector>
namespace playlists
{
	// TU27's five long loot-filter pairs do not fit TU19's 256-byte temporary.
	// Parse placeholders through the native parser, then expand ONLY those values
	// in its resulting playlist rule strings. Keep the original JSON for recipes.
	struct LongRule
	{
		int playlist;
		std::string marker, value;
	};

	struct NativeCatalog
	{
		std::string text;
		std::vector<LongRule> rules;
	};

	inline NativeCatalog PrepareNativeCatalog(std::string_view text)
	{
		auto root = nlohmann::json::parse(text);
		NativeCatalog out{ std::string(text), {} };
		if (root.at("system").value("title_update", std::string{}) != "_tu27")
		{
			ValidateDocument(text);
			return out;
		}
		for (auto& playlist : root.at("playlists"))
		{
			if (!playlist.contains("dvars")) continue;
			for (auto& [key, value] : playlist.at("dvars").items())
			{
				if (!value.is_string() || value.get_ref<const std::string&>().size() < 256) continue;
				const auto original = value.get<std::string>();
				if ((key != "set loot_table_filter" && key != "set MSLKNNLLMN") || original.size() > 512 ||
					original.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_ ") != original.npos)
					throw std::runtime_error("unsupported long TU27 rule");
				const auto marker = "__magma_tu27_rule_" + std::to_string(out.rules.size()) + "__";
				if (text.find(marker) != text.npos) throw std::runtime_error("TU27 placeholder collision");
				out.rules.push_back({ playlist.at("playlist").get<int>(), marker, original });
				value = marker;
			}
		}
		if (out.rules.size() > 32) throw std::runtime_error("too many extended TU27 rules");
		out.text = root.dump();
		ValidateDocument(out.text);
		return out;
	}

	inline DocumentInfo ValidateTransferDocument(std::string_view text)
	{
		if (text.empty() || text.size() >= 0x50000 || text.find('\0') != text.npos)
			throw std::runtime_error("playlist transfer exceeds native text capacity");
		return ValidateDocument(PrepareNativeCatalog(text).text);
	}

	inline std::string ExpandLongRules(std::string rules, int id, const std::vector<LongRule>& values)
	{
		for (const auto& value : values)
		{
			if (value.playlist != id) continue;
			const auto marker = "\"" + value.marker + "\"";
			const auto at = rules.find(marker);
			if (at == rules.npos || rules.find(marker, at + marker.size()) != rules.npos)
				throw std::runtime_error("native TU27 rule placeholder missing or repeated");
			rules.replace(at, marker.size(), "\"" + value.value + "\"");
		}
		if (rules.size() >= 65536) throw std::runtime_error("extended playlist rules exceed bound");
		return rules;
	}
}
