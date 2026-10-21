#pragma once
#include "network/online_services_protocol.h"

#include <vector>
namespace peers::wire
{
	using Session = online_services::protocol::Session;
	enum class Kind : uint8_t
	{
		Query = 1,
		Reply,
		Register,
		Lookup,
		Endpoint
	};
	enum class State : uint8_t
	{
		Unavailable,
		Party,
		Dedicated,
		Idle
	};

	struct Packet
	{
		bool valid = false;
		Kind kind = Kind::Query;
		uint64_t nonce = 0, xuid = 0;
		State state = State::Unavailable;
		uint8_t playlist = 0;
		uint32_t ip = 0;
		uint16_t port = 0;
		Session session{};
	};

	inline void Put(std::string& s, uint64_t v, unsigned n)
	{
		for (unsigned i = 0; i < n; ++i)
			s += char(v >> (8 * i));
	}

	inline uint64_t Get(std::string_view s, size_t pos, unsigned n)
	{
		uint64_t v = 0;
		for (unsigned i = 0; i < n; ++i)
			v |= uint64_t(uint8_t(s[pos + i])) << (8 * i);
		return v;
	}

	inline std::string Encode(const Packet& p)
	{
		std::string s("RPLY\2", 5);
		s += char(p.kind);
		Put(s, p.nonce, 8);
		Put(s, p.xuid, 8);
		if (p.kind == Kind::Reply)
		{
			s += char(p.state);
			s += char(p.playlist);
			// Party replies advertise their matchmaking directory. IP zero means
			// the queried host itself (its local loopback host is not ours).
			Put(s, p.ip, 4);
			Put(s, p.port, 2);
			if (p.state == State::Party || p.state == State::Dedicated)
				s.append(reinterpret_cast<const char*>(p.session.data()), p.session.size());
		}
		else if (p.kind == Kind::Endpoint)
		{
			Put(s, p.ip, 4);
			Put(s, p.port, 2);
		}
		return s;
	}

	inline Packet Parse(std::string_view s)
	{
		Packet p;
		if (s.size() < 22 || s.substr(0, 5) != std::string_view("RPLY\2", 5) || uint8_t(s[5]) < 1 || uint8_t(s[5]) > 5) return p;
		p.kind = Kind(uint8_t(s[5]));
		p.nonce = Get(s, 6, 8);
		p.xuid = Get(s, 14, 8);
		size_t expected = 22;
		if (p.kind == Kind::Reply)
		{
			if (s.size() < 30 || uint8_t(s[22]) > 3 || uint8_t(s[23]) > 128) return p;
			p.state = State(uint8_t(s[22]));
			p.playlist = uint8_t(s[23]);
			expected = 30;
			p.ip = uint32_t(Get(s, 24, 4));
			p.port = uint16_t(Get(s, 28, 2));
			if (p.ip == 0xffffffff) return p;
			if (p.state == State::Party || p.state == State::Dedicated)
			{
				expected += p.session.size();
				if (s.size() != expected) return p;
				std::copy(s.begin() + 30, s.end(), p.session.begin());
				if (std::all_of(p.session.begin(), p.session.end(),
						[](uint8_t b)
						{
							return b == 0;
						}))
					return p;
			}
		}
		else if (p.kind == Kind::Endpoint)
		{
			expected = 28;
			if (s.size() != expected) return p;
			p.ip = uint32_t(Get(s, 22, 4));
			p.port = uint16_t(Get(s, 26, 2));
			if (!p.ip || !p.port || p.ip == 0xffffffff) return p;
		}
		p.valid = s.size() == expected && p.nonce && (p.kind == Kind::Query || p.xuid);
		return p;
	}

	// A directory retains endpoints, never cached session keys. Resolve the live
	// session with a fresh nonce at the returned endpoint before invoking the game.
	struct Entry
	{
		uint64_t xuid, seen;
		uint32_t ip;
		uint16_t port;
	};

	class Directory
	{
		std::vector<Entry> entries;

	public:
		void Expire(uint64_t now)
		{
			std::erase_if(entries,
				[&](const Entry& e)
				{
					return now - e.seen > 15000;
				});
		}

		bool Register(Entry e)
		{
			Expire(e.seen);
			for (auto& old : entries)
				if (old.xuid == e.xuid)
				{
					if (old.ip != e.ip || old.port != e.port) return false;
					old = e;
					return true;
				}
			if (entries.size() >= 256) return false;
			entries.push_back(e);
			return true;
		}

		const Entry* Find(uint64_t xuid, uint64_t now)
		{
			Expire(now);
			for (const auto& e : entries)
				if (e.xuid == xuid) return &e;
			return nullptr;
		}
	};
}
