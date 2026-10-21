#pragma once
#include "network/social/chat_backend.h"
#include "network/social/user_lists.h"
#include <functional>
// Authoritative local social service. Storage and sockets are injected by the
// host; engine pointers never leave the native adapter. IDs are decimal strings
// on the wire so a JSON consumer cannot round a 64-bit UNO/XUID.

namespace social
{
	using chat::Json;
	using chat::Id;
	using chat::Answer;

	struct Session
	{
		std::string id, bytes, endpoint, map, mode;
		int playlist = 0, capacity = 0;
		uint64_t owner = 0;
		uint8_t gameMode = 2;
		std::set<uint64_t> members;
	};

	// TU19 SessionInfo::Serialize106E5D0/12C0730: metadata, CommonAddr84,
	// security KEY16 then ID8. The registered in-memory session uses ID then KEY.
	inline std::string GameJoinReply(const Session& s, bool tu43 = false)
	{
		constexpr char digits[] = "0123456789abcdef";
		std::string out = tu43 ? "494e563a0001018200" : "494e563a0001018100";
		out += digits[s.gameMode >> 4];
		out += digits[s.gameMode & 15];
		out += "0002"; // Public game, native PC ClientPlatform=2 (10AE340).
		out += std::string(34, '0'); // lobbyID64, matchmakingID64, blocked=false.
		if (tu43) out += "00"; // TU43 SessionInfo+129: extra bool precedes has-session.
		out += "01"; // Actual security/session follows.
		return out + s.bytes.substr(0, 168) + s.bytes.substr(184, 32) + s.bytes.substr(168, 16);
	}

	inline bool ValidInviteEnvelope(std::string_view bytes, bool tu43 = false)
	{
		if (bytes.size() < 12 || bytes.size() > 768 || bytes.size() % 2 || !chat::Hex(bytes, bytes.size()) ||
			!bytes.starts_with("494e563a00"))
			return false;
		const auto op = bytes.substr(10, 2);
		if (op == "00") return bytes.size() == 12;
		if (op == "01")
			return (bytes.size() == 16 && bytes.substr(12, 2) == "00") ||
				(bytes.size() == (tu43 ? 278 : 276) && bytes.substr(12, 6) == (tu43 ? "018200" : "018100") &&
					bytes.substr(tu43 ? 60 : 58, 2) == "01");
		if (op != "02") return false;
		size_t end = 12;
		while (end + 2 <= bytes.size() && bytes.substr(end, 2) != "00")
			end += 2;
		return end <= 82 && end + 26 == bytes.size(); // name<=35 + NUL + time32 + UNO64
	}

	class Backend
	{
		struct User
		{
			std::string name, key;
			std::set<uint64_t> friends, requests;
			int availability = 2;
			UserLists playerLists;
		};

		struct Event
		{
			uint64_t id = 0, from = 0, expires = 0;
			std::string bytes;
			Json resolved;
		};

		struct Client
		{
			uint64_t seen = 0, sequence = 0, lastSend = 0, host = 0, partySeen = 0;
			uint32_t ip = 0;
			std::string lease, nonce, party, game, activity = "menus";
			std::set<uint64_t> subscriptions, members;
			std::deque<Event> inbox;

			struct Sent
			{
				uint64_t request, target;
				std::string bytes;
				Answer answer;
			};

			std::deque<Sent> sent;

			struct Mutation
			{
				uint64_t request, target;
				std::string path;
				Answer answer;
			};

			std::deque<Mutation> mutations;
			uint64_t highMutation = 0;
			std::map<uint64_t, uint64_t> pendingJoins;
			uint64_t highRequest = 0;
		};

		std::map<uint64_t, User> users;
		std::map<uint64_t, Client> clients;
		std::map<uint64_t, Session> parties;

		// Optional TU43 Discord capabilities. Ephemeral and never persisted as
		// friendships; accepting a session invite grants only a short join window.
		struct DiscordInvite
		{
			std::string token, session, lease;
			uint64_t expires = 0;
		};

		struct DiscordGrant
		{
			uint64_t sender = 0, receiver = 0, expires = 0;
			std::string session;
		};

		std::map<uint64_t, DiscordInvite> discordInvites;
		std::deque<DiscordGrant> discordGrants;
		Session game;
		uint64_t gameAt = 0, revision = 1;
		bool dirty = false;

		static Answer Error(unsigned code, const char* message)
		{
			return { code, { { "error", message } } };
		}

