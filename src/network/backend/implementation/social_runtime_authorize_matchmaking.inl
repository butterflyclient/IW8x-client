// Shared provider implementation; included inside its existing namespace.
std::string AuthorizeMatchmaking(std::string_view body, uint32_t ip)
{
	if (!storageReady) return {};
	try
	{
		auto request = Json::parse(body);
		std::lock_guard lock(serviceLock);
		auto who = service.AuthorizeMatchmaking(request, ip, GetTickCount64());
		return who.is_object() ? who.dump() : std::string{};
	}
	catch (...)
	{
		return {};
	}
}
