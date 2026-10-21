#pragma once
#include "network/social/social_backend.h"

#include <array>
namespace party_relay120
{
	using chat::Json;

	// Private transport identifiers in the IPv4 benchmarking range. They are never
	// sent to the network: the native socket adapter carries these datagrams over
	// the authenticated provider. Real dedicated-server destinations stay native.
	inline std::array<uint8_t, 6> Address(uint64_t id)
	{
		uint64_t hash = 14695981039346656037ull;
		for (unsigned i = 0; i < 8; ++i)
		{
			hash ^= uint8_t(id >> (i * 8));
			hash *= 1099511628211ull;
		}
		const auto port = uint16_t(1024 + (hash >> 17) % 64512);
		return { 198, uint8_t(18 + ((hash >> 16) & 1)), uint8_t(hash >> 8), uint8_t(hash), uint8_t(port >> 8), uint8_t(port) };
	}

	inline std::string Hex(const void* data, size_t size)
	{
		std::string out;
		out.reserve(size * 2);
		for (size_t i = 0; i < size; ++i)
		{
			const auto b = static_cast<const uint8_t*>(data)[i];
			out += "0123456789abcdef"[b >> 4];
			out += "0123456789abcdef"[b & 15];
		}
		return out;
	}

	inline std::string Endpoint(uint64_t id)
	{
		const auto a = Address(id);
		return Hex(a.data(), a.size());
	}

	inline std::string Unhex(std::string_view text)
	{
		std::string out;
		out.reserve(text.size() / 2);
		auto digit = [](char c)
		{
			return c <= '9' ? c - '0' : c - 'a' + 10;
		};
		for (size_t i = 0; i < text.size(); i += 2)
			out += char(digit(text[i]) * 16 + digit(text[i + 1]));
		return out;
	}

	class Service
	{
		struct Packet
		{
			uint64_t sequence, from, expires;
			std::string bytes;
		};

		struct Client
		{
			std::string lease;
			uint64_t sequence = 0, request = 0, rateAt = 0;
			unsigned sent = 0;
			std::deque<Packet> inbox;
		};

		std::map<uint64_t, Client> clients;

	public:
		chat::Answer Handle(const Json& j, uint32_t ip, uint64_t now, social::Backend& social)
		{
			auto error = [](unsigned status, const char* text)
			{
				return chat::Answer{ status, { { "error", text } } };
			};
			try
			{
				const auto who = social.AuthorizeCommunications(j, ip, now);
				if (who.empty()) return error(401, "expired relay lease");
				const auto id = chat::Id(who, "id");
				const auto lease = j.at("lease").get<std::string>();
				const auto request = chat::Id(j, "request"), ack = chat::Id(j, "ack");
				if (j.at("schema") != 1 || !request || !j.at("packets").is_array() || j.at("packets").size() > 24)
					return error(400, "invalid relay batch");
				for (const auto& packet : j.at("packets"))
					if (!packet.is_object() || !chat::Hex(packet.at("to").get<std::string>(), 12) || !packet.at("bytes").is_string() ||
						packet.at("bytes").get_ref<const std::string&>().empty() ||
						packet.at("bytes").get_ref<const std::string&>().size() > 4096 ||
						packet.at("bytes").get_ref<const std::string&>().size() % 2 ||
						!chat::Hex(
							packet.at("bytes").get_ref<const std::string&>(), packet.at("bytes").get_ref<const std::string&>().size()))
						return error(400, "invalid relay datagram");
				std::erase_if(clients,
					[&](const auto& row)
					{
						return !social.CommunicationsLive(row.first, row.second.lease, now);
					});
				for (const auto& [other, c] : clients)
					if (other != id && Endpoint(other) == Endpoint(id)) return error(409, "relay endpoint collision");
				auto& c = clients[id];
				if (c.lease != lease)
				{
					c = {};
					c.lease = lease;
				}
				if (j.value("stream", std::string{}) == c.lease)
				{
					if (ack > c.sequence) return error(400, "invalid relay acknowledgement");
					std::erase_if(c.inbox,
						[&](const Packet& p)
						{
							return p.sequence <= ack;
						});
				}
				if (!c.rateAt || now < c.rateAt || now - c.rateAt >= 1000)
				{
					c.rateAt = now;
					c.sent = 0;
				}
				if (request > c.request)
				{
					if (c.sent + j.at("packets").size() > 256) return error(429, "relay rate limit");
					c.sent += unsigned(j.at("packets").size());
					c.request = request;
					for (const auto& packet : j.at("packets"))
					{
						const auto destination = packet.at("to").get<std::string>();
						for (auto& [other, receiver] : clients)
							if (other != id && Endpoint(other) == destination && social.PartyRelayAllowed(id, other, now))
							{
								if (receiver.inbox.size() >= 128) receiver.inbox.pop_front();
								receiver.inbox.push_back({ ++receiver.sequence, id, now + 3000, packet.at("bytes").get<std::string>() });
								break;
							}
					}
				}
				std::erase_if(c.inbox,
					[&](const Packet& p)
					{
						return now >= p.expires || !social.PartyRelayAllowed(id, p.from, now);
					});
				Json packets = Json::array();
				for (const auto& packet : c.inbox)
				{
					packets.push_back(
						{ { "seq", std::to_string(packet.sequence) }, { "from", Endpoint(packet.from) }, { "bytes", packet.bytes } });
					if (packets.size() == 24) break;
				}
				return { 200,
					{ { "schema", 1 }, { "stream", c.lease }, { "endpoint", Endpoint(id) }, { "request", std::to_string(request) },
						{ "packets", packets } } };
			}
			catch (...)
			{
				return error(400, "malformed relay request");
			}
		}
	};
}
