#pragma once
#include "auth/profile/xp_tokens.h"
#include "auth/profile/challenge_events.h"
#include "auth/profile/weapon_reference.h"
#include "auth/profile/progression_notices.h"
#include "thirdparty/json.hpp"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
namespace challenges
{
	using Json = nlohmann::json;

	inline int64_t Number(std::string_view text, int64_t fallback = -1)
	{
		int64_t value = 0;
		const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
		return !text.empty() && r.ec == std::errc{} && r.ptr == text.data() + text.size() ? value : fallback;
	}

	inline std::vector<std::string> Split(std::string_view text)
	{
		std::vector<std::string> result;
		while (!text.empty())
		{
			const auto end = text.find('|');
			if (end) result.emplace_back(text.substr(0, end));
			if (end == text.npos) break;
			text.remove_prefix(end + 1);
		}
		return result;
	}

	inline uint64_t Hash(std::string_view text)
	{
		uint64_t h = 14695981039346656037ull;
		for (uint8_t c : text)
			h = (h ^ c) * 1099511628211ull;
		return h;
	}

	class Engine
	{
		Json catalog_, state_;
		std::map<int, const Json*> rules_;
		std::map<std::string, std::vector<int>> events_;

		static int Capped(int64_t value)
		{
			return int(std::clamp<int64_t>(value, 0, INT_MAX));
		}

		static bool Contains(const Json& list, std::string_view text)
		{
			for (const auto& value : list)
				if (value.get<std::string>() == text) return true;
			return false;
		}

		bool Match(const Json& rule, const Event& e) const
		{
			auto p = [&](int i) -> std::string_view
			{
				return i >= 0 && size_t(i) < e.parameters.size() ? e.parameters[i] : std::string_view{};
			};
			const int mode = e.name == "kill" ? int(Number(p(5)))
				: e.name == "end_match" || e.name == "start_match" ? int(Number(p(0)))
				: state_.value("mode", -1);
			const bool br = mode == 24 || (mode >= 96 && mode <= 99);
			const auto scope = rule.value("scope", std::string("any"));
			if (mode < 5 || mode == 42 || mode == 43 || (scope == "br" && !br) || (scope == "mp" && br)) return false;
			if (e.name == "kill")
			{
				if (e.parameters.size() != 14) return false;
				for (const auto& [key, index] : { std::pair{ "mask1", 2 }, { "mask2", 3 }, { "enemyAll", 8 } })
				{
					const auto required = rule.value(key, 0u), actual = uint32_t(Number(p(index), 0));
					if ((actual & required) != required) return false;
				}
				const auto any = rule.value("enemyAny", 0u);
				if (any && !(uint32_t(Number(p(8), 0)) & any)) return false;
				// Native Report serializes arrays as value|value|, in script order.
				const auto weapon = progression::WeaponReference(p(1).substr(0, p(1).find('|')),
					[&](std::string_view ref)
					{
						return catalog_["weapons"].contains(std::string(ref));
					});
				if (rule.contains("weapon") && rule["weapon"].get<std::string>() != weapon) return false;
				if (rule.contains("weaponRefs") && !Contains(rule["weaponRefs"], p(1).substr(0, p(1).find('|')))) return false;
				if (rule.contains("classes"))
				{
					const auto cls = catalog_["weapons"].value(std::string(weapon), std::string{});
					if (!Contains(rule["classes"], cls)) return false;
				}
				const auto attachments = Split(p(6));
				const auto attachmentCount = std::count_if(attachments.begin(), attachments.end(),
					[](const std::string& a)
					{
						return a != "default_sniper_scope" && a != "no_attachments";
					});
				if (rule.contains("attachments") && unsigned(attachmentCount) != rule["attachments"].get<unsigned>()) return false;
				if (rule.contains("attachmentGroups"))
					for (const auto& group : rule["attachmentGroups"])
					{
						bool found = false;
						for (const auto& a : attachments)
							found |= Contains(group, a);
						if (!found) return false;
					}
				if (rule.contains("attachmentNames"))
				{
					if (!catalog_.contains("namedAttachments") || !catalog_["namedAttachments"].contains(std::string(weapon))) return false;
					const auto& names = catalog_["namedAttachments"][std::string(weapon)];
					for (const auto& name : rule["attachmentNames"])
					{
						const auto key = name.get<std::string>();
						if (!names.contains(key)) return false;
						bool found = false;
						for (const auto& a : attachments)
							found |= Contains(names[key], a);
						if (!found) return false;
					}
				}
				if (rule.contains("perks"))
				{
					const auto perks = Split(p(7));
					for (const auto& perk : rule["perks"])
						if (std::find(perks.begin(), perks.end(), perk.get<std::string>()) == perks.end()) return false;
				}
			}
			if (rule.contains("params"))
				for (const auto& [key, allowed] : rule["params"].items())
					if (!Contains(allowed, p(int(Number(key))))) return false;
			if (rule.contains("paramContains"))
				for (const auto& [key, allowed] : rule["paramContains"].items())
				{
					bool found = false;
					for (const auto& token : Split(p(int(Number(key)))))
						found |= Contains(allowed, token);
					if (!found) return false;
				}
			if (rule.contains("ranges"))
				for (const auto& [key, range] : rule["ranges"].items())
				{
					const auto value = Number(p(int(Number(key))));
					if (value < range[0].get<int64_t>() || value > range[1].get<int64_t>()) return false;
				}
			return true;
		}