		bool Friends(uint64_t a, uint64_t b) const
		{
			auto x = users.find(a), y = users.find(b);
			return x != users.end() && y != users.end() && !Blocked(a, b) && x->second.friends.contains(b) && y->second.friends.contains(a);
		}

		bool SessionBlocked(uint64_t id, const Session* s) const
		{
			return s &&
				std::any_of(s->members.begin(), s->members.end(),
					[&](uint64_t member)
					{
						return Blocked(id, member);
					});
		}

		bool JoinAllowed(uint64_t a, uint64_t b, uint64_t now) const
		{
			if (Friends(a, b)) return true;
			if (Blocked(a, b)) return false;
			for (const auto& g : discordGrants)
				if (now < g.expires && ((a == g.sender && b == g.receiver) || (b == g.sender && a == g.receiver)))
				{
					const auto* session = Resolve(g.sender);
					if (session && session->id == g.session && !SessionBlocked(g.receiver, session)) return true;
				}
			return false;
		}

		void Expire(uint64_t now)
		{
			std::erase_if(clients,
				[&](const auto& p)
				{
					return now >= p.second.seen && now - p.second.seen > 15000;
				});
			for (auto& [id, c] : clients)
				if (now >= c.partySeen && now - c.partySeen > 5000)
				{
					c.party.clear();
					c.host = 0;
					c.members.clear();
					parties.erase(id);
				}
			std::erase_if(parties,
				[&](const auto& p)
				{
					return !clients.contains(p.first);
				});
			if (now >= gameAt && now - gameAt > 3000) game = {};
			std::erase_if(discordInvites,
				[&](const auto& p)
				{
					const auto c = clients.find(p.first);
					return now >= p.second.expires || c == clients.end() || c->second.lease != p.second.lease;
				});
			std::erase_if(discordGrants,
				[&](const auto& g)
				{
					return now >= g.expires || !clients.contains(g.sender) || !clients.contains(g.receiver);
				});
			for (auto& [id, c] : clients)
			{
				std::erase_if(c.inbox,
					[&](const Event& e)
					{
						return now >= e.expires || !clients.contains(e.from) || !JoinAllowed(id, e.from, now) ||
							(!e.resolved.is_null() && (Resolve(e.from) != &game || e.resolved.value("bytes", std::string{}) != game.bytes));
					});
				std::erase_if(c.pendingJoins,
					[&](const auto& p)
					{
						return now >= p.second || !JoinAllowed(id, p.first, now);
					});
			}
		}

		const Session* Resolve(uint64_t target, bool requireSpace = true) const
		{
			auto c = clients.find(target);
			if (c == clients.end() || users.at(target).availability == 4) return nullptr;
			if (!game.id.empty() && game.members.contains(target) && c->second.game == game.id)
				return !requireSpace || game.members.size() < size_t(game.capacity) ? &game : nullptr;
			if (c->second.activity != "menus") return nullptr;
			auto h = clients.find(c->second.host);
			auto p = parties.find(c->second.host);
			if (h == clients.end() || p == parties.end() || h->second.activity != "menus" || !h->second.members.contains(target) ||
				!c->second.members.contains(h->first) || c->second.party != p->second.id ||
				(requireSpace && p->second.members.size() >= size_t(p->second.capacity)))
				return nullptr;
			return &p->second;
		}

		Json Presence(uint64_t id) const
		{
			auto c = clients.find(id);
			const auto* s = Resolve(id, false);
			const bool online = c != clients.end() && users.at(id).availability != 4;
			std::string activity = online ? c->second.activity : "offline";
			if (online && !game.id.empty() && game.members.contains(id) && c->second.game == game.id) activity = "ingame";
			return { { "xuid", std::to_string(id) }, { "online", online }, { "activity", activity }, { "joinable", Resolve(id) != nullptr },
				{ "session", s ? s->id : "" }, { "kind", s ? (s == &game ? "game" : "party") : "" }, { "map", s ? s->map : "" },
				{ "mode", s ? s->mode : "" }, { "playlist", s ? s->playlist : 0 },
				{ "availability", online ? users.at(id).availability : 4 } };
		}

		Json FriendsList(uint64_t id) const
		{
			Json out = Json::array();
			for (auto target : users.at(id).friends)
			{
				auto it = users.find(target);
				if (it == users.end() || Blocked(id, target)) continue;
				auto row = Presence(target);
				row["name"] = it->second.name;
				out.push_back(std::move(row));
			}
			return out;
		}

