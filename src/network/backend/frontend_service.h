#pragma once
#include "thirdparty/json.hpp"

#include <string>
#include <string_view>

#include "network/backend/motd_catalog.h"
// Local provider contract, not the retail Battle.net/Demonware wire protocol.
// Only the services needed by the Replay frontend are advertised here.

namespace frontend_service
{
	using Json = nlohmann::json;
	inline constexpr char Build[] = "iw8-1.20.4";

	inline Json Defaults()
	{
		return { { "version", 1 }, { "build", Build }, { "maintenance", false }, { "multiplayer", true }, { "warzone", true },
			{ "patch_revision", uint32_t{ 1 } } };
	}

	inline bool Policy(const Json& j)
	{
		try
		{
			return j.is_object() && j.at("version") == 1 && j.at("build") == Build && j.at("maintenance").is_boolean() &&
				j.at("multiplayer").is_boolean() && j.at("warzone").is_boolean() && j.at("patch_revision").is_number_unsigned() &&
				j.at("patch_revision").get<uint64_t>() > 0 && j.at("patch_revision").get<uint64_t>() <= 0xFFFF;
		}
		catch (...)
		{
			return false;
		}
	}

	inline bool Digest(std::string_view s)
	{
		if (s.size() != 64) return false;
		for (char c : s)
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
		return true;
	}

	struct Reply
	{
		unsigned status;
		Json body;
	};

	inline Reply Respond(
		const Json& request, const Json& identity, const Json& policy, uint64_t utc, std::string_view playlistHash, bool linked = false)
	{
		try
		{
			if (!identity.is_object() || !identity.contains("id")) return { 401, { { "error", "authentication required" } } };
			if (request.at("version") != 1 || request.at("build") != Build ||
				(request.at("op") != "bootstrap" && request.at("op") != "local_login" && request.at("op") != "online_data" &&
					request.at("op") != "crm_read" && request.at("op") != "store_catalog" && request.at("op") != "progression" &&
					request.at("op") != "custom_mods" && request.at("op") != "validate_text" && request.at("op") != "profile" &&
					request.at("op") != "discord" && request.at("op") != "tournament" && request.at("op") != "settings" &&
					request.at("op") != "camo") ||
				!request.at("nonce").is_string() || request.at("nonce").get_ref<const std::string&>().size() > 64)
				return { 400, { { "error", "unsupported frontend request" } } };
			if (!Policy(policy) || !Digest(playlistHash) || utc < 1577836800ull || utc > 4102444800ull)
				return { 503, { { "error", "title service unavailable" } } };
			if (policy.at("maintenance").get<bool>()) return { 503, { { "error", "title service in maintenance" } } };
			if (!policy.at("multiplayer").get<bool>() && !policy.at("warzone").get<bool>())
				return { 403, { { "error", "online play disabled by host" } } };
			Reply reply{ 200,
				{ { "version", 1 }, { "build", Build }, { "nonce", request.at("nonce") },
					{ "account",
						{ { "xuid", identity.at("id") }, { "name", identity.at("name") }, { "provider", "replay" },
							{ "linked", linked || request.at("op") == "local_login" }, { "multiplayer", policy.at("multiplayer") },
							{ "warzone", policy.at("warzone") } } },
					{ "server_time", utc }, { "valid_for_ms", 15000 },
					{ "network", { { "route", "direct" }, { "relay_required", false } } },
					{ "notifications", { { "transport", "social-sync" } } },
					// This frozen build has no generated retail patch files. An empty
					// manifest is an explicit no-update result for this exact build.
					{ "patch", { { "build", Build }, { "revision", policy.at("patch_revision") }, { "files", Json::array() } } },
					{ "commerce", { { "provider", "replay" }, { "pending_exchanges", Json::array() }, { "balances", Json::array() } } },
					{ "playlists_sha256", playlistHash },
					// Loadout/configuration DDL remains local; earned progression is
					// restored separately by the persistent account service.
					{ "profile_store", "local-ddl" } } };
			if (request.at("op") == "online_data")
			{
				const auto kind = request.at("kind").get<std::string>();
				if (kind != "motd" && kind != "achievements") return { 400, { { "error", "unsupported online data" } } };
				// Native completion pages are separate from the server ledger.
				reply.body["online_data"] = { { "kind", kind }, { "records", Json::array() }, { "next_page", "" },
					{ "progression_store", "server-ledger" } };
			}
			return reply;
		}
		catch (...)
		{
			return { 400, { { "error", "malformed frontend request" } } };
		}
	}

	inline bool Validate(const Json& j, std::string_view id, std::string_view nonce)
	{
		try
		{
			if (j.at("version") != 1 || j.at("build") != Build || j.at("nonce").get<std::string>() != nonce ||
				j.at("account").at("xuid").get<std::string>() != id || j.at("account").at("provider") != "replay" ||
				!j.at("account").at("name").is_string() || j.at("account").at("name").get_ref<const std::string&>().size() > 35 ||
				!j.at("account").at("linked").is_boolean() || !j.at("account").at("multiplayer").is_boolean() ||
				!j.at("account").at("warzone").is_boolean() ||
				(!j.at("account").at("multiplayer").get<bool>() && !j.at("account").at("warzone").get<bool>()))
				return false;
			if (!j.at("server_time").is_number_unsigned()) return false;
			const auto utc = j.at("server_time").get<uint64_t>();
			return utc >= 1577836800ull && utc <= 4102444800ull && j.at("valid_for_ms") == 15000 &&
				j.at("network").at("route") == "direct" && j.at("network").at("relay_required") == false &&
				j.at("notifications").at("transport") == "social-sync" && j.at("patch").at("build") == Build &&
				j.at("patch").at("revision").is_number_unsigned() && j.at("patch").at("revision").get<uint64_t>() > 0 &&
				j.at("patch").at("revision").get<uint64_t>() <= 0xFFFF && j.at("patch").at("files") == Json::array() &&
				j.at("commerce").at("provider") == "replay" && j.at("commerce").at("pending_exchanges") == Json::array() &&
				j.at("commerce").at("balances") == Json::array() && Digest(j.at("playlists_sha256").get<std::string>()) &&
				j.at("profile_store") == "local-ddl";
		}
		catch (...)
		{
			return false;
		}
	}

	inline bool OnlineData(const Json& j, std::string_view id, std::string_view nonce, std::string_view kind)
	{
		try
		{
			return Validate(j, id, nonce) && j.at("online_data").at("kind").get<std::string>() == kind &&
				(kind == "motd" && j.at("online_data").contains("catalog_version") ? motd::Response(j.at("online_data"))
					: j.at("online_data").at("records") == Json::array()) &&
				j.at("online_data").at("next_page") == "" && j.at("online_data").at("progression_store") == "server-ledger";
		}
		catch (...)
		{
			return false;
		}
	}
}