		bool Eligible(const Json& rule) const
		{
			if (XP() < rule.value("minimumXP", 0)) return false;
			if (rule.contains("requiresProgress"))
				for (const auto& [key, required] : rule["requiresProgress"].items())
					if (Progress(int(Number(key))) < required.get<int>()) return false;
			if (rule.contains("requiresLoot"))
				for (const auto& item : rule["requiresLoot"])
					if (!Owns(item.get<int>())) return false;
			return true;
		}

		bool Active(const Json& rule) const
		{
			if (!Eligible(rule)) return false;
			const auto kind = rule["kind"].get<std::string>();
			const int id = rule["id"].get<int>();
			if (kind.starts_with("daily_"))
			{
				for (const auto& value : state_["daily"][kind])
					if (value.get<int>() == id) return true;
				return false;
			}
			if (kind.starts_with("weekly_") && rule.value("season", 0) != 3) return false;
			if (kind == "mission") return state_["missions"].value(std::to_string(rule.value("category", 0)), 0) == id;
			return true;
		}

		void Advance(const Json& rule, int amount)
		{
			if (amount <= 0) return;
			const auto key = std::to_string(rule["id"].get<int>());
			const int before = state_["progress"].value(key, 0);
			const int after = std::min(Capped(int64_t(before) + amount), rule["tiers"].back().get<int>());
			state_["progress"][key] = after;
			for (size_t i = 0; i < rule["tiers"].size(); ++i)
			{
				const int target = rule["tiers"][i].get<int>();
				if (before >= target || after < target) continue;
				if (i < rule["xp"].size()) AddXP(rule["xp"][i].get<int>());
				if (i < rule["loot"].size())
				{
					const int item = rule["loot"][i].get<int>();
					if (xp_tokens::Counted(item))
						Grant(item);
					else if (item > 0 && !Owns(item))
					{
						state_["owned"][std::to_string(item)] = true;
						Notices().Items({ item }, rule["kind"] == "gunsmith" || rule["kind"] == "reticle");
					}
				}
				const auto kind = rule["kind"].get<std::string>();
				static const std::map<std::string, std::string> events = { { "daily_mp", "completed_daily_challenge" },
					{ "daily_br", "completed_br_daily_challenge" }, { "weekly_mp", "completed_weekly_challenge" },
					{ "weekly_br", "completed_br_weekly_challenge" }, { "mission", "completed_mission" },
					{ "elder", "season_challenge_complete" } };
				if (const auto event = events.find(kind); event != events.end()) Notices().Push(event->second, rule["id"].get<int>());
			}
			if (rule["kind"] == "mission" && after == rule["tiers"].back())
			{
				const auto next = rule.value("nextRef", std::string{});
				int nextID = 0;
				for (const auto& [id, r] : rules_)
					if ((*r)["ref"] == next)
					{
						nextID = id;
						break;
					}
				state_["missions"][std::to_string(rule.value("category", 0))] = nextID;
			}
		}

