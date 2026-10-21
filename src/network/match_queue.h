#pragma once
#include "network/online_services_protocol.h"
#include "network/peer_protocol.h"

#include <algorithm>
#include <vector>
namespace match_queue
{
	constexpr uint8_t kSearch = 0x35, kReply = 0x36, kCancel = 0x37;
	constexpr size_t kMaxPlayers = 20;
	enum class Phase : uint8_t
	{
		Waiting,
		Loading,
		Ready,
		Unavailable,
		WrongPlaylist
	};

	struct Player
	{
		uint64_t xuid = 0;
		std::string name, clan;
		uint16_t rank = 0, prestige = 0;
		uint32_t emblem = 0, background = 0;
	};

	struct Packet
	{
		bool valid = false;
		uint8_t kind = kSearch;
		uint64_t request = 0;
		Player player;
		std::vector<int> playlists;
		int playlist = 0;
		Phase phase = Phase::Waiting;
		unsigned seconds = 60;
		// Search: additional native party members. Reply: complete waiting roster.
		std::vector<Player> players;
		online_services::protocol::Session session{};
	};

	inline bool Text(std::string_view s, size_t max)
	{
		return s.size() <= max &&
			std::all_of(s.begin(), s.end(),
				[](unsigned char c)
				{
					return c >= 32 && c != 127;
				});
	}

	inline void PutPlayer(std::string& s, const Player& p)
	{
		peers::wire::Put(s, p.xuid, 8);
		s += char(p.name.size());
		s += p.name;
		s += char(p.clan.size());
		s += p.clan;
		peers::wire::Put(s, p.rank, 2);
		peers::wire::Put(s, p.prestige, 2);
		peers::wire::Put(s, p.emblem, 4);
		peers::wire::Put(s, p.background, 4);
	}

	inline bool GetPlayer(std::string_view s, size_t& i, Player& p)
	{
		if (s.size() - i < 10) return false;
		p.xuid = peers::wire::Get(s, i, 8);
		i += 8;
		auto n = uint8_t(s[i++]);
		if (n < 1 || n > 35 || s.size() - i < size_t(n) + 1) return false;
		p.name = s.substr(i, n);
		i += n;
		n = uint8_t(s[i++]);
		if (n > 5 || s.size() - i < size_t(n) + 12) return false;
		p.clan = s.substr(i, n);
		i += n;
		p.rank = uint16_t(peers::wire::Get(s, i, 2));
		i += 2;
		p.prestige = uint16_t(peers::wire::Get(s, i, 2));
		i += 2;
		p.emblem = uint32_t(peers::wire::Get(s, i, 4));
		i += 4;
		p.background = uint32_t(peers::wire::Get(s, i, 4));
		i += 4;
		return p.xuid && Text(p.name, 35) && Text(p.clan, 5) && p.rank <= 10000 && p.prestige <= 1000;
	}

	inline std::string Encode(const Packet& p)
	{
		const bool group = p.kind == kSearch && !p.players.empty();
		std::string s{ char(p.kind), char(group ? 2 : 1) };
		peers::wire::Put(s, p.request, 8);
		if (p.kind == kSearch || p.kind == kCancel)
		{
			PutPlayer(s, p.player);
			if (group)
			{
				s += char(p.players.size());
				for (const auto& member : p.players)
					PutPlayer(s, member);
			}
			if (p.kind == kSearch)
			{
				s += char(p.playlists.size());
				for (int id : p.playlists)
					s += char(id);
			}
		}
		else
		{
			s += char(p.playlist);
			s += char(p.phase);
			s += char(p.seconds);
			s += char(p.players.size());
			for (const auto& player : p.players)
				PutPlayer(s, player);
			if (p.phase == Phase::Ready) s.append(reinterpret_cast<const char*>(p.session.data()), p.session.size());
		}
		return s;
	}