		static bool ValidSession(const Session& s)
		{
			return chat::Hex(s.id, 16) && chat::Hex(s.bytes, 216) && s.bytes.substr(168, 16) == s.id && s.id != "0000000000000000" &&
				s.capacity > 0 && s.capacity <= 200 && chat::Text(s.endpoint, 128) && s.map.size() <= 64 && s.mode.size() <= 24;
		}

	public:
		// Read-only communications policy for the TU43 adapter. It reuses the
		// authenticated lease and canonical account name, never a claimed sender.
#include "network/social/backend/authorization.inl"
		bool CommunicationsLive(uint64_t id, const std::string& lease, uint64_t now) const
		{
			const auto c = clients.find(id);
			return c != clients.end() && c->second.lease == lease && (now < c->second.seen || now - c->second.seen <= 15000);
		}

		bool CommunicationsAllowed(uint64_t receiver, uint64_t sender) const
		{
			const auto user = users.find(receiver);
			return user != users.end() && !Blocked(receiver, sender) && !user->second.playerLists.Contains(2, sender);
		}

		bool PartyRelayAllowed(uint64_t a, uint64_t b, uint64_t now) const
		{
			const auto x = clients.find(a), y = clients.find(b);
			if (x == clients.end() || y == clients.end() || Blocked(a, b) || !CommunicationsLive(a, x->second.lease, now) ||
				!CommunicationsLive(b, y->second.lease, now))
				return false;
			if (JoinAllowed(a, b, now)) return true;
			// Friends of the leader can exchange native party/voice datagrams once
			// both report the same live squad. A claimed roster on one end is not
			// enough to route packets to an unrelated account.
			return !x->second.party.empty() && x->second.party == y->second.party && x->second.host == y->second.host &&
				x->second.members.contains(b) && y->second.members.contains(a) && now >= x->second.partySeen &&
				now - x->second.partySeen <= 5000 && now >= y->second.partySeen && now - y->second.partySeen <= 5000;
		}

		bool Blocked(uint64_t a, uint64_t b) const
		{
			auto x = users.find(a), y = users.find(b);
			return (x != users.end() && x->second.playerLists.Contains(1, b)) || (y != users.end() && y->second.playerLists.Contains(1, a));
		}

		// Reuse authenticated social leases for the local native matchmaking
		// service. Party membership requires matching, live reports on both ends.

		// TU43 opts into complete, fresh party admission. A session heartbeat or
		// chat poll cannot keep an old party roster valid. TU19 callers retain
		// their existing authorization behavior until explicitly opting in.
		bool MatchmakingPartyReady(uint64_t owner, const std::set<uint64_t>& members, const std::string& party, uint64_t now) const
		{
			const auto it = clients.find(owner);
			if (it == clients.end()) return false;
			const auto& c = it->second;
			if (c.party != party || now < c.partySeen || now - c.partySeen > 5000) return false;
			if (c.party.empty()) return c.host == 0 && members == std::set<uint64_t>{ owner };
			if (c.host != owner || c.members != members || !members.contains(owner) || now < c.partySeen || now - c.partySeen > 5000)
				return false;
			for (auto id : members)
			{
				const auto peer = clients.find(id);
				if (peer == clients.end() || peer->second.host != owner || peer->second.party != c.party ||
					peer->second.members != members || now < peer->second.partySeen || now - peer->second.partySeen > 5000)
					return false;
			}
			return true;
		}

		Json TournamentMembers(uint64_t owner, const std::set<uint64_t>& members, const std::string& party, uint64_t now) const
		{
			if (members.empty() || members.size() > 2 || !MatchmakingPartyReady(owner, members, party, now)) return Json::array();
			Json rows = Json::array();
			for (auto id : members)
			{
				if (!users.contains(id)) return Json::array();
				for (auto other : members)
					if (Blocked(id, other)) return Json::array();
				rows.push_back({ { "id", std::to_string(id) }, { "name", users.at(id).name } });
			}
			return rows;
		}

		uint64_t Revision() const
		{
			return revision;
		}
#include "network/social/backend/discord.inl"
		bool Dirty() const
		{
			return dirty;
		}

		void Saved()
		{
			dirty = false;
		}