	public:
		// Engine is never copied/moved: index pointers refer into its immutable catalog.
		Engine() = default;
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

		bool Load(std::string_view bytes)
		{
			auto data = Json::parse(bytes, nullptr, false);
			if (data.is_discarded() || data.value("version", 0) != 1 || !data.contains("rules") || !data["rules"].is_array()) return false;
			catalog_ = std::move(data);
			rules_.clear();
			events_.clear();
			for (const auto& rule : catalog_["rules"])
			{
				const int id = rule["id"].get<int>();
				if (id <= 0 || rule["tiers"].empty() || rules_.contains(id)) return false;
				rules_[id] = &rule;
				events_[rule["event"].get<std::string>()].push_back(id);
			}
			return true;
		}

		void Initialize(uint64_t xuid, int xp)
		{
			state_ = { { "version", 1 }, { "xuid", std::to_string(xuid) }, { "xp", Capped(xp) }, { "day", 0 },
				{ "progress", Json::object() }, { "owned", Json::object() }, { "sequences", Json::object() },
				{ "daily", { { "daily_mp", Json::array() }, { "daily_br", Json::array() } } }, { "missions", Json::object() },
				{ "counters", Json::object() }, { "mode", -1 }, { "matchKey", "" } };
		}

		bool Restore(const Json& saved, uint64_t xuid)
		{
			try
			{
				if (saved.at("version") != 1 || saved.at("xuid") != std::to_string(xuid) || !saved.at("xp").is_number_integer() ||
					saved.at("xp").get<int64_t>() < 0 || saved.at("xp").get<int64_t>() > INT_MAX || saved.at("sequences").size() > 64 ||
					saved.at("progress").size() > 10000 || saved.at("owned").size() > 20000 || saved.at("counters").size() > 10000)
					return false;
				for (const auto* key : { "progress", "owned", "sequences", "daily", "missions", "counters" })
					if (!saved.at(key).is_object()) return false;
				for (const auto& [key, n] : saved["progress"].items())
					if (Number(key) < 1 || !n.is_number_integer() || n.get<int64_t>() < 0 || n.get<int64_t>() > INT_MAX) return false;
				for (const auto& [key, n] : saved["owned"].items())
					if (Number(key) < 1 || Number(key) > 100000000 || !n.is_boolean()) return false;
				for (const auto& [key, n] : saved["sequences"].items())
					if (key.empty() || key.size() > 20 || key.find_first_not_of("0123456789") != key.npos || !n.is_number_unsigned())
						return false;
				if (!saved.at("day").is_number_integer() || saved.at("day").get<int64_t>() < 0 || saved.at("day").get<int64_t>() > 50000 ||
					!saved.at("mode").is_number_integer() || saved.at("mode").get<int>() < -1 || saved.at("mode").get<int>() > 1000 ||
					!saved.at("matchKey").is_string() || saved.at("matchKey").get_ref<const std::string&>().size() > 64)
					return false;
				for (const auto& [key, n] : saved["missions"].items())
					if (Number(key) < 0 || !n.is_number_integer() || n.get<int>() < 0 || n.get<int>() > 10000000) return false;
				for (const auto& [key, c] : saved["counters"].items())
				{
					if (Number(key) < 1 || !c.is_object() || c.size() > 16) return false;
					for (const auto& [field, value] : c.items())
					{
						if (field == "ended")
						{
							if (!value.is_boolean()) return false;
						}
						else if (field == "times")
						{
							if (!value.is_array() || value.size() > 1000) return false;
							for (const auto& t : value)
								if (!t.is_number_unsigned() || t.get<uint64_t>() > UINT32_MAX) return false;
						}
						else if (!value.is_number_integer() || value.get<int64_t>() < 0 || value.get<int64_t>() > UINT32_MAX)
							return false;
					}
				}
				for (const auto* kind : { "daily_mp", "daily_br" })
				{
					if (!saved["daily"].at(kind).is_array() || saved["daily"][kind].size() > 3) return false;
					for (const auto& id : saved["daily"][kind])
						if (!id.is_number_integer() || !rules_.contains(id.get<int>()) || (*rules_.at(id.get<int>()))["kind"] != kind)
							return false;
				}
				if (saved.contains("notifications") && !progression::Notices::Valid(saved["notifications"])) return false;
				const auto counts = saved.value("tokenGrants", Json::object());
				if (!counts.is_object() || counts.size() > xp_tokens::Definitions.size() + 1) return false;
				for (const auto& [key, n] : counts.items())
					if (!xp_tokens::Counted(int(Number(key))) || !n.is_number_integer() || n.get<int64_t>() < 0 ||
						n.get<int64_t>() > INT_MAX)
						return false;
				state_ = saved;
				state_["tokenGrants"] = counts;
				// Legacy booleans prove at least one grant, even without receipts.
				for (const auto& d : xp_tokens::Definitions)
					if (Owns(d.item) && !state_["tokenGrants"].contains(std::to_string(d.item)))
						state_["tokenGrants"][std::to_string(d.item)] = 1;
				if (Owns(xp_tokens::TrialTicket) && !state_["tokenGrants"].contains("200005")) state_["tokenGrants"]["200005"] = 1;
				return true;
			}
			catch (const Json::exception&)
			{
				return false;
			}
		}