	inline Packet Parse(std::string_view s)
	{
		Packet p;
		if (s.size() < 10 || s.size() > 2048) return p;
		const auto version = uint8_t(s[1]);
		if (version != 1 && !(version == 2 && uint8_t(s[0]) == kSearch)) return p;
		p.kind = uint8_t(s[0]);
		p.request = peers::wire::Get(s, 2, 8);
		if (!p.request) return p;
		size_t i = 10;
		if (p.kind == kSearch || p.kind == kCancel)
		{
			if (!GetPlayer(s, i, p.player)) return p;
			if (version == 2)
			{
				if (i >= s.size()) return p;
				const auto count = uint8_t(s[i++]);
				if (!count || count >= kMaxPlayers) return p;
				for (unsigned j = 0; j < count; ++j)
				{
					Player member;
					if (!GetPlayer(s, i, member) || member.xuid == p.player.xuid) return p;
					if (std::any_of(p.players.begin(), p.players.end(),
							[&](const Player& old)
							{
								return old.xuid == member.xuid;
							}))
						return p;
					p.players.push_back(std::move(member));
				}
			}
			if (p.kind == kSearch)
			{
				if (i >= s.size()) return p;
				const auto count = uint8_t(s[i++]);
				if (!count || count > 20 || s.size() - i != count) return p;
				for (unsigned j = 0; j < count; ++j)
				{
					int id = uint8_t(s[i++]);
					if (id > 128 || std::find(p.playlists.begin(), p.playlists.end(), id) != p.playlists.end()) return p;
					p.playlists.push_back(id);
				}
			}
		}
		else if (p.kind == kReply)
		{
			if (s.size() - i < 4) return p;
			p.playlist = uint8_t(s[i++]);
			const auto phase = uint8_t(s[i++]);
			p.seconds = uint8_t(s[i++]);
			const auto count = uint8_t(s[i++]);
			if (p.playlist > 128 || phase > 4 || p.seconds > 60 || count > kMaxPlayers) return p;
			p.phase = Phase(phase);
			for (unsigned j = 0; j < count; ++j)
			{
				Player player;
				if (!GetPlayer(s, i, player)) return p;
				if (std::any_of(p.players.begin(), p.players.end(),
						[&](const Player& old)
						{
							return old.xuid == player.xuid;
						}))
					return p;
				p.players.push_back(std::move(player));
			}
			if (p.phase == Phase::Ready)
			{
				if (s.size() - i != p.session.size()) return p;
				std::copy(s.begin() + i, s.end(), p.session.begin());
				i += p.session.size();
				if (std::all_of(p.session.begin(), p.session.end(),
						[](uint8_t b)
						{
							return !b;
						}))
					return p;
			}
		}
		else
			return p;
		p.valid = i == s.size();
		return p;
	}

	struct Ticket
	{
		Player player;
		uint64_t request = 0, seen = 0;
		uint32_t ip = 0;
		uint16_t port = 0;
		uint64_t leader = 0;

		uint64_t Owner() const
		{
			return leader ? leader : player.xuid;
		}
	};

	class Queue
	{
		std::vector<Ticket> tickets;
		uint64_t first = 0;
		int selected = -1;
		bool force = false;

	public:
		int Playlist() const
		{
			return selected;
		}

		const std::vector<Ticket>& Tickets() const
		{
			return tickets;
		}

		void Clear()
		{
			tickets.clear();
			selected = -1;
			first = 0;
			force = false;
		}

		void Expire(uint64_t now)
		{
			std::erase_if(tickets,
				[&](const Ticket& t)
				{
					return now - t.seen > 7000;
				});
			if (tickets.empty()) Clear();
		}

		bool Add(int playlist, const Ticket& ticket)
		{
			return AddGroup(playlist, ticket, {});
		}

		bool AddGroup(int playlist, const Ticket& ticket, const std::vector<Player>& members)
		{
			Expire(ticket.seen);
			if (!ticket.player.xuid || !ticket.request || members.size() >= kMaxPlayers || (selected >= 0 && playlist != selected))
				return false;
			auto sameGroup = [&](const Ticket& old)
			{
				return old.Owner() == ticket.player.xuid && old.ip == ticket.ip && old.port == ticket.port;
			};
			std::vector<Player> roster{ ticket.player };
			for (const auto& member : members)
			{
				if (!member.xuid ||
					std::any_of(roster.begin(), roster.end(),
						[&](const Player& p)
						{
							return p.xuid == member.xuid;
						}))
					return false;
				roster.push_back(member);
			}
			size_t retained = 0;
			for (const auto& old : tickets)
			{
				if (sameGroup(old)) continue;
				++retained;
				if (std::any_of(roster.begin(), roster.end(),
						[&](const Player& p)
						{
							return p.xuid == old.player.xuid;
						}))
					return false;
			}
			if (retained + roster.size() > kMaxPlayers) return false;
			// Validate the whole group before replacing it. Every member shares the
			// leader's endpoint, generation and lease; a refresh also removes leavers.
			if (tickets.empty())
			{
				selected = playlist;
				first = ticket.seen;
			}
			std::erase_if(tickets,
				[&](const Ticket& old)
				{
					return sameGroup(old) &&
						std::none_of(roster.begin(), roster.end(),
							[&](const Player& p)
							{
								return p.xuid == old.player.xuid;
							});
				});
			for (const auto& player : roster)
			{
				auto member = ticket;
				member.player = player;
				member.leader = ticket.player.xuid;
				const auto old = std::find_if(tickets.begin(), tickets.end(),
					[&](const Ticket& t)
					{
						return t.player.xuid == player.xuid;
					});
				if (old == tickets.end())
					tickets.push_back(std::move(member));
				else
					*old = std::move(member);
			}
			return true;
		}

		void Cancel(uint64_t xuid, uint64_t request, uint32_t ip, uint16_t port)
		{
			std::erase_if(tickets,
				[&](const Ticket& t)
				{
					return t.Owner() == xuid && t.request == request && t.ip == ip && t.port == port;
				});
			if (tickets.empty()) Clear();
		}

		bool Force()
		{
			if (tickets.empty()) return false;
			force = true;
			return true;
		}

		bool ShouldStart(uint64_t now) const
		{
			return !tickets.empty() && (force || tickets.size() >= 2 || now - first >= 60000);
		}

		unsigned Seconds(uint64_t now) const
		{
			return tickets.empty() ? 60 : unsigned((60000 - std::min<uint64_t>(60000, now - first) + 999) / 1000);
		}
	};
}