		Json Save() const
		{
			Json rows = Json::array();
			for (const auto& [id, u] : users)
			{
				Json f = Json::array(), r = Json::array();
				for (auto x : u.friends)
					f.push_back(std::to_string(x));
				for (auto x : u.requests)
					r.push_back(std::to_string(x));
				rows.push_back({ { "xuid", std::to_string(id) }, { "name", u.name }, { "key", u.key }, { "friends", f }, { "requests", r },
					{ "availability", u.availability }, { "player_lists", u.playerLists.Save() } });
			}
			return { { "version", 2 }, { "users", rows } };
		}

		bool Load(const Json& j)
		{
			// Validate into a temporary backend. A corrupt store must not erase the
			// current relationships or turn an existing account into a new claim.
			try
			{
				if ((j.at("version") != 1 && j.at("version") != 2) || !j.at("users").is_array() || j["users"].size() > 1024) return false;
				std::map<uint64_t, User> next;
				for (const auto& row : j["users"])
				{
					const auto id = Id(row, "xuid");
					User u;
					u.name = row.at("name");
					u.key = row.at("key");
					u.availability = row.value("availability", 2);
					if (u.availability < 1 || u.availability > 4) return false;
					if (!id || next.contains(id) || !chat::Text(u.name, 35) || !chat::Hex(u.key, 32)) return false;
					if (j.at("version") == 2 && (!row.contains("player_lists") || !u.playerLists.Load(row["player_lists"], id)))
						return false;
					for (auto name : { "friends", "requests" })
					{
						if (!row.at(name).is_array() || row[name].size() > 100) return false;
						for (const auto& v : row[name])
						{
							auto target = Id(Json{ { "id", v } }, "id");
							if (!target || target == id ||
								!(std::string_view(name) == "friends" ? u.friends : u.requests).insert(target).second)
								return false;
						}
					}
					next.emplace(id, std::move(u));
				}
				for (const auto& [id, u] : next)
				{
					for (auto f : u.friends)
						if (!next.contains(f) || !next.at(f).friends.contains(id)) return false;
					for (auto r : u.requests)
						if (!next.contains(r) || u.friends.contains(r)) return false;
				}
				users = std::move(next);
				clients.clear();
				parties.clear();
				discordInvites.clear();
				discordGrants.clear();
				game = {};
				dirty = false;
				++revision;
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		bool Befriend(uint64_t a, uint64_t b)
		{
			if (!a || !b || a == b || !users.contains(a) || !users.contains(b) || Blocked(a, b)) return false;
			auto& x = users[a];
			auto& y = users[b];
			if (!Friends(a, b) && (x.friends.size() >= 100 || y.friends.size() >= 100)) return false;
			x.friends.insert(b);
			y.friends.insert(a);
			x.requests.erase(b);
			y.requests.erase(a);
			dirty = true;
			++revision;
			return true;
		}

		void Publish(Session s, uint64_t now)
		{
			game = ValidSession(s) ? std::move(s) : Session{};
			gameAt = now;
			Expire(now);
		}

		Json Status(uint64_t now)
		{
			Expire(now);
			Json rows = Json::array();
			for (const auto& [id, u] : users)
			{
				auto row = Presence(id);
				row["name"] = u.name;
				row["friends"] = u.friends.size();
				rows.push_back(row);
			}
			return rows;
		}

		Answer Handle(std::string_view path, const Json& j, uint32_t ip, uint64_t now, std::string fresh = {}, uint64_t utc = 0,
			bool tu43 = false, const std::function<bool(uint64_t, int)>& gameAdmission = {})
		{
			try
			{
				if (!j.is_object() || j.value("version", 0) != 1) return Error(400, "unsupported version");
				const auto id = Id(j, "xuid");
				if (!id) return Error(400, "invalid identity");
				Expire(now);
				if (path == "/v1/social/open")
				{
					const auto name = j.value("name", std::string{}), key = j.value("key", std::string{}),
							   nonce = j.value("nonce", std::string{});
					if (!chat::Text(name, 35) || !chat::Hex(key, 32) || !chat::Hex(nonce, 32) || !chat::Hex(fresh, 32))
						return Error(400, "invalid registration");
					auto u = users.find(id);
					if (u != users.end() && u->second.key != key) return Error(403, "identity credential mismatch");
					auto c = clients.find(id);
					if (c != clients.end() && (c->second.nonce != nonce || c->second.ip != ip))
						return Error(409, "identity already online");
					if (u == users.end())
					{
						if (users.size() >= 1024) return Error(503, "user limit");
						users.emplace(id, User{ name, key, {}, {} });
						dirty = true;
						++revision;
					}
					else if (u->second.name != name)
					{
						u->second.name = name;
						dirty = true;
						++revision;
					}
					if (c == clients.end())
					{
						Client v;
						v.ip = ip;
						v.lease = std::move(fresh);
						v.nonce = nonce;
						c = clients.emplace(id, std::move(v)).first;
					}
					c->second.seen = now;
					return { 200, { { "lease", c->second.lease }, { "revision", revision } } };
				}
				auto it = clients.find(id);
				if (it == clients.end() || it->second.ip != ip || j.value("lease", std::string{}) != it->second.lease)
					return Error(401, "expired or invalid lease");
				auto& c = it->second;
				c.seen = now;
				if (path == "/v1/social/userlist")
				{
					const auto name = j.value("list", std::string{}), op = j.value("op", std::string{});
					const int type = UserLists::Type(name);
					if (type < 0) return Error(400, "unknown user list");
					auto& lists = users.at(id).playerLists;
					if (op == "fetch")
					{
						if (!j.value("page", std::string{}).empty()) return Error(400, "invalid page token");
						auto rows = lists.Rows(type);
						for (auto& row : rows)
							if (auto u = users.find(Id(row, "xuid")); u != users.end()) row["name"] = u->second.name;
						return { 200, { { "list", name }, { "users", rows }, { "page", "" }, { "revision", revision } } };
					}
					const auto request = Id(j, "request");
					if (!request) return Error(400, "invalid request id");
					if (op == "create") return { 200, { { "accepted", true }, { "request", std::to_string(request) } } };
					if ((op != "add" && op != "remove") || !j.contains("users") || !j["users"].is_array() || j["users"].empty() ||
						j["users"].size() > 200 || !utc || utc > UINT32_MAX)
						return Error(400, "invalid list mutation");
					std::vector<UserLists::Entry> entries;
					std::set<uint64_t> ids;
					for (const auto& v : j["users"])
					{
						auto target = Id(Json{ { "id", v } }, "id");
						if (!target || target == id || !users.contains(target) || !ids.insert(target).second)
							return Error(400, "invalid list member");
						entries.push_back({ target, utc, users.at(target).name });
					}
					const auto token = c.nonce + ":" + std::to_string(request);
					const auto signature = Json{ { "list", name }, { "op", op }, { "users", j["users"] } }.dump();
					// Check receipts before relationship changes: replaying an old
					// block after unblock must not remove a new friendship.
					for (const auto& r : lists.receipts)
						if (r.token == token)
						{
							if (r.body != signature) return Error(409, "request reused");
							return { 200, { { "accepted", true }, { "request", std::to_string(request) } } };
						}
					const auto status = lists.Mutate(type, op == "remove", entries, token, signature);
					if (status != 200) return Error(status, "user list capacity or request conflict");
					if (type == 1 && op == "add")
						for (const auto& e : entries)
						{
							users[id].friends.erase(e.id);
							users[id].requests.erase(e.id);
							users[e.id].friends.erase(id);
							users[e.id].requests.erase(id);
							for (auto& [who, client] : clients)
								if (who == id || who == e.id) client.subscriptions.erase(who == id ? e.id : id);
						}
					dirty = true;
					++revision;
					Expire(now);
					return { 200, { { "accepted", true }, { "request", std::to_string(request) } } };
				}
				if (path == "/v1/social/close")
				{
					clients.erase(it);
					parties.erase(id);
					return { 200, { { "closed", true } } };
				}
				if (path == "/v1/social/search")
				{
					// Local UNO directory, not a retail platform-account search.
					// Only disclose public identity fields; credentials, leases and
					// private session data never leave this endpoint.
					const auto name = j.value("username", std::string{});
					if (!chat::Text(name, 35) || j.value("provider", std::string{}) != "uno" || j.value("substring", false))
						return Error(400, "invalid username search");
					auto fold = [](std::string text)
					{
						// Locale-independent ASCII case folding preserves UTF-8.
						for (auto& ch : text)
							if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
						return text;
					};
					const auto key = fold(name);
					Json rows = Json::array();
					for (const auto& [target, u] : users)
						if (fold(u.name) == key)
						{
							if (!rows.empty()) return Error(409, "ambiguous username");
							rows.push_back({ { "xuid", std::to_string(target) }, { "name", u.name } });
						}
					// TU43 formats an undiscriminated provider name as name#0.
					// Accept that exact native presentation, or an explicit XUID,
					// while keeping ambiguous display names an error.
					if (tu43 && rows.empty())
					{
						const auto numeric = Id(Json{ { "id", name } }, "id");
						if (numeric && users.contains(numeric))
							rows.push_back({ { "xuid", name }, { "name", users.at(numeric).name } });
						else if (key.ends_with("#0"))
							for (const auto& [target, u] : users)
								if (fold(u.name) == key.substr(0, key.size() - 2))
								{
									if (!rows.empty()) return Error(409, "ambiguous username");
									rows.push_back({ { "xuid", std::to_string(target) }, { "name", u.name } });
								}
					}
					return { 200, { { "users", rows } } };
				}
				if (path == "/v1/social/availability")
				{
					const int status = j.value("availability", 0);
					const auto request = Id(j, "request");
					if (status < 1 || status > 4 || !request) return Error(400, "invalid availability");
					auto& u = users.at(id);
					if (u.availability != status)
					{
						u.availability = status;
						dirty = true;
						++revision;
					}
					return { 200, { { "accepted", true }, { "request", std::to_string(request) }, { "availability", status } } };
				}
				if (path == "/v1/social/sync")
				{
					const auto activity = j.value("activity", std::string{}), party = j.value("party", std::string{}),
							   gameId = j.value("game", std::string{});
					if (activity != "menus" && activity != "party" && activity != "searching" && activity != "ingame")
						return Error(400, "invalid activity");
					if ((!party.empty() && !chat::Hex(party, 16)) || (!gameId.empty() && !chat::Hex(gameId, 16)))
						return Error(400, "invalid session");
					std::set<uint64_t> members;
					if (!j.contains("members") || !j["members"].is_array() || j["members"].size() > 200)
						return Error(400, "invalid roster");
					for (auto m : j["members"])
					{
						auto uid = Id(Json{ { "id", m } }, "id");
						if (!uid || !members.insert(uid).second) return Error(400, "invalid member");
					}
					Session s;
					const auto host = Id(j, "host");
					if (host == id && !party.empty())
					{
						s.id = party;
						s.bytes = j.value("bytes", std::string{});
						s.endpoint = j.value("endpoint", std::string{});
						s.capacity = 20;
						s.owner = id;
						s.members = members;
						if (!ValidSession(s) || !members.contains(id)) return Error(400, "invalid owned party");
					}
					const auto ack = tu43 && j.value("event_stream", std::string{}) != c.lease ? 0 : Id(j, "ack");
					if (ack > c.sequence) return Error(400, "invalid event acknowledgement");
					// Worker keepalives can acknowledge events without asserting
					// that a main-thread roster captured before loading is fresh.
					if (j.value("snapshot_fresh", true))
					{
						c.activity = activity;
						c.party = party;
						c.host = host;
						c.members = std::move(members);
						c.game = gameId;
						c.partySeen = now;
						if (!s.id.empty())
							parties[id] = std::move(s);
						else
							parties.erase(id);
					}
					std::erase_if(c.inbox,
						[&](const Event& e)
						{
							return e.id <= ack;
						});
					Json presence = Json::array(), events = Json::array();
					for (auto f : tu43 ? users.at(id).friends : c.subscriptions)
						if (Friends(id, f)) presence.push_back(Presence(f));
					for (const auto& e : c.inbox)
					{
						events.push_back({ { "id", std::to_string(e.id) }, { "from", std::to_string(e.from) },
							{ "name", users.at(e.from).name }, { "bytes", e.bytes }, { "resolved", e.resolved } });
						if (events.size() == 16) break;
					}
					Json result{ { "revision", revision }, { "presence", presence }, { "events", events },
						{ "availability", users.at(id).availability } };
					if (tu43) result["event_stream"] = c.lease;
					return { 200, std::move(result) };
				}
				if (path == "/v1/social/friends") return { 200, { { "revision", revision }, { "friends", FriendsList(id) } } };
				if (path == "/v1/social/requests")
				{
					const bool outgoing = j.value("outgoing", false);
					Json rows = Json::array();
					for (const auto& [target, u] : users)
						if (outgoing ? u.requests.contains(id) : users.at(id).requests.contains(target))
						{
							auto row = Presence(target);
							row["name"] = u.name;
							rows.push_back(std::move(row));
						}
					return { 200, { { "friends", rows }, { "revision", revision } } };
				}
				if (path == "/v1/social/subscribe" || path == "/v1/social/unsubscribe")
				{
					if (!j.contains("users") || !j["users"].is_array() || j["users"].size() > 100)
						return Error(400, "invalid subscriptions");
					std::set<uint64_t> ids;
					for (auto uid : j["users"])
					{
						auto x = Id(Json{ { "id", uid } }, "id");
						if (!Friends(id, x)) return Error(403, "not friends");
						ids.insert(x);
					}
					Json presence = Json::array();
					for (auto x : ids)
					{
						if (path.ends_with("/unsubscribe"))
							c.subscriptions.erase(x);
						else
						{
							c.subscriptions.insert(x);
							presence.push_back(Presence(x));
						}
					}
					return { 200, { { "presence", presence } } };
				}
				const auto target = Id(j, "target");
				const bool mutation = path == "/v1/social/request" || path == "/v1/social/remove";
				const auto mutationId = Id(j, "request");
				if (mutation)
				{
					if (!mutationId) return Error(400, "invalid mutation request");
					for (const auto& receipt : c.mutations)
						if (receipt.request == mutationId)
							return receipt.target == target && receipt.path == path ? receipt.answer : Error(409, "request reused");
					if (mutationId <= c.highMutation) return Error(409, "stale mutation request");
				}
				auto acceptedMutation = [&]()
				{
					Answer answer{ 200, { { "accepted", true }, { "request", std::to_string(mutationId) } } };
					c.highMutation = mutationId;
					c.mutations.push_back({ mutationId, target, std::string(path), answer });
					if (c.mutations.size() > 64) c.mutations.pop_front();
					return answer;
				};
				if (path == "/v1/social/request")
				{
					if (!target || target == id || !users.contains(target)) return Error(404, "unknown target");
					if (Blocked(id, target)) return Error(403, "relationship blocked");
					auto& u = users[id];
					auto& t = users[target];
					if (Friends(id, target)) return tu43 ? acceptedMutation() : Error(409, "already friends");
					if (u.requests.contains(target))
					{
						if (!Befriend(id, target)) return Error(409, "friend limit");
					}
					else
					{
						if (t.requests.size() >= 100) return Error(429, "request limit");
						t.requests.insert(id);
						dirty = true;
						++revision;
					}
					return acceptedMutation();
				}
				if (path == "/v1/social/remove")
				{
					if (!users.contains(target)) return Error(404, "unknown target");
					bool existed = users[id].friends.erase(target) > 0;
					existed = users[id].requests.erase(target) > 0 || existed;
					existed = users[target].requests.erase(id) > 0 || existed;
					if (!existed) return Error(404, "relationship not found");
					users[target].friends.erase(id);
					dirty = true;
					++revision;
					Expire(now);
					return acceptedMutation();
				}
				if (path == "/v1/social/resolve")
				{
					if (!JoinAllowed(id, target, now)) return Error(403, "join not authorized");
					const auto* s = Resolve(target);
					if (!s) return Error(409, "target is not joinable");
					if (s == &game && gameAdmission && !gameAdmission(id, s->playlist)) return Error(403, "playlist access denied");
					if (SessionBlocked(id, s)) return Error(403, "session contains blocked player");
					return { 200,
						{ { "session", s->id }, { "bytes", s->bytes }, { "endpoint", s->endpoint }, { "playlist", s->playlist },
							{ "map", s->map }, { "mode", s->mode }, { "owner", std::to_string(s->owner) },
							{ "kind", s == &game ? "game" : "party" } } };
				}
				if (path == "/v1/social/message")
				{
					const auto request = Id(j, "request");
					const auto bytes = j.value("bytes", std::string{});
					if (!request || !ValidInviteEnvelope(bytes, tu43)) return Error(400, "invalid invitation message");
					const auto op = bytes.substr(10, 2);
					if (op != "00" && op != "01" && op != "02") return Error(400, "invalid invitation opcode");
					for (const auto& sent : c.sent)
						if (sent.request == request)
							return sent.target == target && sent.bytes == bytes ? sent.answer : Error(409, "request reused");
					if (request <= c.highRequest) return Error(409, "stale request");
					if (!JoinAllowed(id, target, now)) return Error(403, "join not authorized");
					auto receiver = clients.find(target);
					if (receiver == clients.end()) return Error(409, "target offline");
					if (c.lastSend && now >= c.lastSend && now - c.lastSend < 200) return Error(429, "message rate limit");
					if (receiver->second.inbox.size() >= 64) return Error(429, "recipient queue full");
					// Native requests are six bytes (INV: NUL, opcode). Successful
					// responses and invites retain native serialization; admission
					// still verifies their session against current backend state.
					if (op == "00" && (bytes.size() != 12 || !Resolve(target))) return Error(409, "target not joinable");
					if (op == "02" && !Resolve(id)) return Error(409, "sender not joinable");
					if (gameAdmission &&
						((op == "00" && Resolve(target) == &game && !gameAdmission(id, game.playlist)) ||
							(op != "00" && Resolve(id) == &game && !gameAdmission(target, game.playlist))))
						return Error(403, "playlist access denied");
					if ((op == "00" && SessionBlocked(id, Resolve(target))) || (op != "00" && SessionBlocked(target, Resolve(id))))
						return Error(403, "session contains blocked player");
					if (op == "01")
					{
						auto pending = receiver->second.pendingJoins.find(id);
						if (pending == receiver->second.pendingJoins.end() || now >= pending->second)
							return Error(409, "unsolicited join response");
						const auto* session = Resolve(id);
						if (bytes.substr(12, 2) == "01")
						{
							// SessionInfo::Serialize writes the security KEY before
							// its ID (12C0730), unlike the in-memory 108-byte layout.
							if (!session || bytes.size() != (tu43 ? 278 : 276) || bytes.substr(14, 4) != (tu43 ? "8200" : "8100") ||
								bytes.substr(tu43 ? 60 : 58, 2) != "01" ||
								bytes.substr(tu43 ? 62 : 60, 168) != session->bytes.substr(0, 168) ||
								bytes.substr(tu43 ? 230 : 228, 48) != session->bytes.substr(184, 32) + session->bytes.substr(168, 16))
								return Error(409, "stale or invalid join session");
						}
						else if (bytes.size() != 16 || bytes.substr(12, 2) != "00")
							return Error(400, "malformed join response");
						receiver->second.pendingJoins.erase(pending);
					}
					// F5A1C0 writes name (<=35 bytes + NUL), timestamp32, UNO64.
					if (op == "02")
					{
						size_t end = 12;
						while (end + 2 <= bytes.size() && bytes.substr(end, 2) != "00")
							end += 2;
						if (end > 82 || end + 26 != bytes.size()) return Error(400, "invalid invite size");
						uint64_t embedded = 0;
						for (size_t i = 0; i < 8; ++i)
						{
							unsigned byte = 0;
							auto text = bytes.substr(end + 10 + 2 * i, 2);
							auto parse = std::from_chars(text.data(), text.data() + 2, byte, 16);
							if (parse.ec != std::errc{}) return Error(400, "invalid invite identity");
							embedded |= uint64_t(byte) << (8 * i);
						}
						if (embedded != id) return Error(403, "invalid invite sender");
					}
					if (op == "00" && Resolve(target) == &game)
					{
						// The dedicated service is the owner of this session. A
						// playing peer's retail invitation handler cannot publish
						// it while using our direct transport. Supply the genuine
						// native response here; the receiver retains native HSM,
						// prerequisite checks, atomic admission and game loading.
						if (c.inbox.size() >= 64) return Error(429, "sender queue full");
						Json resolved = { { "session", game.id }, { "bytes", game.bytes }, { "endpoint", game.endpoint },
							{ "playlist", game.playlist }, { "map", game.map }, { "mode", game.mode }, { "kind", "game" } };
						c.inbox.push_back({ ++c.sequence, target, now + 30000, GameJoinReply(game, tu43), std::move(resolved) });
					}
					else
					{
						if (op == "00") c.pendingJoins[target] = now + 30000;
						auto& r = receiver->second;
						r.inbox.push_back({ ++r.sequence, id, now + 30000, bytes, {} });
					}
					c.lastSend = now;
					c.highRequest = request;
					Answer answer{ 200, { { "accepted", true }, { "request", std::to_string(request) } } };
					c.sent.push_back({ request, target, bytes, answer });
					if (c.sent.size() > 64) c.sent.pop_front();
					return answer;
				}
				return Error(404, "unknown social operation");
			}
			catch (...)
			{
				return Error(400, "malformed social request");
			}
		}
	};
}
