#pragma once
#include "auth/profile/challenge_engine.h"
#include "network/social/chat_backend.h"

#include <map>
#include <set>
#include <algorithm>
namespace tournament120
{
	using Json = nlohmann::json;
	inline constexpr int Playlist = 15, TeamCount = 16, TeamSize = 2, RewardChallenge = 1544;
	inline constexpr uint64_t AllocationBase = uint64_t(1) << 62;

	struct Award
	{
		std::string user;
		int wins;
	};

	inline bool UInt(const Json& j, uint64_t max)
	{
		return (j.is_number_unsigned() || (j.is_number_integer() && j.get<int64_t>() >= 0)) && j.get<uint64_t>() <= max;
	}

	inline bool User(const std::string& s)
	{
		return chat::Id(Json{ { "id", s } }, "id") != 0;
	}

	struct Host
	{
		bool sampled = false, waiting = false, running = false;
		uint64_t frame = 0, allocation = 0;
		std::map<std::string, int> teams;
	};

	struct Reservation
	{
		uint64_t id = 0;
		std::map<uint64_t, int> teams;
	};

	// One installed dedicated server plays bracket matches sequentially. The
	// provider retains results across restarts; an interrupted match is replayed.
	// HTTP supplies registration/readiness only. Results enter through Observe.
	class Service
	{
		Json state{ { "version", 1 }, { "next", uint64_t(1) }, { "queue", Json::array() }, { "bracket", Json::object() } };
		std::map<std::string, uint32_t> presence;
		std::set<std::string> ready;
		int active = -1;
		uint64_t allocation = 0, session = 0, retiring = 0;
		uint32_t match = 0, endedAt = 0;
		std::map<std::string, int> results, teams;

		Json& Bracket()
		{
			return state["bracket"];
		}

		const Json& Bracket() const
		{
			return state.at("bracket");
		}

		static bool Contains(const Json& members, const std::string& user)
		{
			return std::any_of(members.begin(), members.end(),
				[&](const Json& p)
				{
					return p.at("id") == user;
				});
		}

		bool Fresh(const std::string& user, uint32_t utc) const
		{
			const auto at = presence.find(user);
			return at != presence.end() && utc >= at->second && utc - at->second < 30;
		}

		std::vector<std::string> Players(int index) const
		{
			std::vector<std::string> ids;
			if (Bracket().empty() || index < 0 || index >= 15) return ids;
			for (const auto& team : Bracket().at("matches").at(index).at("teams"))
			{
				if (team < 0) continue;
				for (const auto& p : Bracket().at("teams").at(team.get<int>()))
					ids.push_back(p.at("id"));
			}
			return ids;
		}

		void ResetMatch()
		{
			active = -1;
			allocation = session = 0;
			match = endedAt = 0;
			results.clear();
			teams.clear();
			ready.clear();
		}

		bool Withdrawn(int team) const
		{
			if (team < 0 || Bracket().empty()) return false;
			const auto& list = Bracket().at("withdrawn");
			return std::find(list.begin(), list.end(), team) != list.end();
		}

		void Advance(int index, int winner)
		{
			Bracket()["matches"][index]["winner"] = winner;
			if (index < 14) Bracket()["matches"][8 + index / 2]["teams"][index % 2] = winner;
		}

		void Forfeits()
		{
			if (Bracket().empty()) return;
			for (int i = 0; i < 15; ++i)
			{
				const auto& m = Bracket().at("matches").at(i);
				const int a = m.at("teams").at(0), b = m.at("teams").at(1);
				if (m.at("winner") != -1 || a < 0 || b < 0 || (!Withdrawn(a) && !Withdrawn(b))) continue;
				// Withdrawal advances the bracket but never grants combat rewards.
				Advance(i, Withdrawn(a) ? b : a);
				if (active == i)
				{
					retiring = allocation;
					ResetMatch();
				}
			}
		}

