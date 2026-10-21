#pragma once
#include "network/social/chat_backend.h"

#include <array>
namespace social
{
	// Native UserList capacity is 200. Scores are server UTC seconds (last met).
	// Mutations carry a durable nonce/request receipt, so a lost response cannot
	// replay an older add after a later remove, including across server restarts.
	struct UserLists
	{
		struct Entry
		{
			uint64_t id = 0, time = 0;
			std::string name;
		};

		std::array<std::vector<Entry>, 3> lists;

		struct Receipt
		{
			std::string token, body;
		};

		std::deque<Receipt> receipts;

		static int Type(std::string_view name)
		{
			return name == "recentPlayers" ? 0 : name == "blocklistPlayers" ? 1 : name == "mutedPlayers" ? 2 : -1;
		}

		bool Contains(int type, uint64_t id) const
		{
			return std::any_of(lists[type].begin(), lists[type].end(),
				[&](const Entry& e)
				{
					return e.id == id;
				});
		}

		chat::Json Rows(int type) const
		{
			auto out = chat::Json::array();
			for (const auto& e : lists[type])
				out.push_back({ { "xuid", std::to_string(e.id) }, { "name", e.name }, { "time", e.time } });
			return out;
		}

		chat::Json Save() const
		{
			auto out = chat::Json::array(), saved = chat::Json::array();
			for (int i = 0; i < 3; ++i)
				out.push_back(Rows(i));
			for (const auto& r : receipts)
				saved.push_back({ { "token", r.token }, { "body", r.body } });
			return { { "lists", out }, { "receipts", saved } };
		}

		bool Load(const chat::Json& j, uint64_t owner)
		{
			try
			{
				UserLists next;
				if (!j.at("lists").is_array() || j["lists"].size() != 3 || !j.at("receipts").is_array() || j["receipts"].size() > 64)
					return false;
				for (int i = 0; i < 3; ++i)
				{
					const auto& rows = j["lists"][i];
					if (!rows.is_array() || rows.size() > 200) return false;
					for (const auto& r : rows)
					{
						Entry e{ chat::Id(r, "xuid"), r.at("time").get<uint64_t>(), r.at("name").get<std::string>() };
						if (!e.id || e.id == owner || !e.time || e.time > UINT32_MAX || !chat::Text(e.name, 63) || next.Contains(i, e.id))
							return false;
						next.lists[i].push_back(std::move(e));
					}
				}
				std::set<std::string> tokens;
				for (const auto& r : j["receipts"])
				{
					Receipt receipt{ r.at("token"), r.at("body") };
					if (receipt.token.size() > 64 || receipt.token.size() < 34 || receipt.body.size() > 8192 ||
						!tokens.insert(receipt.token).second)
						return false;
					next.receipts.push_back(std::move(receipt));
				}
				*this = std::move(next);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		// Caller validates identities and commits this copy atomically with storage.
		unsigned Mutate(int type, bool remove, const std::vector<Entry>& entries, const std::string& token, const std::string& body)
		{
			for (const auto& r : receipts)
				if (r.token == token) return r.body == body ? 200 : 409;
			auto next = lists[type];
			for (const auto& e : entries)
			{
				std::erase_if(next,
					[&](const Entry& old)
					{
						return old.id == e.id;
					});
				if (!remove) next.insert(next.begin(), e);
			}
			std::stable_sort(next.begin(), next.end(),
				[](const Entry& a, const Entry& b)
				{
					return a.time > b.time;
				});
			if (next.size() > 200)
			{
				if (type == 1) return 409;
				next.resize(200);
			}
			lists[type] = std::move(next);
			receipts.push_back({ token, body });
			if (receipts.size() > 64) receipts.pop_front();
			return 200;
		}
	};
}