		const Json& Snapshot() const
		{
			return state_;
		}

		progression::Notices Notices()
		{
			return progression::Notices(state_["notifications"]);
		}

		int XP() const
		{
			return state_.value("xp", 0);
		}

		void AddXP(int amount)
		{
			if (amount > 0) state_["xp"] = Capped(int64_t(XP()) + amount);
		}

		void TournamentProgress(int wins)
		{
			if (wins >= 0 && wins <= 4) state_["progress"]["1544"] = wins;
		}

		int Progress(int id) const
		{
			return state_["progress"].value(std::to_string(id), 0);
		}

		bool Owns(int item) const
		{
			return state_["owned"].value(std::to_string(item), false);
		}

		int TokenGrants(int item) const
		{
			return state_.contains("tokenGrants") ? state_.at("tokenGrants").value(std::to_string(item), Owns(item) ? 1 : 0)
				: (Owns(item) ? 1 : 0);
		}

		void RecoverTokenGrants(int item, int count)
		{
			if (!state_.contains("tokenGrants")) state_["tokenGrants"] = Json::object();
			state_["tokenGrants"][std::to_string(item)] = std::max(TokenGrants(item), count);
		}

		void Grant(int item)
		{
			if (xp_tokens::Counted(item))
			{
				if (!state_.contains("tokenGrants")) state_["tokenGrants"] = Json::object();
				auto& counts = state_["tokenGrants"];
				const auto key = std::to_string(item);
				counts[key] = Capped(int64_t(counts.value(key, 0)) + 1);
			}
			if (item > 0 && !Owns(item))
			{
				state_["owned"][std::to_string(item)] = true;
				Notices().Items({ item });
			}
		}

		void Mastery()
		{
			if (!catalog_.contains("mastery")) return;
			// Gold dependencies precede class mastery, which precedes Damascus.
			for (const auto& r : catalog_["mastery"])
			{
				bool complete = true;
				for (const auto& [id, target] : r.at("progress").items())
					complete &= Progress(int(Number(id))) >= target.get<int>();
				for (const auto& item : r.at("items"))
					complete &= Owns(item.get<int>());
				if (complete)
				{
					Grant(r.at("loot").get<int>());
					state_["progress"][std::to_string(r.at("id").get<int>())] = 1;
				}
			}
		}

