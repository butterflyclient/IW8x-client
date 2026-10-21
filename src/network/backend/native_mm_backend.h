#pragma once
#include "network/match_assignment.h"
#include "network/social/chat_backend.h"

#include <functional>
#include <cstring>
// Service145 semantics at the verified Replay DWAsyncMatchMaking boundary.
// Authentication and allocation are supplied by the dedicated host; this core
// never accepts session addresses/security keys from a searching client.

namespace native_mm
{
	using chat::Json;
	using chat::Answer;

	inline uint64_t Number(const Json& j)
	{
		if (j.is_number_unsigned()) return j.get<uint64_t>();
		if (j.is_number_integer())
		{
			const auto n = j.get<int64_t>();
			return n > 0 ? uint64_t(n) : 0;
		}
		return chat::Id(Json{ { "id", j } }, "id");
	}

	inline std::string Base64(const uint8_t* data, size_t size)
	{
		constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		unsigned value = 0, bits = 0;
		for (size_t i = 0; i < size; ++i)
		{
			value = (value << 8) | data[i];
			bits += 8;
			while (bits >= 6)
			{
				bits -= 6;
				out += alphabet[(value >> bits) & 63];
			}
		}
		if (bits) out += alphabet[(value << (6 - bits)) & 63];
		while (out.size() % 4)
			out += '=';
		return out;
	}

	struct Principal
	{
		uint64_t id = 0;
		std::string name;
		std::set<uint64_t> members;
		std::string party;
	};

	class Backend
	{
		struct User
		{
			uint64_t token = 0, seen = 0;
			Json info;
		};

		struct Search
		{
			uint64_t owner = 0, id = 0, request = 0, seen = 0;
			Json document;
			match_queue::Packet query;
			bool delivered = false;
			Principal principal;
		};

		std::map<uint64_t, User> users;
		std::map<uint64_t, Search> searches;
		uint64_t sequence;

		static Answer Error(unsigned status, const char* text)
		{
			return { status, { { "error", text } } };
		}

	public:
		using Allocator = std::function<matchmaking::Assignment(const match_queue::Packet&)>;
		using Admission = std::function<bool(const Principal&, uint64_t)>;
		using PlaylistPolicy = std::function<bool(const Principal&, int)>;

		explicit Backend(uint64_t seed = 1) : sequence(seed ? seed : 1)
		{
		}

		Json SaveTokens() const
		{
			Json entries = Json::array();
			for (const auto& [id, u] : users)
				if (u.token) entries.push_back({ { "id", id }, { "token", u.token } });
			return { { "version", 1 }, { "entries", entries } };
		}

		bool LoadTokens(const Json& saved)
		{
			try
			{
				if (saved.at("version") != 1 || !saved.at("entries").is_array() || saved.at("entries").size() > 4096) return false;
				std::map<uint64_t, User> loaded;
				for (const auto& entry : saved.at("entries"))
				{
					const auto id = Number(entry.at("id")), token = Number(entry.at("token"));
					if (!id || !token || loaded.contains(id)) return false;
					User u;
					u.token = token;
					loaded.emplace(id, std::move(u));
				}
				users = std::move(loaded);
				return true;
			}
			catch (const Json::exception&)
			{
				return false;
			}
		}

		void Expire(uint64_t now, const Allocator& allocate)
		{
			std::erase_if(searches,
				[&](auto& row)
				{
					auto& s = row.second;
					if (now >= s.seen && now - s.seen > 15000)
					{
						auto cancel = s.query;
						cancel.kind = match_queue::kCancel;
						allocate(cancel);
						return true;
					}
					return false;
				});
			// Native clients cache matchmaking tokens across matches. Expiring one
			// during gameplay makes the next valid search fail with HTTP403.
			// Tokens are not authentication: every operation still requires a live
			// social lease. Bound the cache without silently invalidating clients.
		}