		void StartBracket()
		{
			if (!Bracket().empty() && Bracket().at("matches").back().at("winner") == -1) return;
			size_t players = 0;
			for (const auto& q : state.at("queue"))
				players += q.at("members").size();
			if (players < 32) return;
			Json squads = Json::array(), singles = Json::array();
			for (const auto& q : state.at("queue"))
			{
				if (q.at("members").size() == 2)
					squads.push_back(q.at("members"));
				else
					singles.push_back(q.at("members").front());
			}
			for (size_t i = 0; i < singles.size(); i += 2)
				squads.push_back(Json::array({ singles.at(i), singles.at(i + 1) }));
			Json matches = Json::array();
			for (int i = 0; i < 15; ++i)
				matches.push_back({ { "teams", i < 8 ? Json::array({ i * 2, i * 2 + 1 }) : Json::array({ -1, -1 }) }, { "winner", -1 } });
			Bracket() = { { "id", state.at("next") }, { "teams", squads }, { "matches", matches }, { "withdrawn", Json::array() } };
			state["next"] = state.at("next").get<uint64_t>() + 1;
			state["queue"] = Json::array();
			ResetMatch();
		}

	public:
		const Json& Save() const
		{
			return state;
		}

		Json Awards() const
		{
			return state.value("awards", Json::object());
		}

		void AckAward(const std::string& id, int wins)
		{
			if (state.contains("awards") && state["awards"].value(id, 0) <= wins) state["awards"].erase(id);
		}

