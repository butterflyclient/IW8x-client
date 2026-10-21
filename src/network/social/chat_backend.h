#pragma once
#include "thirdparty/json.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
// Local service implementation of IW8 channel-chat semantics. No LUI code and
// no engine pointers live here. Callers serialize access and supply monotonic
// time, unpredictable leases, and the dedicated server's authoritative roster.

namespace chat
{
	using Json = nlohmann::json;

	struct Player
	{
		uint64_t xuid = 0;
		std::string name;
		int team = 0;
	};

	struct Answer
	{
		unsigned status;
		Json body;
	};

	inline bool Text(std::string_view s, size_t max)
	{
		if (s.empty() || s.size() > max) return false;
		bool visible = false;
		for (unsigned char c : s)
		{
			if (c < 32 || c == 127) return false;
			if (c != ' ') visible = true;
		}
		// JSON parser/serializer checks UTF-8; refuse invalid input even for tests
		// that call this core without passing through the JSON wire parser.
		try
		{
			Json(s).dump();
		}
		catch (...)
		{
			return false;
		}
		return visible;
	}

	inline uint64_t Id(const Json& j, const char* key)
	{
		if (!j.contains(key) || !j[key].is_string()) return 0;
		const auto s = j[key].get<std::string>();
		uint64_t n = 0;
		if (s.empty() || s.size() > 20) return 0;
		for (char c : s)
		{
			if (c < '0' || c > '9' || n > (UINT64_MAX - uint64_t(c - '0')) / 10) return 0;
			n = n * 10 + (c - '0');
		}
		return n;
	}

	inline bool Hex(std::string_view s, size_t size)
	{
		return s.size() == size &&
			std::all_of(s.begin(), s.end(),
				[](char c)
				{
					return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
				});
	}

	class Backend
	{
		struct Event
		{
			uint64_t sequence, room, sender;
			unsigned flags;
			std::string name, text;
			int type = 0;
		};

		struct Client
		{
			uint64_t xuid = 0, seen = 0, lastSend = 0, sequence = 0, lastRequest = 0, scopeSeen = 0;
			uint32_t ip = 0;
			std::string name, nonce, lease, party, game, principalLease;
			uint64_t host = 0;
			std::set<uint64_t> partyMembers;
			std::deque<Event> inbox;

			struct Sent
			{
				uint64_t request, room;
				unsigned flags;
				std::string text;
				Answer answer;
				uint64_t target = 0;
			};

			std::deque<Sent> sent;
		};

		struct Room
		{
			uint64_t id = 0;
			int type = 0;
			std::string key;
			std::set<uint64_t> members;
		};

		std::map<uint64_t, Client> clients;
		std::map<std::string, Room> rooms;
		std::map<uint64_t, Player> gamePlayers;
		std::string game;
		uint64_t rosterAt = 0, nextRoom;

		static Answer Error(unsigned status, const char* reason)
		{
			return { status, Json{ { "error", reason } } };
		}

		void Expire(uint64_t now)
		{
			// The game thread captures a roster before acquiring the service lock.
			// HTTP can renew a lease meanwhile. Unsigned subtraction of that newer
			// timestamp used to expire live clients and destroy their chat channels.
			std::erase_if(clients,
				[&](const auto& p)
				{
					return (now >= p.second.seen && now - p.second.seen > 15000) || (live && !live(p.first, p.second.principalLease, now));
				});
			if (authorize)
				for (auto& [id, c] : clients)
					if (!c.scopeSeen || (now >= c.scopeSeen && now - c.scopeSeen > 5000))
					{
						c.party.clear();
						c.host = 0;
						c.partyMembers.clear();
						c.game.clear();
					}
			if (now >= rosterAt && now - rosterAt > 3000)
			{
				game.clear();
				gamePlayers.clear();
			}
		}

		void Reconcile()
		{
			for (auto& [k, r] : rooms)
				r.members.clear();
			auto add = [&](std::string key, int type, uint64_t uid)
			{
				auto& r = rooms[key];
				if (!r.id)
				{
					r.id = ++nextRoom;
					r.type = type;
					r.key = key;
				}
				r.members.insert(uid);
			};
			for (auto& [uid, c] : clients)
			{
				if (global) add("global", 6, uid);
				auto h = clients.find(c.host);
				// A guest cannot create party membership by claiming a host XUID:
				// both native snapshots must agree and the host must list it.
				if (h != clients.end() && h->second.host == c.host && !c.party.empty() && h->second.party == c.party &&
					h->second.partyMembers.contains(uid) && c.partyMembers.contains(c.host))
					add("p:" + std::to_string(c.host) + ":" + c.party, 3, uid);
				auto p = gamePlayers.find(uid);
				if (!game.empty() && c.game == game && p != gamePlayers.end())
				{
					add("g:" + game, 4, uid);
					if (p->second.team > 0 && p->second.team < 201) add("t:" + game + ":" + std::to_string(p->second.team), 5, uid);
				}
			}
			std::erase_if(rooms,
				[](const auto& p)
				{
					return p.second.members.empty();
				});
			// Do not deliver queued messages after a member leaves/changes team.
			for (auto& [uid, c] : clients)
				std::erase_if(c.inbox,
					[&](const Event& e)
					{
						if (blocked && blocked(uid, e.sender)) return true;
						if (e.type == 7) return !clients.contains(e.sender);
						for (const auto& [key, r] : rooms)
							if (r.id == e.room) return !r.members.contains(uid);
						return true;
					});
		}

