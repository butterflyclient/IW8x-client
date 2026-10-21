#pragma once
#include "thirdparty/json.hpp"

#include <array>
#include <algorithm>
#include <set>
#include <string>
#include <string_view>
namespace motd
{
	using Json = nlohmann::json;
	inline constexpr std::array<int, 7> Locations{ 1, 2, 6, 7, 8, 11, 12 };

	inline bool Text(const Json& value, size_t limit, bool nonempty = false)
	{
		if (!value.is_string()) return false;
		const auto& s = value.get_ref<const std::string&>();
		return s.size() <= limit && (!nonempty || !s.empty()) && s.find('\0') == s.npos;
	}

	inline bool Unsigned(const Json& value, uint64_t max)
	{
		return value.is_number_unsigned() && value.get<uint64_t>() <= max;
	}

	inline bool Record(const Json& r)
	{
		try
		{
			if (!r.is_object() || r.size() != 6 || !Text(r.at("id"), 16, true)) return false;
			const auto id = r.at("id").get<std::string>();
			if (id[0] == '0' || id.find_first_not_of("0123456789") != id.npos || std::stoull(id) > 9007199254740991ull) return false;
			if (!r.at("location").is_number_integer()) return false;
			const int loc = r.at("location").get<int>();
			if (std::find(Locations.begin(), Locations.end(), loc) == Locations.end()) return false;
			if (!Unsigned(r.at("starts"), 4102444800ull) || !Unsigned(r.at("ends"), 4102444800ull)) return false;
			if (r.at("ends") != 0 && r.at("ends") <= r.at("starts")) return false;
			if (!Text(r.at("locale"), 8, true)) return false;
			const auto& m = r.at("message");
			// Native metadata parser has a 4096-byte template and a 6144-byte output.
			// Support text/key-art cards first; actions/assets have separate services.
			if (!m.is_object() || m.size() != 6 || m.at("layout_type") != 0 || !Text(m.at("title"), 128, true) ||
				!Text(m.at("content_short"), 384) || !Text(m.at("content_long"), 1600) || m.at("image") != "mw_keyart_small" ||
				m.at("popup_image") != "mw_keyart")
				return false;
			const auto bytes = m.dump();
			return bytes.size() < 3500 && bytes.find("{{") == bytes.npos;
		}
		catch (...)
		{
			return false;
		}
	}

	inline bool Records(const Json& records)
	{
		if (!records.is_array() || records.size() > 15) return false;
		std::set<std::string> ids;
		std::array<int, 15> counts{};
		for (const auto& r : records)
		{
			if (!Record(r) || !ids.insert(r.at("id").get<std::string>()).second) return false;
			const int loc = r.at("location");
			// Limit each location to the native cache budget; all modes share 15 slots.
			if (++counts[loc] > (loc == 1 || loc == 6 || loc == 11 ? 1 : 2)) return false;
		}
		return true;
	}

	inline bool Catalog(const Json& j)
	{
		try
		{
			return j.is_object() && j.size() == 3 && j.at("version") == 1 && Unsigned(j.at("revision"), 0xFFFFFFFFull) &&
				j.at("revision") != 0 && Records(j.at("messages"));
		}
		catch (...)
		{
			return false;
		}
	}

	inline Json Defaults()
	{
		Json records = Json::array();
		for (int loc : { 1, 2, 6, 7 })
		{
			const bool br = loc >= 6, main = loc == 1 || loc == 6;
			records.push_back({ { "id", std::to_string(1201000 + loc) }, { "location", loc }, { "starts", uint64_t{ 0 } },
				{ "ends", uint64_t{ 0 } }, { "locale", "en" },
				{ "message",
					{ { "title", main ? (br ? "Warzone" : "Multiplayer") : "Missions & Daily Challenges" },
						{ "content_short",
							main ? (br ? "Play in teams of four." : "Find your next match in Quick Play.")
								 : "Earn XP and rewards through challenges." },
						{ "content_long",
							main ? "Select a playlist to join an available dedicated server."
								 : "Choose a mission in Challenges and track your daily objectives." },
						{ "layout_type", 0 }, { "image", "mw_keyart_small" }, { "popup_image", "mw_keyart" } } } });
		}
		return { { "version", 1 }, { "revision", uint32_t{ 1 } }, { "messages", records } };
	}

	inline Json Select(const Json& catalog, uint64_t utc, std::string_view locale)
	{
		Json records = Json::array();
		for (const auto& r : catalog.at("messages"))
			if (r.at("starts").get<uint64_t>() <= utc && (!r.at("ends").get<uint64_t>() || utc < r.at("ends").get<uint64_t>()) &&
				(r.at("locale").get_ref<const std::string&>() == locale || r.at("locale") == "*"))
				records.push_back(r);
		return records;
	}

	inline bool Response(const Json& data)
	{
		try
		{
			return data.at("kind") == "motd" && data.at("catalog_version") == 1 && Unsigned(data.at("revision"), 0xFFFFFFFFull) &&
				data.at("revision") != 0 && Records(data.at("records")) && data.at("next_page") == "";
		}
		catch (...)
		{
			return false;
		}
	}
}
