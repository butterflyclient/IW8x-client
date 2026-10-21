#pragma once
#include "../../thirdparty/json.hpp"

#include <array>
#include <set>
#include <map>
#include <string>
#include <string_view>
namespace playlists
{
	inline bool IsMultiplayerContext(std::string_view context)
	{
		return context == "mp" || context == "gw" || context == "2v2" || context == "big" || context == "bm";
	}

	// TU19 contains five MP playlists outside its live category rotation. Expose
	// these through the existing native menu/filter datasources, without changing
	// their maps, weights, gametypes, recipes, or rules. Custom catalogs stay intact.
	inline std::string ExpandBundledCategories(std::string_view text)
	{
		auto root = nlohmann::json::parse(text);
		std::set<int> visible;
		for (const auto& category : root.at("categories"))
			for (const auto& child : category.at("bnet_include"))
				if (child.is_number_integer()) visible.insert(child.get<int>());
		for (const auto& playlist : root.at("playlists"))
		{
			const int id = playlist.at("playlist").get<int>();
			if (!IsMultiplayerContext(playlist.at("context").get<std::string>()) || visible.contains(id)) continue;
			const auto& type = playlist.at("maps").at(0).at("gametype").get_ref<const std::string&>();
			const char* target = type.starts_with("hc_") ? "H" : "B";
			bool added = false;
			for (auto& category : root.at("categories"))
			{
				if (category.at("category") != target) continue;
				auto& children = category.at("bnet_include");
				if (children.size() >= 20) throw std::runtime_error("expanded playlist category exceeds Replay capacity");
				children.push_back(id);
				added = true;
				break;
			}
			if (!added) throw std::runtime_error("missing native MP category for playlist");
		}
		return root.dump();
	}

	// Preflight only. Replay's Playlist_ParsePlaylists owns deserialization and
	// registration; these limits come from its fixed arrays (see playlist_port.md).
	struct DocumentInfo
	{
		std::array<bool, 129> ids{}, multiplayer{};
		// Empty means this playlist does not have one unambiguous BR client mode.
		// Gameplay/host rules still come from Replay's native parsed tables.
		std::array<std::string, 129> brVariant{};
		std::array<bool, 129> bloodMoney{};
		int version = 0;
		size_t gametypes = 0, entries = 0;
	};

