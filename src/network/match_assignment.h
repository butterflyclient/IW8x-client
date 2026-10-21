#pragma once
#include "network/match_queue.h"
#include "thirdparty/json.hpp"
namespace matchmaking
{
	using Json = nlohmann::json;
	constexpr const char* kProtocol = "iw8-replay-1";

	struct Endpoint
	{
		std::string host;
		uint16_t port = 0;
	};

	struct Assignment
	{
		match_queue::Packet queue;
		std::string id, map, mode;
		Endpoint publicEndpoint, lanEndpoint;
	};

	inline std::string Hex(std::string_view bytes)
	{
		constexpr char digits[] = "0123456789abcdef";
		std::string out;
		out.reserve(bytes.size() * 2);
		for (unsigned char c : bytes)
		{
			out += digits[c >> 4];
			out += digits[c & 15];
		}
		return out;
	}

	inline std::string Unhex(std::string_view text)
	{
		if (text.empty() || text.size() > 4096 || text.size() % 2) return {};
		std::string out;
		out.reserve(text.size() / 2);
		for (size_t i = 0; i < text.size(); i += 2)
		{
			int a = online_services::protocol::Nibble(text[i]), b = online_services::protocol::Nibble(text[i + 1]);
			if (a < 0 || b < 0) return {};
			out += char(a * 16 + b);
		}
		return out;
	}

	inline bool Token(const std::string& text)
	{
		return !text.empty() && text.size() < 64 &&
			std::all_of(
				text.begin(), text.end(),
				[](unsigned char c)
				{
					return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
				});
	}

	inline bool ReadEndpoint(const Json& j, Endpoint& e)
	{
		if (!j.is_object() || !j.contains("host") || !j["host"].is_string() || !j.contains("port") || !j["port"].is_number_unsigned())
			return false;
		e.host = j["host"].get<std::string>();
		const auto port = j["port"].get<uint64_t>();
		if (!port || port > 65535 || e.host.empty() || e.host.size() > 253 || e.host.find_first_of("\r\n\t /\\:@") != std::string::npos ||
			e.host.find('\0') != std::string::npos)
			return false;
		e.port = uint16_t(port);
		return true;
	}

	inline Json EndpointJson(const Endpoint& e)
	{
		return { { "host", e.host }, { "port", e.port } };
	}

	inline std::string Request(const match_queue::Packet& packet)
	{
		return Json{ { "protocol", kProtocol }, { "queue", Hex(match_queue::Encode(packet)) } }.dump();
	}

	inline match_queue::Packet ReadRequest(std::string_view body)
	{
		try
		{
			auto j = Json::parse(body, nullptr, false);
			if (j.is_discarded() || !j.is_object() || j.value("protocol", std::string{}) != kProtocol || !j.contains("queue") ||
				!j["queue"].is_string())
				return {};
			auto p = match_queue::Parse(Unhex(j["queue"].get<std::string>()));
			if (p.kind != match_queue::kSearch && p.kind != match_queue::kCancel) p.valid = false;
			return p;
		}
		catch (const Json::exception&)
		{
			return {};
		}
	}

	inline std::string Response(const Assignment& a)
	{
		Json j = { { "protocol", kProtocol }, { "queue", Hex(match_queue::Encode(a.queue)) } };
		if (a.queue.phase == match_queue::Phase::Ready)
		{
			j["assignment"] = { { "id", a.id }, { "playlist", a.queue.playlist }, { "map", a.map }, { "mode", a.mode },
				{ "public", EndpointJson(a.publicEndpoint) } };
			if (a.lanEndpoint.port) j["assignment"]["lan"] = EndpointJson(a.lanEndpoint);
		}
		return j.dump();
	}

	inline bool ReadResponse(std::string_view body, uint64_t request, std::span<const int> playlists, Assignment& out)
	{
		try
		{
			auto j = Json::parse(body, nullptr, false);
			if (j.is_discarded() || !j.is_object() || j.value("protocol", std::string{}) != kProtocol || !j.contains("queue") ||
				!j["queue"].is_string())
				return false;
			Assignment a;
			a.queue = match_queue::Parse(Unhex(j["queue"].get<std::string>()));
			if (!a.queue.valid || a.queue.kind != match_queue::kReply || a.queue.request != request ||
				std::find(playlists.begin(), playlists.end(), a.queue.playlist) == playlists.end())
				return false;
			if (a.queue.phase == match_queue::Phase::Ready)
			{
				if (!j.contains("assignment") || !j["assignment"].is_object()) return false;
				const auto& v = j["assignment"];
				a.id = v.at("id").get<std::string>();
				a.map = v.at("map").get<std::string>();
				a.mode = v.at("mode").get<std::string>();
				if (!Token(a.id) || !Token(a.map) || !Token(a.mode) || v.at("playlist").get<int>() != a.queue.playlist ||
					!ReadEndpoint(v.at("public"), a.publicEndpoint))
					return false;
				if (v.contains("lan") && !ReadEndpoint(v["lan"], a.lanEndpoint)) return false;
			}
			out = std::move(a);
			return true;
		}
		catch (const Json::exception&)
		{
			return false;
		}
	}
}
