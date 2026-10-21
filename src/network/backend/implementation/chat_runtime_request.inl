// Shared provider implementation; included inside its existing namespace.
match_http::Reply Request(std::string_view path, std::string_view body, uint32_t ip)
{
	try
	{
		if (body.size() > 16384) return { 400, "{}" };
		auto request = Json::parse(body);
		std::lock_guard guard(serviceLock);
		service.blocked = [](uint64_t receiver, uint64_t sender)
		{
			return !social_runtime::CommunicationsAllowed(receiver, sender);
		};
		service.authorize = [](const Json& j, uint32_t peer, uint64_t now)
		{
			return Json::parse(social_runtime::AuthorizeCommunications(j.dump(), peer, now), nullptr, false);
		};
		service.live = social_runtime::CommunicationsLive;
		service.global = true;
		auto answer = service.Handle(path, request, ip, GetTickCount64(), path == "/v1/chat/open" ? random() : std::string{});
		return { answer.status, answer.body.dump() };
	}
	catch (...)
	{
		return { 400, "{\"error\":\"malformed chat request\"}" };
	}
}
