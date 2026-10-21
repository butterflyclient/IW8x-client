#pragma once
#include "network/social/chat_backend.h"

#include <array>
#include <string_view>
// Replay's title-public DW service95/task1 contract. HTTP is the local transport,
// not a claim to implement the encrypted retail Demonware wire protocol.

namespace publisher
{
	using chat::Json;
	using chat::Answer;
	inline constexpr std::array<std::string_view, 7> Names{ "general", "pc", "f2p", "general_8_19", "pc_8_19", "f2p_8_19", "autobuild" };

	inline int Index(std::string_view name)
	{
		for (size_t i = 0; i < Names.size(); ++i)
			if (Names[i] == name) return int(i);
		return -1;
	}

	inline bool Version(const Json& j)
	{
		return j.is_number_integer() && j >= 0 && j <= 65535;
	}

	inline bool Variables(const Json& values)
	{
		if (!values.is_object() || values.size() > 256) return false;
		for (auto it = values.begin(); it != values.end(); ++it)
		{
			const auto& key = it.key();
			if (key.empty() || key.size() > 63 || key == "majorVersion" || key == "minorVersion") return false;
			for (unsigned char c : key)
				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
			if (!it->is_string()) return false;
			const auto& text = it->get_ref<const std::string&>();
			if (text.size() > 127) return false;
			for (unsigned char c : text)
				if (c < 32 || c > 126) return false;
		}
		return values.dump().size() < 60000;
	}

	inline bool Namespace(const Json& j)
	{
		return j.is_object() && j.contains("major") && Version(j["major"]) && j["major"] > 0 && j.contains("minor") &&
			Version(j["minor"]) && j.contains("variables") && Variables(j["variables"]);
	}

	inline bool Response(const Json& j, std::string_view expected)
	{
		return j.is_object() && j.contains("version") && j["version"] == 1 && j.contains("context") && j["context"] == "" &&
			j.contains("namespace") && j["namespace"].is_string() && j["namespace"].get_ref<const std::string&>() == expected &&
			Index(expected) >= 0 && Namespace(j);
	}

	inline bool Manifest(const Json& j)
	{
		if (!j.is_object() || !j.contains("version") || j["version"] != 1 || !j.contains("context") || j["context"] != "" ||
			!j.contains("namespaces") || !j["namespaces"].is_object() || j["namespaces"].size() != Names.size())
			return false;
		for (auto name : Names)
		{
			const auto it = j["namespaces"].find(std::string(name));
			if (it == j["namespaces"].end() || !it->is_object() || !it->contains("major") || !Version((*it)["major"]) ||
				(*it)["major"] == 0 || !it->contains("minor") || !Version((*it)["minor"]))
				return false;
		}
		return true;
	}

	class Backend
	{
		Json namespaces = Json::object();

	public:
		Backend()
		{
			for (auto name : Names)
				namespaces[std::string(name)] = { { "major", 1 }, { "minor", 0 }, { "variables", Json::object() } };
			// Harmless service-owned metadata. Retail live-ops values are unknown;
			// keep shipped dvar defaults and existing compatibility fixes intact.
			namespaces["general"]["variables"]["replay_publisher_revision"] = "1";
		}

		Json Document() const
		{
			return { { "version", 1 }, { "namespaces", namespaces } };
		}

		bool Load(const Json& doc, bool requireNewVersion = false)
		{
			try
			{
				if (!doc.is_object() || doc.value("version", 0) != 1 || !doc.contains("namespaces") || !doc["namespaces"].is_object() ||
					doc["namespaces"].size() > Names.size())
					return false;
				Backend candidate;
				for (auto it = doc["namespaces"].begin(); it != doc["namespaces"].end(); ++it)
				{
					if (Index(it.key()) < 0 || !Namespace(*it)) return false;
					candidate.namespaces[it.key()] = *it;
				}
				if (requireNewVersion)
					for (auto name : Names)
					{
						const auto& old = namespaces[std::string(name)];
						const auto& next = candidate.namespaces[std::string(name)];
						// Reject stale revisions and edits under an unchanged version.
						if (next["major"] < old["major"] || (next["major"] == old["major"] && next["minor"] < old["minor"]) ||
							(next["major"] == old["major"] && next["minor"] == old["minor"] && next["variables"] != old["variables"]))
							return false;
					}
				namespaces = std::move(candidate.namespaces);
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		Answer Handle(const Json& request) const
		{
			try
			{
				if (!request.is_object() || request.value("version", 0) != 1 || !request.contains("context") || request["context"] != "")
					return { 400, { { "error", "invalid publisher context or protocol" } } };
				const auto op = request.value("op", std::string{});
				if (op == "versions")
				{
					Json list = Json::object();
					for (auto name : Names)
					{
						const auto& ns = namespaces[std::string(name)];
						list[std::string(name)] = { { "major", ns["major"] }, { "minor", ns["minor"] } };
					}
					return { 200, { { "version", 1 }, { "context", "" }, { "namespaces", std::move(list) } } };
				}
				if (op != "fetch") return { 400, { { "error", "unsupported publisher operation" } } };
				const auto name = request.value("namespace", std::string{});
				if (Index(name) < 0) return { 404, { { "error", "unknown publisher namespace" } } };
				auto result = namespaces.at(name);
				result["version"] = 1;
				result["context"] = "";
				result["namespace"] = name;
				return { 200, std::move(result) };
			}
			catch (...)
			{
				return { 400, { { "error", "malformed publisher request" } } };
			}
		}
	};
}
