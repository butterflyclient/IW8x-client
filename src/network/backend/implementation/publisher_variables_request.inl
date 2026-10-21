// Shared provider implementation; included inside its existing namespace.
match_http::Reply Request(std::string_view bytes)
{
	try
	{
		if (bytes.size() > 4096) return { 400, "{\"error\":\"publisher request too large\"}" };
		auto request = Json::parse(bytes);
		std::lock_guard lock(backendLock);
		if (!storageReady) return { 503, "{\"error\":\"publisher configuration unavailable\"}" };
		auto reply = backend.Handle(request);
		return { reply.status, reply.body.dump() };
	}
	catch (...)
	{
		return { 400, "{\"error\":\"malformed publisher request\"}" };
	}
}