		bool Knows(int id) const
		{
			if (rules_.contains(id)) return true;
			if (catalog_.contains("mastery"))
				for (const auto& r : catalog_["mastery"])
					if (r.at("id") == id) return true;
			return false;
		}

		void Day(uint32_t utc)
		{
			const unsigned day = utc / 86400;
			if (day <= state_.value("day", 0u)) return; // clock rollback cannot reaward dailies
			state_["day"] = day;
			for (const auto* kind : { "daily_mp", "daily_br" })
			{
				std::vector<std::pair<uint64_t, int>> order;
				for (const auto& [id, r] : rules_)
					if ((*r)["kind"] == kind)
					{
						const auto key = std::to_string(id);
						state_["progress"].erase(key);
						state_["counters"].erase(key);
						order.emplace_back(Hash(state_["xuid"].get<std::string>() + ":" + std::to_string(day) + ":" + key), id);
					}
				std::sort(order.begin(), order.end());
				std::set<std::string> seen;
				Json chosen = Json::array();
				for (const auto& [hash, id] : order)
					if (seen.insert((*rules_.at(id))["desc"].get<std::string>()).second)
					{
						chosen.push_back(id);
						if (chosen.size() == 3) break;
					}
				state_["daily"][kind] = std::move(chosen);
			}
		}

		std::vector<int> Assignments(std::string_view kind) const
		{
			std::vector<int> ids;
			if (kind == "daily_mp" || kind == "daily_br") return state_["daily"][std::string(kind)].get<std::vector<int>>();
			for (const auto& [id, r] : rules_)
				if ((*r)["kind"].get<std::string>() == kind && Active(*r)) ids.push_back(id);
			return ids;
		}

		bool Activate(int id)
		{
			const auto it = rules_.find(id);
			if (it == rules_.end() || (*it->second)["kind"] != "mission") return false;
			const auto& rule = *it->second;
			if (!Eligible(rule)) return false;
			// Completed stages cannot be reactivated to farm their rewards.
			if (Progress(id) >= rule["tiers"].back().get<int>()) return false;
			for (const auto& [otherID, other] : rules_)
				if (other->value("nextRef", std::string{}) == rule["ref"].get<std::string>() &&
					Progress(otherID) < (*other)["tiers"].back().get<int>())
					return false;
			state_["missions"][std::to_string(rule.value("category", 0))] = id;
			return true;
		}

		int Mission(int category) const
		{
			return state_["missions"].value(std::to_string(category), 0);
		}

