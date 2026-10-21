// Shared provider implementation; included inside its existing namespace.
match_http::Reply DiscordRequest(uint64_t owner, std::string_view text, const std::function<bool(uint64_t, int)>& admission)
{
	try
	{
		auto request = Json::parse(text);
		std::lock_guard lock(serviceLock);
		if (!storageReady) return { 503, "{}" };
		auto reply = service.Discord(
			owner, request, GetTickCount64(), request.value("op", std::string{}) == "create" ? random() : std::string{}, admission);
		return { reply.status, reply.body.dump() };
	}
	catch (...)
	{
		return { 400, "{}" };
	}
}
