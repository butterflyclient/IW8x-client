#pragma once
#include "thirdparty/json.hpp"
#include "auth/profile/challenge_events.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <string>
#include <vector>
namespace after_action
{
	using Json = nlohmann::json;

	inline Json Empty()
	{
		return { { "version", 1 }, { "matches", Json::array() }, { "viewed", "" } };
	}

	inline bool Number(const Json& n, uint64_t max = INT_MAX)
	{
		return n.is_number_integer() && (n.is_number_unsigned() || n.get<int64_t>() >= 0) && n.get<uint64_t>() <= max;
	}

	inline bool Id(const std::string& id)
	{
		const auto colon = id.find(':');
		if (colon == id.npos || !colon || id.size() > 32) return false;
		uint64_t session = 0;
		uint32_t match = 0;
		const auto a = std::from_chars(id.data(), id.data() + colon, session);
		const auto b = std::from_chars(id.data() + colon + 1, id.data() + id.size(), match);
		return a.ec == std::errc{} && a.ptr == id.data() + colon && b.ec == std::errc{} && b.ptr == id.data() + id.size() && session &&
			match && id == std::to_string(session) + ":" + std::to_string(match);
	}

	inline Json Info(int xp, int battle)
	{
		Json j = Json::object();
		for (const auto* key : { "StartXP", "CurrentXP", "StartSeasonXP", "CurrentSeasonXP", "StartBattlepassXP", "CurrentBattlepassXP",
				 "BattlepassTimeXP", "BattlepassChallengeXP", "StartLevelXP", "CurrentLevelXP", "StartLevel", "CurrentLevel", "StartKeys",
				 "CurrentKeys", "StartCommonCrate", "CurrentCommonCrate", "StartCPpoints", "CurrentCPpoints", "ChallengeXP", "MissionXP",
				 "PlunderXP", "MatchXP" })
			j[key] = 0;
		j["StartXP"] = j["CurrentXP"] = xp;
		j["StartBattlepassXP"] = j["CurrentBattlepassXP"] = battle;
		return j;
	}