		Answer Handle(const Principal& who, const Json& body, uint64_t now, uint64_t freshToken, uint64_t hostId, const Allocator& allocate,
			const Admission& admission = {}, const PlaylistPolicy& policy = {})
		{
			try
			{
				if (!who.id || !chat::Text(who.name, 35)) return Error(401, "unauthenticated player");
				Expire(now, allocate);
				const auto op = body.at("op").get<std::string>();
				if ((op == "info" || op == "token") && !users.contains(who.id) && users.size() >= 4096)
					return Error(503, "matchmaking token capacity reached");
				if (op == "info")
				{
					const auto& doc = body.at("document");
					if (!doc.is_object() || doc.dump().size() > 16384) return Error(400, "invalid player info");
					auto& u = users[who.id];
					u.info = doc;
					u.seen = now;
					return { 200, { { "accepted", true } } };
				}
				if (op == "token")
				{
					auto& u = users[who.id];
					if (!freshToken) return Error(503, "token unavailable");
					if (!u.token) u.token = freshToken;
					u.seen = now;
					return { 200, { { "token", u.token } } };
				}
				if (op == "start")
				{
					if (admission && !admission(who, now)) return Error(409, "party reports not ready");
					const auto request = Number(body.at("request"));
					const auto& doc = body.at("document");
					if (!request || !doc.is_object() || doc.dump().size() > 16384) return Error(400, "invalid search");
					auto old = searches.find(who.id);
					if (old != searches.end() && old->second.request == request)
					{
						if (old->second.document != doc) return Error(409, "request changed");
						old->second.seen = now;
						return { 200, { { "mm_id", old->second.id } } };
					}
					if (old != searches.end()) return Error(409, "cancel previous search first");
					const auto& members = doc.at("members");
					const auto& tokens = doc.at("tokens");
					if (!members.is_array() || members.empty() || members.size() > 20 || !tokens.is_array() ||
						tokens.size() != members.size())
						return Error(400, "invalid members");
					std::set<uint64_t> unique;
					bool owner = false;
					for (size_t i = 0; i < members.size(); ++i)
					{
						const auto id = Number(members[i]);
						const auto token = Number(tokens[i]);
						auto u = users.find(id);
						if (!id || !unique.insert(id).second || !who.members.contains(id) || u == users.end() || !token ||
							token != u->second.token)
							return Error(403, "invalid member token");
						owner |= id == who.id;
					}
					if (!owner) return Error(403, "search owner absent");
					if (admission && unique != who.members) return Error(409, "complete party required");
					const auto& filter = doc.at("ruleset_payload").at("filter");
					const int playlist = filter.at("playlist_id").get<int>();
					if (playlist < 0 || playlist > 128) return Error(400, "invalid playlist");
					if (policy && !policy(who, playlist)) return Error(403, "playlist policy denied");
					Search s;
					s.owner = who.id;
					s.id = ++sequence;
					s.request = request;
					s.seen = now;
					s.document = doc;
					s.principal = who;
					s.query.valid = true;
					s.query.request = s.id;
					s.query.playlists = { playlist };
					s.query.player.xuid = who.id;
					s.query.player.name = who.name;
					for (auto id : unique)
						if (id != who.id)
						{
							match_queue::Player p;
							p.xuid = id;
							p.name = "Player";
							s.query.players.push_back(p);
						}
					auto a = allocate(s.query);
					if (a.queue.phase == match_queue::Phase::Unavailable || a.queue.phase == match_queue::Phase::WrongPlaylist)
						return Error(409, "playlist unavailable");
					const auto id = s.id;
					searches.emplace(who.id, std::move(s));
					return { 200, { { "mm_id", id } } };
				}
				auto it = searches.find(who.id);
				const auto mmid = Number(body.at("mm_id"));
				if (op == "cancel" || op == "shutdown")
				{
					if (it != searches.end() && it->second.id == mmid)
					{
						auto cancel = it->second.query;
						cancel.kind = match_queue::kCancel;
						allocate(cancel);
						searches.erase(it);
					}
					return { 200, { { "accepted", true } } }; // Idempotent cancellation never cancels a newer generation.
				}
				if (op != "poll") return Error(400, "unsupported matchmaking operation");
				if (it == searches.end() || !mmid || it->second.id != mmid) return Error(404, "stale search");
				auto& s = it->second;
				if (policy && !policy(who, s.query.playlists.front()))
				{
					auto cancel = s.query;
					cancel.kind = match_queue::kCancel;
					allocate(cancel);
					searches.erase(it);
					return Error(403, "playlist policy changed");
				}
				if (admission && (who.members != s.principal.members || who.party != s.principal.party || !admission(s.principal, now)))
				{
					auto cancel = s.query;
					cancel.kind = match_queue::kCancel;
					allocate(cancel);
					searches.erase(it);
					return Error(409, "party changed or expired");
				}
				s.seen = now;
				auto a = allocate(s.query);
				Json result = { { "mm_id", mmid }, { "phase", int(a.queue.phase) }, { "seconds", a.queue.seconds } };
				if (a.queue.phase != match_queue::Phase::Ready) return { 200, result };
				// The real SessionInfo108 is CommonAddr84, securityID8, securityKey16.
				// JoinLobby's parsers decode those same fields separately from JSON.
				uint64_t lobby = 0;
				memcpy(&lobby, a.queue.session.data() + 84, 8);
				if (!lobby || !hostId || !matchmaking::Token(a.map) || !matchmaking::Token(a.mode)) return Error(503, "session not ready");
				Json players = Json::object(), states = Json::object(), mmids = Json::object();
				for (const auto& p : a.queue.players)
				{
					auto key = std::to_string(p.xuid);
					players[key] = "connected";
					states[key] = 1;
					mmids[key] = mmid;
				}
				const auto address = Base64(a.queue.session.data(), 84);
				Json host = { { "listen_server", { { "host_player_id", hostId }, { "host_address", address } } },
					{ "player_state", states }, { "ruleset_payload", { { "is_merge_src", false }, { "is_merge_dst", false } } },
					{ "team_balance", { { "can_change_teams", true } } }, { "lobby_open", true },
					{ "dedicated_server",
						{ { "info", "IW8 local backend" },
							{ "address", a.publicEndpoint.host + ":" + std::to_string(a.publicEndpoint.port) }, { "user_id", hostId },
							{ "data_center", "local" } } } };
				Json backend = { { "lobby_id", lobby }, { "update_id", 1 }, { "update_time", now },
					{ "listen_server", { { "lobby_host_order", Json::array({ hostId }) } } },
					{ "dtls",
						{ { "security_id", Base64(a.queue.session.data() + 84, 8) },
							{ "security_key", Base64(a.queue.session.data() + 92, 16) } } },
					{ "players", players }, { "player_id_to_mm_id", mmids },
					{ "ruleset_payload", { { "filter", s.document.at("ruleset_payload").at("filter") }, { "private_lobby", false } } },
					{ "performance", s.document.value("performance", Json{ { "game_type", 1 } }) } };
				result["lobby_id"] = lobby;
				result["host"] = host;
				result["backend"] = backend;
				result["map"] = a.map;
				result["mode"] = a.mode;
				// First native local candidate is the peer-selected route, distinct
				// from the public CommonAddr identity preserved for hashing/DTLS.
				const auto* route = a.queue.session.data();
				uint16_t routePort = 0;
				memcpy(&routePort, route + 4, 2);
				if (routePort)
					result["route"] = { { "host",
											std::to_string(route[0]) + "." + std::to_string(route[1]) + "." + std::to_string(route[2]) +
												"." + std::to_string(route[3]) },
						{ "port", routePort } };
				result["playlist"] = a.queue.playlist;
				return { 200, result };
			}
			catch (const Json::exception&)
			{
				return Error(400, "malformed matchmaking request");
			}
		}
	};
}