		bool Load(const Json& value)
		{
			try
			{
				if (value.at("version") != 1 || !UInt(value.at("next"), 1000000000) || value.at("next") == 0 ||
					!value.at("queue").is_array() || value.at("queue").size() > 32 || !value.at("bracket").is_object())
					return false;
				std::set<std::string> ids;
				size_t count = 0;
				const auto members = [&](const Json& players, size_t minimum, size_t maximum)
				{
					if (!players.is_array() || players.size() < minimum || players.size() > maximum) return false;
					for (const auto& p : players)
					{
						const auto id = p.at("id").get<std::string>(), name = p.at("name").get<std::string>();
						if (!User(id) || !ids.insert(id).second || name.empty() || name.size() > 64 || name.find('\0') != name.npos)
							return false;
					}
					return true;
				};
				for (const auto& q : value.at("queue"))
				{
					if (!members(q.at("members"), 1, 2) || !Contains(q.at("members"), q.at("leader"))) return false;
					if (q.contains("expires") && !UInt(q.at("expires"), UINT32_MAX)) return false;
					count += q.at("members").size();
				}
				if (count > 32) return false;
				const auto& b = value.at("bracket");
				if (!b.empty())
				{
					ids.clear();
					if (!UInt(b.at("id"), 1000000000) || b.at("id") == 0 || b.at("id") >= value.at("next") || !b.at("teams").is_array() ||
						b.at("teams").size() != 16 || !b.at("matches").is_array() || b.at("matches").size() != 15 ||
						!b.at("withdrawn").is_array() || b.at("withdrawn").size() > 16)
						return false;
					for (const auto& squad : b.at("teams"))
						if (!members(squad, 2, 2)) return false;
					std::set<int> withdrawn;
					for (const auto& t : b.at("withdrawn"))
						if (!UInt(t, 15) || !withdrawn.insert(t.get<int>()).second) return false;
					for (int i = 0; i < 15; ++i)
					{
						const auto& m = b.at("matches").at(i);
						if (!m.at("teams").is_array() || m.at("teams").size() != 2 || !m.at("winner").is_number_integer()) return false;
						for (int side = 0; side < 2; ++side)
						{
							const int expected = i < 8 ? i * 2 + side : b.at("matches").at((i - 8) * 2 + side).at("winner").get<int>();
							if (m.at("teams").at(side) != expected) return false;
						}
						const int winner = m.at("winner");
						if (winner != -1 &&
							(m.at("teams").at(0) < 0 || m.at("teams").at(1) < 0 ||
								(winner != m.at("teams").at(0) && winner != m.at("teams").at(1))))
							return false;
					}
				}
				const auto awards = value.value("awards", Json::object());
				if (!awards.is_object() || awards.size() > 1024) return false;
				for (const auto& [id, w] : awards.items())
					if (!User(id) || !UInt(w, 4) || w == 0) return false;
				state = value;
				presence.clear();
				retiring = 0;
				ResetMatch();
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		int Team(const std::string& user) const
		{
			if (!Bracket().empty())
				for (int i = 0; i < 16; ++i)
					if (Contains(Bracket().at("teams").at(i), user)) return i;
			return -1;
		}

		int Pending(const std::string& user) const
		{
			const int team = Team(user);
			if (team < 0 || Withdrawn(team)) return -1;
			for (int i = 0; i < 15; ++i)
			{
				const auto& m = Bracket().at("matches").at(i);
				if (m.at("winner") == -1 && (m.at("teams").at(0) == team || m.at("teams").at(1) == team)) return i;
			}
			return -1;
		}

		bool Registered(const std::string& user) const
		{
			for (const auto& q : state.at("queue"))
				if (Contains(q.at("members"), user)) return true;
			return false;
		}

		bool Reserved() const
		{
			return active >= 0 || retiring != 0;
		}

		bool Allowed(const std::set<uint64_t>& members, const std::set<int>& selected) const
		{
			if (retiring) return false;
			if (active < 0) return true;
			if (selected != std::set<int>{ Playlist }) return false;
			const auto players = Players(active);
			int partyTeam = -1;
			for (auto id : members)
			{
				const auto user = std::to_string(id);
				if (std::find(players.begin(), players.end(), user) == players.end()) return false;
				if (partyTeam < 0)
					partyTeam = Team(user);
				else if (Team(user) != partyTeam)
					return false;
			}
			return !members.empty();
		}

		unsigned Handle(const std::string& user, const std::string& op, const Json& members, uint32_t utc)
		{
			if (!User(user)) return 400;
			if (op == "register" || op == "ready")
			{
				if (!members.is_array() || members.empty() || members.size() > 2) return 400;
				std::set<std::string> ids;
				for (const auto& p : members)
				{
					if (!p.is_object() || !p.contains("id") || !p["id"].is_string() || !p.contains("name") || !p["name"].is_string())
						return 400;
					const auto id = p["id"].get<std::string>(), name = p["name"].get<std::string>();
					if (!User(id) || !ids.insert(id).second || !chat::Text(name, 35)) return 400;
				}
			}
			presence[user] = utc;
			auto& queue = state["queue"];
			for (auto it = queue.begin(); it != queue.end();)
			{
				if (utc >= it->value("expires", UINT32_MAX))
					it = queue.erase(it);
				else
				{
					if (Contains(it->at("members"), user) && it->value("expires", uint32_t(0)) < utc + 40) (*it)["expires"] = utc + 60;
					++it;
				}
			}
			if (op == "register")
			{
				if (Registered(user))
				{
					for (const auto& q : queue)
						if (Contains(q.at("members"), user)) return q.at("members") == members ? 200 : 409;
				}
				if (Pending(user) >= 0) return 409;
				if (!members.is_array() || members.empty() || members.size() > 2 || !Contains(members, user)) return 400;
				size_t count = 0;
				for (const auto& q : state.at("queue"))
					count += q.at("members").size();
				if (count + members.size() > 32) return 409;
				for (const auto& p : members)
					if (Registered(p.at("id")) || Pending(p.at("id")) >= 0) return 409;
				state["queue"].push_back({ { "leader", user }, { "members", members }, { "expires", utc + 60 } });
				StartBracket();
				return 200;
			}
			if (op == "cancel")
			{
				auto& q = state["queue"];
				for (auto i = q.begin(); i != q.end();)
					if (Contains(i->at("members"), user))
						i = q.erase(i);
					else
						++i;
				ready.erase(user);
				const int team = Team(user);
				if (team >= 0 && !Withdrawn(team))
				{
					Bracket()["withdrawn"].push_back(team);
					Forfeits();
				}
				return 200;
			}
			if (op == "ready")
			{
				if (Pending(user) < 0 || !members.is_array() || !Contains(members, user)) return 409;
				for (const auto& member : members)
					if (Team(member.at("id")) != Team(user)) return 409;
				ready.insert(user);
				return 200;
			}
			if (op == "unready")
			{
				if (!Reserved()) ready.erase(user);
				return 200;
			}
			if (op == "fetch" || op == "rejoin") return 200;
			return 400; // There is deliberately no client result/award operation.
		}

		Reservation Poll(const Host& host, uint64_t now, uint32_t utc, bool enabled)
		{
			if (!host.sampled || now < host.frame || now - host.frame >= 5000) return {};
			if (retiring)
			{
				if (host.waiting && !host.running)
					retiring = 0;
				else
					return { retiring, {} };
			}
			if (active >= 0 && host.allocation == allocation)
			{
				if (host.running)
				{
					const auto players = Players(active);
					auto observed = host.teams;
					if (observed.size() == 4 && players.size() == 4 && observed[players[0]] > 0 && observed[players[2]] > 0 &&
						observed[players[0]] == observed[players[1]] && observed[players[2]] == observed[players[3]] &&
						observed[players[0]] != observed[players[2]])
						teams = std::move(observed);
					else
						teams.clear();
				}
				else if (host.waiting && session)
				{
					if (!endedAt) endedAt = utc;
					if (utc >= endedAt && utc - endedAt >= 10) ResetMatch();
				}
			}
			if (active >= 0 && !session && host.waiting)
			{
				const auto players = Players(active);
				if (!enabled ||
					!std::all_of(players.begin(), players.end(),
						[&](const auto& id)
						{
							return Fresh(id, utc);
						}))
					ResetMatch();
			}
			if (active < 0 && enabled && host.waiting && !host.running && !Bracket().empty())
			{
				for (int i = 0; i < 15; ++i)
				{
					const auto& m = Bracket().at("matches").at(i);
					if (m.at("winner") != -1 || m.at("teams").at(0) < 0 || m.at("teams").at(1) < 0) continue;
					const auto players = Players(i);
					if (players.size() == 4 &&
						std::all_of(players.begin(), players.end(),
							[&](const auto& id)
							{
								return ready.contains(id) && Fresh(id, utc);
							}))
					{
						active = i;
						allocation = AllocationBase + state.at("next").get<uint64_t>();
						state["next"] = state.at("next").get<uint64_t>() + 1;
						break;
					}
				}
			}
			Reservation result;
			if (active >= 0)
			{
				result.id = allocation;
				const auto players = Players(active);
				for (size_t i = 0; i < players.size(); ++i)
					result.teams[std::stoull(players[i])] = int(i / 2) + 1;
			}
			return result;
		}

		std::vector<Award> Observe(const challenges::Event& e, uint64_t sourceAllocation)
		{
			std::vector<Award> awards;
			if (active < 0 || sourceAllocation != allocation || !e.session || !e.match || !e.sequence) return awards;
			const auto players = Players(active);
			const auto user = std::to_string(e.xuid);
			if (std::find(players.begin(), players.end(), user) == players.end()) return awards;
			if (e.name == "start_match" && !e.parameters.empty() && e.parameters[0] == "25")
			{
				if (!session)
				{
					session = e.session;
					match = e.match;
				}
				return awards;
			}
			if (e.name != "end_match" || e.session != session || e.match != match || e.parameters.size() != 6 || e.parameters[0] != "25" ||
				teams.empty())
				return awards;
			const int result = int(challenges::Number(e.parameters[1]));
			if (result != 1 && result != 2) return awards;
			results.emplace(user, result);
			if (results.size() != 4) return awards;
			if (results[players[0]] != results[players[1]] || results[players[2]] != results[players[3]] ||
				results[players[0]] == results[players[2]])
				return awards;
			auto& m = Bracket()["matches"][active];
			const int winner = m["teams"][results[players[0]] == 1 ? 0 : 1];
			const int wins = active < 8 ? 1 : active < 12 ? 2 : active < 14 ? 3 : 4;
			for (const auto& p : Bracket()["teams"][winner])
				awards.push_back({ p.at("id"), wins });
			if (!state.contains("awards")) state["awards"] = Json::object();
			for (const auto& a : awards)
				state["awards"][a.user] = std::max(state["awards"].value(a.user, 0), a.wins);
			Advance(active, winner);
			retiring = allocation;
			ResetMatch();
			Forfeits();
			return awards;
		}

		Json Projection(const std::string& user, uint32_t utc) const
		{
			const int team = Team(user), pending = Pending(user);
			int status = 0, wins = 0;
			if (team >= 0 && !Withdrawn(team))
			{
				status = pending >= 0 ? 1 : 2;
				for (const auto& m : Bracket().at("matches"))
					wins += m.at("winner") == team;
				if (Bracket().at("matches").back().at("winner") == team) status = 3;
			}
			size_t queued = 0;
			for (const auto& q : state.at("queue"))
				queued += q.at("members").size();
			Json available = Json::array();
			for (const auto& id : ready)
				if (Fresh(id, utc)) available.push_back(id);
			return { { "registered", Registered(user) }, { "queued_players", queued }, { "required_players", 32 }, { "team_size", 2 },
				{ "status", status }, { "team", team }, { "wins", wins }, { "pending_match", pending }, { "active_match", active },
				{ "join", active >= 0 && pending == active }, { "ready", available }, { "bracket", Bracket() }, { "playlist", Playlist } };
		}
	};
}
