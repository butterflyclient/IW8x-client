#pragma once
#include "network/peer_protocol.h"

#include <charconv>
namespace peers
{
	struct Endpoint
	{
		uint32_t ip = 0; // IPv4 network-order bytes, as in sockaddr_in.
		uint16_t port = 0; // Host order.
		bool operator==(const Endpoint&) const = default;
	};

	inline bool ParseXuid(std::string_view text, uint64_t& xuid)
	{
		int radix = 10;
		if (text.starts_with("0x") || text.starts_with("0X"))
		{
			text.remove_prefix(2);
			radix = 16;
		}
		if (text.empty()) return false;
		// from_chars writes a numeric prefix even when the whole input is an IP
		// address ("127.0.0.1" -> 127). Do not turn an address lookup into a query
		// for that partial XUID; callers use zero to request the endpoint's identity.
		uint64_t parsed = 0;
		const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, radix);
		if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !parsed) return false;
		xuid = parsed;
		return true;
	}

	// Socket-independent reply correlation. A packet never initiates a join unless
	// a local connect request authorized its nonce, endpoint and optional XUID.
	struct Request
	{
		uint64_t nonce = 0, xuid = 0, started = 0;
		bool discovery = false, preferParty = false;
		std::vector<Endpoint> targets;
		wire::Packet candidate{};
		Endpoint candidateFrom{};

		bool Accept(const wire::Packet& p, Endpoint from, uint64_t self, uint64_t now)
		{
			if (!nonce || now - started >= 10000 || !p.valid || p.kind != wire::Kind::Reply || p.nonce != nonce || p.xuid == self ||
				(xuid && p.xuid != xuid))
				return false;
			const bool known = std::find(targets.begin(), targets.end(), from) != targets.end();
			if (!known && !(discovery && xuid && (from.port == 27126 || from.port == 27127))) return false;
			if (p.state == wire::State::Unavailable) return false;
			if (!candidate.valid || p.state == wire::State::Party ||
				(p.state == wire::State::Dedicated && candidate.state == wire::State::Idle))
			{
				candidate = p;
				candidateFrom = from;
			}
			return true;
		}

		bool Ready(uint64_t now) const
		{
			return nonce && now - started < 10000 && candidate.valid &&
				(!preferParty || candidate.state == wire::State::Party || now - started >= 1200);
		}

		bool Expired(uint64_t now) const
		{
			return nonce && now - started >= 10000;
		}
	};
}
