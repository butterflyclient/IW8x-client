#pragma once
#include <array>
#include <algorithm>
#include <span>
#include <cstdint>
#include <string>
#include <string_view>
namespace online_services::protocol
{
	constexpr size_t kSessionBytes = 108;
	constexpr size_t kSessionHex = kSessionBytes * 2;
	using Session = std::array<uint8_t, kSessionBytes>;

	inline int Nibble(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	inline bool ParseSession(std::string_view hex, Session& session)
	{
		if (hex.size() != kSessionHex) return false;
		Session parsed{};
		for (size_t i = 0; i < parsed.size(); ++i)
		{
			const int hi = Nibble(hex[i * 2]), lo = Nibble(hex[i * 2 + 1]);
			if (hi < 0 || lo < 0) return false;
			parsed[i] = uint8_t((hi << 4) | lo);
		}
		session = parsed;
		return true;
	}

	inline std::string SessionHex(const Session& session)
	{
		constexpr char digits[] = "0123456789ABCDEF";
		std::string hex(kSessionHex, '0');
		for (size_t i = 0; i < session.size(); ++i)
		{
			hex[i * 2] = digits[session[i] >> 4];
			hex[i * 2 + 1] = digits[session[i] & 15];
		}
		return hex;
	}

	// Direct dedicated matchmaking v1. Request IDs correlate replies with the
	// current search, so UDP replies from a cancelled/different playlist cannot join.
	constexpr uint8_t kDedicatedSearch = 0x31, kDedicatedReply = 0x32;
	enum class DedicatedState : uint8_t
	{
		Waiting = 0,
		Ready = 1,
		WrongPlaylist = 2,
		NoPlaylist = 3
	};

	struct DedicatedPacket
	{
		bool valid = false;
		uint8_t playlist = 0;
		uint64_t request = 0;
		DedicatedState state = DedicatedState::Waiting;
		Session session{};
	};

	inline std::string DedicatedSearch(uint8_t playlist, uint64_t request)
	{
		std::string wire{ char(kDedicatedSearch), char(1), char(playlist) };
		for (unsigned i = 0; i < 8; ++i)
			wire += char(request >> (i * 8));
		return wire;
	}

	inline DedicatedPacket ParseDedicated(std::string_view wire, bool reply)
	{
		DedicatedPacket out;
		if (wire.size() < 11 || uint8_t(wire[0]) != (reply ? kDedicatedReply : kDedicatedSearch) || uint8_t(wire[1]) != 1 ||
			uint8_t(wire[2]) > 128)
			return out;
		out.playlist = uint8_t(wire[2]);
		for (unsigned i = 0; i < 8; ++i)
			out.request |= uint64_t(uint8_t(wire[3 + i])) << (i * 8);
		if (reply)
		{
			if (wire.size() < 12 || uint8_t(wire[11]) > 3) return out;
			out.state = DedicatedState(uint8_t(wire[11]));
			const auto expected = out.state == DedicatedState::Ready ? 12 + kSessionBytes : 12;
			if (wire.size() != expected) return out;
			if (out.state == DedicatedState::Ready)
				for (size_t i = 0; i < out.session.size(); ++i)
					out.session[i] = uint8_t(wire[12 + i]);
		}
		else if (wire.size() != 11)
			return out;
		out.valid = out.request != 0;
		return out;
	}

	inline std::string DedicatedReply(const DedicatedPacket& request, DedicatedState state, const Session& session)
	{
		auto wire = DedicatedSearch(request.playlist, request.request);
		wire[0] = char(kDedicatedReply);
		wire += char(state);
		if (state == DedicatedState::Ready) wire.append(reinterpret_cast<const char*>(session.data()), session.size());
		return wire;
	}

	inline bool MatchesSearch(const DedicatedPacket& reply, int playlist, uint64_t request)
	{
		return reply.valid && reply.playlist == playlist && reply.request == request;
	}

	inline bool MatchesSearch(const DedicatedPacket& reply, std::span<const int> playlists, uint64_t request)
	{
		return reply.valid && reply.request == request && std::find(playlists.begin(), playlists.end(), reply.playlist) != playlists.end();
	}
	enum class Kind
	{
		Invalid,
		BanStatus,
		Searching,
		RequestSession,
		JoinSession,
		PlayerOnline,
		Error,
		UpdateRequired
	};

	struct Packet
	{
		Kind kind = Kind::Invalid;
		uint8_t value = 0;
		std::string text;
		Session session{};
	};

	inline Packet Parse(std::string_view bytes)
	{
		Packet out;
		if (bytes.empty() || bytes.size() > 256) return out;
		const auto type = uint8_t(bytes.front());
		const auto payload = bytes.substr(1);
		switch (type)
		{
		case 0x01:
			if (payload.size() == 1)
			{
				out.kind = Kind::BanStatus;
				out.value = uint8_t(payload[0]);
			}
			break;
		case 0x11:
			if (payload.empty()) out.kind = Kind::Searching;
			break;
		case 0x12:
			if (payload.empty()) out.kind = Kind::RequestSession;
			break;
		case 0x13:
			if (ParseSession(payload, out.session)) out.kind = Kind::JoinSession;
			break;
		case 0x20:
		case 0xf0:
			if (!payload.empty())
			{
				out.kind = type == 0x20 ? Kind::PlayerOnline : Kind::Error;
				out.text.assign(payload);
				for (char& c : out.text)
					if (uint8_t(c) < 32 || c == 127) c = ' ';
			}
			break;
		case 0xf1:
			out.kind = Kind::UpdateRequired;
			break;
		}
		return out;
	}
}