	inline DocumentInfo ValidateDocument(std::string_view text)
	{
		using Json = nlohmann::json;
		const auto fail = [](const char* why)
		{
			throw std::runtime_error(why);
		};
		if (text.empty() || text.size() >= 0x50000 || text.find('\0') != text.npos)
			fail("playlist text is empty, oversized, or contains NUL");
		const Json root = Json::parse(text);
		if (!root.is_object() || !root.contains("system") || !root["system"].is_object()) fail("missing system object");
		for (const char* key : { "gametypes", "playlists", "categories" })
			if (!root.contains(key) || !root[key].is_array() || root[key].empty())
				fail("missing nonempty gametypes/playlists/categories array");
		if (root["gametypes"].size() > 64 || root["playlists"].size() > 129 || root["categories"].size() > 26)
			fail("playlist exceeds Replay table capacity");
		DocumentInfo info;
		info.version = root["system"].at("version").get<int>();
		info.gametypes = root["gametypes"].size();
		std::set<std::string> gametypes, maps, categories;
		std::map<std::string, const Json*> definitions;
		for (const auto& type : root["gametypes"])
		{
			const auto name = type.at("gametype").get<std::string>();
			const auto script = type.at("script").get<std::string>();
			if (name.empty() || name.size() >= 16 || script.empty() || script.size() >= 16 || !gametypes.insert(name).second)
				fail("invalid or duplicate gametype");
			if (type.contains("recipe") && (!type["recipe"].is_string() || type["recipe"].get_ref<const std::string&>().size() >= 8192))
				fail("recipe exceeds Replay's native 8192-byte text buffer");
			definitions.emplace(name, &type);
		}
		for (const auto& playlist : root["playlists"])
		{
			if (!playlist.at("playlist").is_number_integer()) fail("playlist ID must be an integer");
			const int id = playlist.at("playlist").get<int>();
			if (id < 0 || id > 128 || info.ids[id]) fail("invalid or duplicate playlist ID");
			info.ids[id] = true;
			info.multiplayer[id] = IsMultiplayerContext(playlist.value("context", std::string{}));
			const auto& entries = playlist.at("maps");
			if (!entries.is_array() || entries.empty()) fail("playlist has no map entries");
			info.entries += entries.size();
			if (info.entries > 2048) fail("more than 2048 map/gametype entries");
			for (const auto& entry : entries)
			{
				const auto map = entry.at("map").get<std::string>();
				const auto type = entry.at("gametype").get<std::string>();
				if (map.empty() || map.size() >= 24 || !gametypes.contains(type)) fail("invalid map or unknown gametype");
				maps.insert(map);
				if (!entry.at("weight").is_number_integer()) fail("map weight must be an integer");
				const int weight = entry.at("weight").get<int>();
				if (weight < 0 || weight > 255) fail("map weight exceeds byte range");
			}
			bool allBr = true, firstMode = true;
			std::string variant;
			bool bloodMoney = false;
			for (const auto& entry : entries)
			{
				const auto& type = *definitions.at(entry.at("gametype").get<std::string>());
				if (type.at("script") != "br")
				{
					allBr = false;
					break;
				}
				std::string mode = "br";
				bool money = false;
				// Preserve catalog order: gametype defaults, then playlist override.
				for (const auto* owner : { &type, &playlist })
				{
					if (!owner->contains("dvars")) continue;
					const auto& rules = owner->at("dvars");
					if (rules.contains("set scr_br_gametype")) mode = rules.at("set scr_br_gametype").get<std::string>();
					if (rules.contains("set ui_blood_money"))
					{
						const auto& value = rules.at("set ui_blood_money");
						money = value == 1 || value == true || value == "1";
					}
				}
				if (!firstMode && (variant != mode || bloodMoney != money))
				{
					allBr = false;
					break;
				}
				variant = mode;
				bloodMoney = money;
				firstMode = false;
			}
			if (allBr)
			{
				info.brVariant[id] = variant;
				info.bloodMoney[id] = bloodMoney;
			}
		}
		if (maps.size() > 128) fail("more than 128 distinct maps");
		for (const auto& category : root["categories"])
		{
			const auto name = category.at("category").get<std::string>();
			if ((name != "TOP" && (name.size() != 1 || name[0] < 'A' || name[0] > 'Y')) || !categories.insert(name).second)
				fail("invalid or duplicate category");
			const auto& include = category.at("bnet_include");
			if (!include.is_array() || include.size() > 20) fail("category exceeds 20 native entries");
			for (const auto& item : include)
			{
				if (item.is_string())
				{
					const auto name = item.get<std::string>();
					if (name.size() != 1 || name[0] < 'A' || name[0] > 'Y') fail("invalid category reference");
				}
				else
				{
					if (!item.is_number_integer()) fail("invalid playlist reference");
					const int id = item.get<int>();
					if (id < 0 || id > 128 || !info.ids[id]) fail("unknown playlist reference");
				}
			}
		}
		// Native rule conversion uses two 256-byte temporaries, a 1024-byte
		// formatted command, and a shared 200 KiB pool. Count conservatively.
		size_t pool = 4096 + maps.size() * 24;
		const auto visit = [&](const auto& self, const Json& value, int depth) -> void
		{
			if (depth > 32) fail("excessive JSON nesting");
			if (value.is_string())
			{
				const auto& s = value.get_ref<const std::string&>();
				if (s.find('\0') != s.npos) fail("embedded NUL in native string");
			}
			else if (value.is_array())
			{
				for (const auto& child : value)
					self(self, child, depth + 1);
			}
			else if (value.is_object())
			{
				for (auto it = value.begin(); it != value.end(); ++it)
				{
					if (it.key() == "dvars" || it.key() == "bnet_dvars")
					{
						if (!it->is_object()) fail("dvars must be an object");
						for (auto rule = it->begin(); rule != it->end(); ++rule)
						{
							const auto literal = rule->is_string() ? rule->get<std::string>() : rule->dump();
							if (rule.key().size() >= 256 || literal.size() >= 256 || rule->is_structured() || rule->is_null())
								fail("rule exceeds native conversion capacity");
							pool += rule.key().size() + literal.size() + 8;
						}
					}
					self(self, it.value(), depth + 1);
				}
			}
		};
		visit(visit, root, 0);
		if (pool >= 204800) fail("rules exceed native string pool");
		return info;
	}
}
