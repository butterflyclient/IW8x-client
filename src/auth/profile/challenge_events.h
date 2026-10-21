#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
namespace challenges
{
	// IW8 1.20.4 RVA15A3100 handles Y/d/k/r/v by the FIRST BYTE before
	// calling our receiver at15A26A0. In particular, "rpe" disconnects the client.
	// '_' reaches the ordinary dispatcher without native side effects.
	inline constexpr std::string_view kEventCommand = "_rpe";

	inline bool IsEventCommand(std::string_view token)
	{
		return token == kEventCommand;
	}

	inline std::string EventCommand(uint64_t session, uint64_t sequence, unsigned part, unsigned count, std::string_view hex)
	{
		return std::string(kEventCommand) + " " + std::to_string(session) + " " + std::to_string(sequence) + " " + std::to_string(part) +
			" " + std::to_string(count) + " " + std::string(hex);
	}

	struct Event
	{
		uint64_t session = 0, sequence = 0, xuid = 0;
		uint32_t match = 0, milliseconds = 0, utc = 0;
		std::string name;
		std::vector<std::string> parameters;
	};

	inline std::string Pack(const Event& event)
	{
		std::string bytes;
		auto integer = [&](uint64_t value, unsigned size)
		{
			for (unsigned i = 0; i < size; ++i)
				bytes += char(value >> (8 * i));
		};
		integer(1, 4);
		integer(event.session, 8);
		integer(event.sequence, 8);
		integer(event.xuid, 8);
		integer(event.match, 4);
		integer(event.milliseconds, 4);
		integer(event.utc, 4);
		integer(event.parameters.size(), 4);
		bytes += event.name;
		bytes += '\0';
		for (const auto& value : event.parameters)
		{
			bytes += value;
			bytes += '\0';
		}
		return bytes.size() <= 8192 ? bytes : std::string{};
	}

	inline std::optional<Event> Unpack(std::string_view bytes)
	{
		if (bytes.size() < 45 || bytes.size() > 8192) return {};
		size_t cursor = 0;
		auto integer = [&](unsigned size)
		{
			uint64_t value = 0;
			for (unsigned i = 0; i < size; ++i)
				value |= uint64_t(uint8_t(bytes[cursor++])) << (8 * i);
			return value;
		};
		if (integer(4) != 1) return {};
		Event event;
		event.session = integer(8);
		event.sequence = integer(8);
		event.xuid = integer(8);
		event.match = uint32_t(integer(4));
		event.milliseconds = uint32_t(integer(4));
		event.utc = uint32_t(integer(4));
		const auto count = integer(4);
		if (!event.session || !event.sequence || !event.xuid || count > 24) return {};
		for (unsigned i = 0; i <= count; ++i)
		{
			const auto end = bytes.find('\0', cursor);
			if (end == bytes.npos || end - cursor > 4096) return {};
			const auto text = bytes.substr(cursor, end - cursor);
			if (i == 0)
			{
				if (text.empty() || text.size() > 63 || text.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != text.npos)
					return {};
				event.name = std::string(text);
			}
			else
				event.parameters.emplace_back(text);
			cursor = end + 1;
		}
		if (cursor != bytes.size()) return {};
		return event;
	}

	inline std::string Hex(std::string_view bytes)
	{
		constexpr char digits[] = "0123456789abcdef";
		std::string result;
		result.reserve(bytes.size() * 2);
		for (uint8_t c : bytes)
		{
			result += digits[c >> 4];
			result += digits[c & 15];
		}
		return result;
	}

	inline std::optional<std::string> Unhex(std::string_view hex)
	{
		if (hex.empty() || hex.size() > 800 || hex.size() % 2) return {};
		auto digit = [](char c)
		{
			return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
		};
		std::string bytes;
		for (size_t i = 0; i < hex.size(); i += 2)
		{
			const int a = digit(hex[i]), b = digit(hex[i + 1]);
			if (a < 0 || b < 0) return {};
			bytes += char(a * 16 + b);
		}
		return bytes;
	}

	// The engine's reliable channel is ordered. Keep one bounded event in flight,
	// never an unbounded map keyed by network-controlled fragment identifiers.
	struct Assembly
	{
		uint64_t session = 0, sequence = 0;
		unsigned next = 0, total = 0;
		std::string bytes;

		std::optional<Event> Add(uint64_t s, uint64_t q, unsigned part, unsigned count, std::string_view hex)
		{
			const auto decoded = Unhex(hex);
			if (!s || !q || !decoded || count == 0 || count > 21 || part >= count)
			{
				*this = {};
				return {};
			}
			if (part == 0)
			{
				*this = {};
				session = s;
				sequence = q;
				total = count;
			}
			if (s != session || q != sequence || total != count || part != next || bytes.size() + decoded->size() > 8192)
			{
				*this = {};
				return {};
			}
			bytes += *decoded;
			++next;
			if (next != total) return {};
			const auto event = Unpack(bytes);
			const bool valid = event && event->session == session && event->sequence == sequence;
			*this = {};
			return valid ? event : std::nullopt;
		}
	};
}