		std::string Name(uint64_t uid, int type) const
		{
			if (type == 4 || type == 5)
			{
				auto p = gamePlayers.find(uid);
				if (p != gamePlayers.end()) return p->second.name;
			}
			auto p = clients.find(uid);
			return p == clients.end() ? std::string{} : p->second.name;
		}

	public:
		std::function<bool(uint64_t, uint64_t)> blocked;
		// Optional authenticated policy: the TU19 runtime supplies the existing
		// social account/lease service. Legacy users of this core retain behavior.
		std::function<Json(const Json&, uint32_t, uint64_t)> authorize;
		std::function<bool(uint64_t, const std::string&, uint64_t)> live;
		bool global = false;

		explicit Backend(uint64_t seed = 0x100000000ull) : nextRoom(seed)
		{
		}

		void Publish(std::string key, std::vector<Player> players, uint64_t now)
		{
			game = std::move(key);
			gamePlayers.clear();
			rosterAt = now;
			for (auto& p : players)
				if (p.xuid && Text(p.name, 35)) gamePlayers.emplace(p.xuid, std::move(p));
			Expire(now);
			Reconcile();
		}

		Answer Handle(std::string_view path, const Json& j, uint32_t ip, uint64_t now, std::string newLease = {})
		{
			Expire(now);
			Reconcile();
			if (!j.is_object() || j.value("version", 0) != 1) return Error(400, "protocol");
			const auto uid = Id(j, "xuid");
			if (!uid) return Error(400, "identity");
			Json principal;
			if (authorize)
			{
				principal = authorize(j, ip, now);
				if (!principal.is_object() || Id(principal, "id") != uid || !Text(principal.value("name", std::string{}), 35))
					return Error(401, "authentication required");
			}
			if (path == "/v1/chat/open")
			{
				const auto name = authorize ? principal.at("name").get<std::string>() : j.value("name", std::string{}),
						   nonce = j.value("nonce", std::string{});
				if (!Text(name, 35) || !Hex(nonce, 32) || !Hex(newLease, 32)) return Error(400, "identity");
				auto old = clients.find(uid);
				// A newly authenticated social session owns a new chat stream.
				if (old != clients.end() && authorize && old->second.principalLease != j.value("social_lease", std::string{}))
				{
					clients.erase(old);
					old = clients.end();
					Reconcile();
				}
				if (old != clients.end())
				{
					if (old->second.ip != ip || old->second.nonce != nonce) return Error(409, "identity already connected");
					old->second.seen = now;
					return { 200, Json{ { "lease", old->second.lease } } };
				}
				if (clients.size() >= (global ? 200u : 256u)) return Error(503, "capacity");
				Client c;
				c.xuid = uid;
				c.name = name;
				c.ip = ip;
				c.nonce = nonce;
				c.lease = newLease;
				c.seen = now;
				if (authorize) c.principalLease = j.at("social_lease").get<std::string>();
				clients.emplace(uid, std::move(c));
				return { 200, Json{ { "lease", newLease } } };
			}
			auto it = clients.find(uid);
			if (it == clients.end() || it->second.ip != ip || j.value("lease", std::string{}) != it->second.lease)
				return Error(401, "session expired");
			auto& c = it->second;
			if (authorize)
			{
				if (c.principalLease != j.value("social_lease", std::string{})) return Error(401, "session changed");
				c.name = principal.at("name").get<std::string>();
			}
			if (path == "/v1/chat/close")
			{
				clients.erase(it);
				Reconcile();
				return { 200, Json::object() };
			}
			if (path == "/v1/chat/sync")
			{
				const auto party = j.value("party", std::string{}), g = j.value("game", std::string{});
				const auto host = Id(j, "host");
				if ((!party.empty() && !Hex(party, 16)) || (!g.empty() && !Hex(g, 16)) || !j.contains("members") ||
					!j["members"].is_array() || j["members"].size() > 200)
					return Error(400, "membership");
				std::set<uint64_t> members;
				for (const auto& m : j["members"])
				{
					auto id = Id(Json{ { "id", m } }, "id");
					if (!id || !members.insert(id).second) return Error(400, "membership");
				}
				if (!party.empty() && (!host || !members.contains(uid) || !members.contains(host))) return Error(400, "party");
				const auto ack = Id(j, "ack");
				if (ack > c.sequence) return Error(400, "ack");
				c.party = party;
				c.host = host;
				c.partyMembers = std::move(members);
				c.game = g;
				c.seen = now;
				if (authorize)
				{
					if (j.value("snapshot_fresh", false))
						c.scopeSeen = now;
					else
					{
						c.scopeSeen = 0;
						c.party.clear();
						c.host = 0;
						c.partyMembers.clear();
						c.game.clear();
					}
				}
				if (!authorize && j.contains("name") && Text(j.value("name", std::string{}), 35)) c.name = j["name"].get<std::string>();
				while (!c.inbox.empty() && c.inbox.front().sequence <= ack)
					c.inbox.pop_front();
				Reconcile();
				Json channels = Json::array(), events = Json::array();
				for (const auto& [key, r] : rooms)
					if (r.members.contains(uid))
					{
						Json roster = Json::array();
						for (auto id : r.members)
							roster.push_back(Json{ { "xuid", std::to_string(id) }, { "name", Name(id, r.type) } });
						channels.push_back(Json{ { "id", std::to_string(r.id) }, { "type", r.type }, { "members", std::move(roster) } });
					}
				for (const auto& e : c.inbox)
				{
					if (blocked && blocked(uid, e.sender)) continue;
					events.push_back(Json{ { "seq", std::to_string(e.sequence) }, { "channel", std::to_string(e.room) },
						{ "sender", std::to_string(e.sender) }, { "name", e.name }, { "flags", e.flags }, { "text", e.text },
						{ "type", e.type } });
					if (events.size() == 12) break;
				}
				return { 200,
					Json{ { "channels", std::move(channels) }, { "events", std::move(events) }, { "communications", bool(authorize) } } };
			}
			if (path != "/v1/chat/send") return Error(404, "endpoint");
			const auto request = Id(j, "request"), channel = Id(j, "channel"), target = Id(j, "target");
			const auto text = j.value("text", std::string{});
			const unsigned flags = j.value("flags", 0u);
			if (!request || (!channel && !target) || (channel && target) || !Text(text, 499) || (flags & ~0x7FFu))
				return Error(400, "message");
			for (const auto& old : c.sent)
				if (old.request == request)
				{
					if (old.room != channel || old.target != target || old.text != text || old.flags != flags)
						return Error(409, "request reused");
					return old.answer;
				}
			if (request <= c.lastRequest) return Error(409, "stale request");
			if (target)
			{
				// Private delivery requires authenticated, live endpoints. Never
				// convert a failed whisper into a room/global broadcast.
				if (!authorize || target == uid || !clients.contains(target)) return Error(404, "recipient unavailable");
				if (blocked && (blocked(target, uid) || blocked(uid, target))) return Error(403, "communications denied");
				if (c.lastSend && now >= c.lastSend && now - c.lastSend < 200) return Error(429, "throttled");
				auto& receiver = clients.at(target);
				if (receiver.inbox.size() >= 128) receiver.inbox.pop_front();
				receiver.inbox.push_back({ ++receiver.sequence, uid, uid, flags, c.name, text, 7 });
				c.lastSend = now;
				c.seen = now;
				c.lastRequest = request;
				Answer reply{ 200, Json{ { "request", std::to_string(request) }, { "accepted", true } } };
				c.sent.push_back({ request, 0, flags, text, reply, target });
				if (c.sent.size() > 128) c.sent.pop_front();
				return reply;
			}
			Room* room = nullptr;
			for (auto& [key, r] : rooms)
				if (r.id == channel)
				{
					room = &r;
					break;
				}
			if (!room || !room->members.contains(uid)) return Error(403, "not a channel member");
			if (c.lastSend && now - c.lastSend < 200) return Error(429, "throttled");
			// Refuse, rather than silently dropping, when any receiver is behind.
			if (!global)
				for (auto member : room->members)
					if (member != uid && !(blocked && blocked(member, uid)) && clients.at(member).inbox.size() >= 128)
						return Error(503, "receiver backlog");
			const auto name = Name(uid, room->type);
			for (auto member : room->members)
				if (member != uid && !(blocked && blocked(member, uid)))
				{
					auto& receiver = clients.at(member);
					// A loading/slow client must not prevent everyone else from chatting.
					if (global && receiver.inbox.size() >= 128) receiver.inbox.pop_front();
					receiver.inbox.push_back({ ++receiver.sequence, room->id, uid, flags, name, text });
				}
			c.lastSend = now;
			c.seen = now;
			c.lastRequest = request;
			Answer reply{ 200, Json{ { "request", std::to_string(request) }, { "accepted", true } } };
			c.sent.push_back({ request, channel, flags, text, reply });
			if (c.sent.size() > 128) c.sent.pop_front();
			return reply;
		}
	};
}
