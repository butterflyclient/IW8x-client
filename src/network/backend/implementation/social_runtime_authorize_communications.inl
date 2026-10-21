// Shared provider implementation; included inside its existing namespace.
std::string AuthorizeCommunications(std::string_view body, uint32_t ip, uint64_t now)
{
	if (!storageReady) return {};
	try
	{
		auto request = Json::parse(body);
		request["lease"] = request.value("social_lease", std::string{});
		std::lock_guard lock(serviceLock);
		auto who = service.AuthorizeCommunications(request, ip, now);
		return who.is_object() ? who.dump() : std::string{};
	}
	catch (...)
	{
		return {};
	}
}
