// Shared provider implementation; included inside its existing namespace.
match_http::Reply Request(std::string_view body, uint32_t peer)
{
	try
	{
		if (body.size() > 16384) return { 400, "{\"error\":\"request too large\"}" };
		auto who = social_runtime::AuthorizeMatchmaking(body, peer);
		if (who.empty()) return { 401, "{\"error\":\"authentication required\"}" };
		std::lock_guard lock(serviceLock);
		if (!serviceReady) return { 503, "{\"error\":\"frontend configuration unavailable\"}" };
		const auto catalog = playlist_backend::ServerCatalog();
		if (!accountStoreReady) return { 503, "{\"error\":\"account storage unavailable\"}" };
		const auto identity = Json::parse(who), request = Json::parse(body);
		const auto id = identity.at("id").get<std::string>();
		auto reply = frontend_service::Respond(
			request, identity, policy, uint64_t(std::time(nullptr)), catalog.sha256, linkedAccounts.value(id, false));
		if (reply.status == 200 && request.at("op") == "tournament")
		{
			const auto info = playlists::ValidateTransferDocument(catalog.text);
			if (!policy.at("multiplayer").get<bool>() || !info.ids[tournament120::Playlist])
				reply.status = 403;
			else
				reply.status = tournament_backend::Request(identity, request, reply.body);
		}
		if (reply.status == 200 && request.at("op") == "camo") reply.body["camo"] = camo_changer::ServerStatus(chat::Id(identity, "id"));
		if (reply.status == 200 && request.at("op") == "discord")
		{
			const auto info = playlists::ValidateTransferDocument(catalog.text);
			auto action = request;
			action["op"] = request.at("action");
			const auto result = social_runtime::DiscordRequest(chat::Id(identity, "id"), action.dump(),
				[&](uint64_t, int playlist)
				{
					return playlist >= 0 && playlist < 129 && info.ids[playlist] &&
						policy.at(info.multiplayer[playlist] ? "multiplayer" : "warzone").get<bool>();
				});
			reply.status = result.status;
			reply.body["discord"] = Json::parse(result.body);
		}
		if (reply.status == 200 &&
			(request.at("op") == "progression" || request.at("op") == "custom_mods" || request.at("op") == "profile" ||
				request.at("op") == "settings"))
		{
			uint64_t xuid = 0;
			const auto parsed = std::from_chars(id.data(), id.data() + id.size(), xuid);
			if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size() || !xuid) return { 400, "{}" };
			reply.status = request.at("op") == "settings" ? settings_backend::Request(xuid, request, reply.body)
				: request.at("op") == "profile"			  ? profile_backend::Request(xuid, request, reply.body)
				: request.at("op") == "custom_mods"		  ? custom_mod_backend::Request(xuid, request, reply.body)
														  : progression_backend::Request(xuid, request, reply.body);
		}
		if (reply.status == 200 && request.at("op") == "store_catalog" && !store_backend::Reply(reply.body))
			return { 503, "{\"error\":\"store catalog unavailable\"}" };
		if (reply.status == 200 && request.at("op") == "validate_text") reply.status = name_validation::Request(request, reply.body);
		if (reply.status == 200 && request.at("op") == "online_data" && request.at("kind") == "motd" &&
			request.value("motd_catalog", 0) == 1)
		{
			if (!motdCatalogReady) return { 503, "{\"error\":\"MOTD catalog unavailable\"}" };
			const auto locale = request.value("locale", std::string("en"));
			if (locale.empty() || locale.size() > 8) return { 400, "{\"error\":\"invalid MOTD locale\"}" };
			reply.body["online_data"]["catalog_version"] = 1;
			reply.body["online_data"]["revision"] = motdCatalog.at("revision");
			reply.body["online_data"]["records"] = motd::Select(motdCatalog, uint64_t(std::time(nullptr)), locale);
		}
		if (reply.status == 200 && request.at("op") == "crm_read")
		{
			if (!motdCatalogReady || !motdReadsReady) return { 503, "{\"error\":\"MOTD read storage unavailable\"}" };
			const auto message = request.at("message_id").get<std::string>();
			if (!motd::Unsigned(request.at("revision"), 0xFFFFFFFFull) || request.at("revision") != motdCatalog.at("revision"))
				return { 409, "{\"error\":\"MOTD revision changed\"}" };
			bool found = false;
			for (const auto& row : motdCatalog.at("messages"))
				if (row.at("id") == message) found = true;
			if (!found) return { 404, "{\"error\":\"unknown MOTD message\"}" };
			if (!SaveMotdRead(id, message, request.at("revision").get<uint32_t>()))
				return { 503, "{\"error\":\"MOTD read write failed\"}" };
			reply.body["crm_read"] = { { "message_id", message }, { "recorded", true } };
		}
		if (reply.status == 200 && request.at("op") == "local_login" && !linkedAccounts.value(id, false))
		{
			auto updated = linkedAccounts;
			updated[id] = true;
			if (!SaveAccounts(updated)) return { 503, "{\"error\":\"account storage write failed\"}" };
			linkedAccounts = std::move(updated);
		}
		return { reply.status, reply.body.dump() };
	}
	catch (...)
	{
		return { 400, "{\"error\":\"malformed frontend request\"}" };
	}
}
