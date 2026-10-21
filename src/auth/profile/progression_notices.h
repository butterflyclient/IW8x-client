#pragma once
#include "../../../thirdparty/json.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace progression
{
	using NoticeJson = nlohmann::json;

	struct Notice
	{
		uint64_t sequence = 0;
		std::string event, weapon;
		int value = 0;
		std::vector<int> items;
	};

	inline bool ChallengeNotice(const std::string& name)
	{
		return name == "completed_daily_challenge" || name == "completed_br_daily_challenge" || name == "completed_weekly_challenge" ||
			name == "completed_br_weekly_challenge" || name == "completed_mission" || name == "season_challenge_complete";
	}

	inline bool KnownNotice(const std::string& name)
	{
		return ChallengeNotice(name) || name == "player_ranked_up" || name == "weapon_ranked_up" || name == "season_ranked_up" ||
			name == "progression_items_unlocked" || name == "gunsmith_items_unlocked";
	}

	// Stored alongside reward ownership and replay sequence IDs. Old saves start
	// with empty presentation baselines; loading existing progress grants no popups.
	class Notices
	{
		NoticeJson& data_;

	public:
		explicit Notices(NoticeJson& data) : data_(data)
		{
			if (data_.is_null())
				data_ = { { "next", 1 }, { "pending", NoticeJson::array() }, { "levels", NoticeJson::object() },
					{ "items", NoticeJson::object() } };
		}

		static bool Valid(const NoticeJson& data)
		{
			try
			{
				if (!data.is_object() || (!data.at("next").is_number_unsigned() && !data.at("next").is_number_integer()) ||
					data.at("next").get<int64_t>() < 1 || !data.at("pending").is_array() || !data.at("levels").is_object() ||
					!data.at("items").is_object() || data.at("pending").size() > 10000 || data.at("levels").size() > 256 ||
					data.at("items").size() > 20000)
					return false;
				uint64_t previous = 0;
				for (const auto& n : data["pending"])
				{
					const auto sequence = n.at("sequence").get<uint64_t>();
					if (sequence <= previous || sequence >= data["next"].get<uint64_t>() ||
						!KnownNotice(n.at("event").get<std::string>()) || n.at("weapon").get<std::string>().size() > 128 ||
						n.at("value").get<int>() < 0 || !n.at("items").is_array() || n["items"].size() > 512)
						return false;
					for (const auto& item : n["items"])
						if (item.get<int>() <= 0 || item.get<int>() > 100000000) return false;
					previous = sequence;
				}
				for (const auto& [key, value] : data["levels"].items())
					if (key.size() > 160 || value.get<int>() < -1 || value.get<int>() > 10000) return false;
				for (const auto& [key, value] : data["items"].items())
					if (key.size() > 10 || !value.is_boolean() || !value.get<bool>()) return false;
				return true;
			}
			catch (const NoticeJson::exception&)
			{
				return false;
			}
		}

		void Push(const std::string& event, int value, const std::string& weapon = {}, const std::vector<int>& items = {})
		{
			// Bound presentation history if no HUD consumes it for thousands of
			// rewards. Expiry never removes ownership, XP or challenge progress.
			if (data_["pending"].size() >= 10000)
			{
				data_["pending"].erase(data_["pending"].begin());
				data_["expired"] = data_.value("expired", uint64_t(0)) + 1;
			}
			const auto sequence = data_["next"].get<uint64_t>();
			data_["next"] = sequence + 1;
			data_["pending"].push_back(
				{ { "sequence", sequence }, { "event", event }, { "value", value }, { "weapon", weapon }, { "items", items } });
		}

		void Items(const std::vector<int>& items, bool gunsmith = false)
		{
			std::vector<int> fresh;
			for (int item : items)
				if (item > 0 && !data_["items"].contains(std::to_string(item)))
				{
					data_["items"][std::to_string(item)] = true;
					fresh.push_back(item);
				}
			if (!fresh.empty()) Push(gunsmith ? "gunsmith_items_unlocked" : "progression_items_unlocked", 0, {}, fresh);
		}

		void Observe(const std::string& key, const std::string& event, int rank, const std::string& weapon,
			const std::map<int, std::vector<int>>& grants)
		{
			if (rank < -1) return;
			auto& levels = data_["levels"];
			if (!levels.contains(key))
			{
				levels[key] = rank;
				for (const auto& [level, items] : grants)
					if (level <= rank)
						for (int item : items)
							data_["items"][std::to_string(item)] = true;
				return;
			}
			const int before = levels[key].get<int>();
			if (rank <= before) return; // A stale stats bank cannot re-arm earned levels.
			levels[key] = rank;
			for (int level = before + 1; level <= rank; ++level)
			{
				Push(event, level, weapon);
				if (const auto at = grants.find(level); at != grants.end()) Items(at->second, !weapon.empty());
			}
		}

		Notice Peek(bool challenge) const
		{
			for (const auto& n : data_["pending"])
				if (ChallengeNotice(n["event"]) == challenge)
					return { n["sequence"].get<uint64_t>(), n["event"].get<std::string>(), n["weapon"].get<std::string>(),
						n["value"].get<int>(), n["items"].get<std::vector<int>>() };
			return {};
		}

		bool Ack(uint64_t sequence)
		{
			auto& pending = data_["pending"];
			for (auto it = pending.begin(); it != pending.end(); ++it)
				if ((*it)["sequence"] == sequence)
				{
					pending.erase(it);
					return true;
				}
			return false;
		}
	};
}
