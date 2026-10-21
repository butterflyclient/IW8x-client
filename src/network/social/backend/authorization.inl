Json AuthorizeCommunications(const Json& request, uint32_t ip, uint64_t now)
{
	Expire(now);
	const auto id = Id(request, "xuid");
	auto c = clients.find(id);
	if (request.value("version", 0) != 1 || c == clients.end() || c->second.ip != ip ||
		request.value("lease", std::string{}) != c->second.lease)
		return {};
	c->second.seen = now;
	return { { "id", std::to_string(id) }, { "name", users.at(id).name } };
}

Json AuthorizeMatchmaking(const Json& request, uint32_t ip, uint64_t now)
{
	Expire(now);
	const auto id = Id(request, "xuid");
	auto c = clients.find(id);
	if (request.value("version", 0) != 1 || c == clients.end() || c->second.ip != ip ||
		request.value("lease", std::string{}) != c->second.lease)
		return {};
	c->second.seen = now;
	Json members = Json::array({ std::to_string(id) });
	if (c->second.host == id && !c->second.party.empty())
		for (auto member : c->second.members)
		{
			auto other = clients.find(member);
			if (member != id && other != clients.end() && other->second.host == id && other->second.party == c->second.party &&
				other->second.members.contains(id))
				members.push_back(std::to_string(member));
		}
	for (const auto& a : members)
		for (const auto& b : members)
			if (Blocked(Id(Json{ { "id", a } }, "id"), Id(Json{ { "id", b } }, "id"))) return {};
	return { { "id", std::to_string(id) }, { "name", users.at(id).name }, { "members", members }, { "party", c->second.party } };
}
