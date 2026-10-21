Answer Discord(
	uint64_t id, const Json& request, uint64_t now, const std::string& random, const std::function<bool(uint64_t, int)>& gameAdmission = {})
{
	Expire(now);
	if (!clients.contains(id)) return Error(401, "session expired");
	const auto op = request.value("op", std::string{});
	if (op == "create")
	{
		const auto* session = Resolve(id);
		const auto& client = clients.at(id);
		if (!session || (session != &game && (now < client.partySeen || now - client.partySeen > 5000)))
		{
			discordInvites.erase(id);
			return Error(409, "session is not joinable");
		}
		auto& invite = discordInvites[id];
		if (invite.session != session->id || invite.lease != client.lease || now + 60000 >= invite.expires)
		{
			if (!chat::Hex(random, 32)) return Error(503, "invite entropy unavailable");
			invite = { random, session->id, client.lease, now + 600000 };
		}
		return { 200,
			{ { "token", invite.token }, { "expires_in_ms", invite.expires - now }, { "session", session->id },
				{ "size", session->members.size() }, { "capacity", session->capacity }, { "kind", session == &game ? "game" : "party" } } };
	}
	if (op != "resolve") return Error(400, "invalid Discord operation");
	const auto token = request.value("token", std::string{});
	if (!chat::Hex(token, 32)) return Error(400, "invalid invite token");
	for (const auto& [sender, invite] : discordInvites)
		if (invite.token == token)
		{
			const auto* session = Resolve(sender);
			const auto& client = clients.at(sender);
			if (sender == id) return Error(409, "already in your own session");
			if (!session || session->id != invite.session ||
				(session != &game && (now < client.partySeen || now - client.partySeen > 5000)))
				return Error(410, "session unavailable or full");
			if (session->members.contains(id)) return Error(409, "already in this session");
			if (Blocked(id, sender) || SessionBlocked(id, session)) return Error(403, "session contains blocked player");
			if (session == &game && gameAdmission && !gameAdmission(id, session->playlist)) return Error(403, "playlist access denied");
			std::erase_if(discordGrants,
				[&](const auto& g)
				{
					return g.sender == sender && g.receiver == id;
				});
			if (discordGrants.size() >= 256) return Error(429, "invite admission busy");
			discordGrants.push_back({ sender, id, now + 120000, session->id });
			return { 200,
				{ { "target", std::to_string(sender) }, { "name", users.at(sender).name }, { "session", session->id },
					{ "kind", session == &game ? "game" : "party" }, { "expires_in_ms", 120000 } } };
		}
	return Error(410, "invite expired or unavailable");
}