		bool Apply(const Event& e, const std::function<int(const std::string&)>& weaponRank)
		{
			if (e.xuid == 0 || state_["xuid"] != std::to_string(e.xuid) || !e.session || !e.sequence) return false;
			const auto session = std::to_string(e.session);
			auto& ledger = state_["sequences"];
			if (e.sequence <= ledger.value(session, uint64_t(0))) return false;
			if (!ledger.contains(session) && ledger.size() >= 64) ledger.erase(ledger.begin());
			ledger[session] = e.sequence;
			const auto match = session + ":" + std::to_string(e.match);
			if (state_["matchKey"] != match)
			{
				state_["matchKey"] = match;
				state_["counters"] = Json::object();
				state_["mode"] = -1;
				for (const auto& [id, r] : rules_)
					if (r->value("sameMatch", false) && Progress(id) < r->at("tiers").back().get<int>())
						state_["progress"][std::to_string(id)] = 0;
			}
			if ((e.name == "start_match" || e.name == "end_match") && !e.parameters.empty()) state_["mode"] = int(Number(e.parameters[0]));
			if (e.name == "kill" && e.parameters.size() == 14) state_["mode"] = int(Number(e.parameters[5]));
			if (e.name == "mp_addxp" && !e.parameters.empty())
			{
				// Native formatter uses %f for script floats. Accept finite positive
				// decimals and truncate exactly as the original DDL XP path does.
				const char* begin = e.parameters[0].c_str();
				char* end = nullptr;
				const double xp = std::strtod(begin, &end);
				if (end != begin && *end == '\0' && std::isfinite(xp) && xp > 0 && xp <= INT_MAX) AddXP(int(xp));
			}
			if (e.name == "death")
				for (auto& [key, c] : state_["counters"].items())
					c["streak"] = 0;
			const auto found = events_.find(e.name);
			if (found == events_.end()) return true;
			// Snapshot active mission IDs so one event never completes two stages.
			std::vector<int> active;
			for (int id : found->second)
				if (Active(*rules_.at(id))) active.push_back(id);
			for (int id : active)
			{
				const auto& rule = *rules_.at(id);
				if (rule["kind"].get<std::string>().starts_with("daily_") && e.utc / 86400 != state_["day"].get<unsigned>()) continue;
				if (Progress(id) >= rule["tiers"].back().get<int>() || !Match(rule, e)) continue;
				if (rule.contains("level") && weaponRank(rule["weapon"]) < rule["level"].get<int>()) continue;
				const auto key = std::to_string(id);
				auto& counter = state_["counters"][key];
				if (counter.is_null()) counter = Json::object();
				int delta = 1;
				if (rule.contains("valueParam"))
				{
					const auto i = rule["valueParam"].get<unsigned>();
					if (i >= e.parameters.size()) continue;
					const auto n = Number(e.parameters[i]);
					if (n < 0 || n > INT_MAX) continue;
					const int unit = rule.value("unit", 1);
					const auto amount = n + counter.value("remainder", int64_t(0));
					delta = Capped(amount / unit);
					counter["remainder"] = amount % unit;
				}
				if (rule.contains("perMatch"))
				{
					const int n = Capped(int64_t(counter.value("match", 0)) + delta), target = rule["perMatch"].get<int>();
					const int before = counter.value("match", 0) / target, after = n / target;
					delta = rule.value("repeated", false) ? after - before : (before == 0 && after > 0 ? 1 : 0);
					counter["match"] = n;
				}
				if (rule.contains("streak"))
				{
					const int n = Capped(int64_t(counter.value("streak", 0)) + delta), target = rule["streak"].get<int>();
					delta = n / target - counter.value("streak", 0) / target;
					counter["streak"] = n;
				}
				if (rule.contains("windowMs"))
				{
					auto times = counter.value("times", std::vector<uint32_t>{});
					const auto limit = rule["windowMs"].get<uint32_t>();
					times.erase(std::remove_if(times.begin(), times.end(),
									[&](uint32_t t)
									{
										return e.milliseconds < t || e.milliseconds - t > limit;
									}),
						times.end());
					times.push_back(e.milliseconds);
					delta = 0;
					if (times.size() >= rule["windowCount"].get<unsigned>())
					{
						delta = 1;
						times.clear();
					}
					counter["times"] = times;
				}
				if (rule.contains("rapid"))
				{
					const auto last = counter.value("rapidTime", uint32_t(0));
					int count = counter.value("rapidCount", 0);
					// 1843.gsc updaterecentkills resets four seconds after the most
					// recent kill, rather than using a four-second fixed window.
					if (e.milliseconds < last || e.milliseconds - last >= 4000) count = 0;
					++count;
					counter["rapidCount"] = count;
					counter["rapidTime"] = e.milliseconds;
					delta = count == rule["rapid"].get<int>() ? 1 : 0;
				}
				// end_match reports totals: reconnecting within this map must not
				// count those totals or completed-match wins twice.
				if (e.name == "end_match")
				{
					if (counter.value("ended", false)) continue;
					counter["ended"] = true;
				}
				Advance(rule, delta);
			}
			return true;
		}
	};
}