	inline bool ReportValid(const Json& r)
	{
		try
		{
			if (!r.is_object() || !Id(r.at("id").get<std::string>()) ||
				(r.at("status") != "playing" && r.at("status") != "completed" && r.at("status") != "left" &&
					r.at("status") != "interrupted") ||
				!r.at("mode").is_number_integer() || r.at("mode") < -1 || r.at("mode") > 255)
				return false;
			for (const auto* key : { "started", "updated", "ended" })
				if (!Number(r.at(key), UINT32_MAX)) return false;
			const auto fields = Info(0, 0);
			for (const auto& [key, unused] : fields.items())
				if (!Number(r.at("info").at(key))) return false;
			if (r.at("info").at("CurrentXP") < r.at("info").at("StartXP") ||
				r.at("info").at("CurrentBattlepassXP") < r.at("info").at("StartBattlepassXP"))
				return false;
			for (const auto* key : { "kills", "deaths", "seconds", "result" })
				if (!Number(r.at("stats").at(key))) return false;
			if (!r.at("unlocks").is_array() || r.at("unlocks").size() > 4096 || !r.at("challenges").is_array() ||
				r.at("challenges").size() > 4096 || !r.at("weapons").is_object() || r.at("weapons").size() > 256)
				return false;
			for (const auto& n : r.at("unlocks"))
				if (!Number(n, 100000000) || n == 0) return false;
			for (const auto& n : r.at("challenges"))
			{
				if (!n.at("id").is_string() || n.at("id").get<std::string>().empty() || n.at("id").get<std::string>().size() > 20 ||
					n.at("id").get<std::string>().find_first_not_of("0123456789") != std::string::npos || !n.at("kind").is_string() ||
					n.at("kind").get<std::string>().size() > 32 || !Number(n.at("count"), 128) || n.at("count") == 0)
					return false;
			}
			for (const auto& [ref, xp] : r.at("weapons").items())
				if (ref.size() > 100 || !Number(xp)) return false;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	inline bool Valid(const Json& state)
	{
		try
		{
			if (state.at("version") != 1 || !state.at("matches").is_array() || state.at("matches").size() > 16 ||
				!state.at("viewed").is_string())
				return false;
			std::vector<std::string> ids;
			for (const auto& r : state.at("matches"))
			{
				if (!ReportValid(r)) return false;
				const auto id = r.at("id").get<std::string>();
				if (std::find(ids.begin(), ids.end(), id) != ids.end()) return false;
				ids.push_back(id);
			}
			return state.at("viewed") == "" || Id(state.at("viewed").get<std::string>());
		}
		catch (...)
		{
			return false;
		}
	}

	inline int Add(int a, int b)
	{
		return int(std::min<int64_t>(INT_MAX, int64_t(a) + std::max(0, b)));
	}

	inline void Increment(Json& n, int amount)
	{
		n = Add(n.get<int>(), amount);
	}

	inline int Param(const challenges::Event& e, size_t i)
	{
		if (i >= e.parameters.size()) return 0;
		int value = 0;
		const auto& s = e.parameters[i];
		const auto r = std::from_chars(s.data(), s.data() + s.size(), value);
		return r.ec == std::errc{} && r.ptr == s.data() + s.size() && value >= 0 ? value : 0;
	}

	// TU19: 2244.gsc emits six end_match fields (no duration), and 1843.gsc
	// emits fourteen kill fields. Seconds means elapsed trusted event time.
	// Called only after the progression engine accepts a trusted dedicated event.
	// Deltas, report and replay receipts persist in the same account transaction.
	inline void Observe(Json& state, const challenges::Event& e, const Json& catalog, const Json& before, const Json& after,
		int battleBefore, int battleAfter, const Json& weaponsBefore, const Json& weaponsAfter, int directXP)
	{
		if (!e.match) return;
		const auto id = std::to_string(e.session) + ":" + std::to_string(e.match);
		auto& matches = state["matches"];
		auto at = std::find_if(matches.begin(), matches.end(),
			[&](const Json& r)
			{
				return r.at("id") == id;
			});
		if (at == matches.end())
		{
			if (!matches.empty() && matches.back().at("status") == "playing")
			{
				matches.back()["status"] = "interrupted";
				matches.back()["ended"] = e.utc;
			}
			if (matches.size() == 16) matches.erase(matches.begin());
			matches.push_back({ { "id", id }, { "status", "playing" }, { "mode", -1 }, { "started", e.utc }, { "updated", e.utc },
				{ "ended", 0 }, { "info", Info(before.at("xp").get<int>(), battleBefore) },
				{ "stats", { { "kills", 0 }, { "deaths", 0 }, { "seconds", 0 }, { "result", 0 } } }, { "unlocks", Json::array() },
				{ "challenges", Json::array() }, { "weapons", Json::object() } });
			at = matches.end() - 1;
		}
		auto& r = *at;
		r["updated"] = std::max(r.at("updated").get<uint32_t>(), e.utc);
		auto& info = r["info"];
		const int earned = std::max(0, after.at("xp").get<int>() - before.at("xp").get<int>());
		Increment(info["CurrentXP"], earned);
		Increment(info["ChallengeXP"], std::max(0, earned - directXP));
		Increment(info["MatchXP"], std::min(earned, directXP));
		Increment(info["CurrentBattlepassXP"], std::max(0, battleAfter - battleBefore));
		if ((e.name == "start_match" || e.name == "end_match") && !e.parameters.empty()) r["mode"] = std::min(255, Param(e, 0));
		if (e.name == "kill" && e.parameters.size() == 14)
		{
			Increment(r["stats"]["kills"], 1);
			r["mode"] = std::min(255, Param(e, 5));
		}
		if (e.name == "death") Increment(r["stats"]["deaths"], 1);
		if (e.name == "end_match" && e.parameters.size() == 6)
		{
			const int result = Param(e, 1);
			r["stats"]["result"] = result;
			r["stats"]["seconds"] =
				e.utc >= r.at("started").get<uint32_t>() ? std::min(uint32_t(INT_MAX), e.utc - r.at("started").get<uint32_t>()) : 0;
			r["status"] = result ? "completed" : "left";
			r["ended"] = e.utc;
		}
		for (const auto& [ref, xp] : weaponsAfter.items())
		{
			const int delta = xp.get<int>() - weaponsBefore.value(ref, 0);
			if (delta > 0) r["weapons"][ref] = Add(r["weapons"].value(ref, 0), delta);
		}
		for (const auto& [item, owned] : after.at("owned").items())
			if (owned == true && !before.at("owned").value(item, false) && r["unlocks"].size() < 4096)
			{
				const int loot = std::stoi(item);
				if (std::find(r["unlocks"].begin(), r["unlocks"].end(), Json(loot)) == r["unlocks"].end()) r["unlocks"].push_back(loot);
			}
		if (before.at("progress") == after.at("progress")) return;
		for (const auto& rule : catalog.at("rules"))
		{
			const auto key = std::to_string(rule.at("id").get<int64_t>());
			const int old = before.at("progress").value(key, 0), next = after.at("progress").value(key, 0);
			if (next <= old) continue;
			int count = 0;
			for (const auto& tier : rule.at("tiers"))
				if (tier > old && tier <= next) ++count;
			if (!count) continue;
			auto& rows = r["challenges"];
			auto found = std::find_if(rows.begin(), rows.end(),
				[&](const Json& n)
				{
					return n.at("id") == key;
				});
			if (found != rows.end())
				(*found)["count"] = std::min(128, found->at("count").get<int>() + count);
			else if (rows.size() < 4096)
				rows.push_back({ { "id", key }, { "kind", rule.value("kind", std::string("gunsmith")) }, { "count", count } });
		}
	}

	inline bool Ack(Json& state, const std::string& id)
	{
		if (state.at("viewed") == id) return false;
		if (state.at("matches").empty()) return false;
		const auto& r = state.at("matches").back();
		if (r.at("id") == id && r.at("status") != "playing")
		{
			state["viewed"] = id;
			return true;
		}
		return false;
	}

	inline Json Latest(const Json& state)
	{
		return state.at("matches").empty() ? Json() : state.at("matches").back();
	}
}
