// Shared provider implementation; included inside its existing namespace.
match_http::Reply Request(std::string_view path, std::string_view bytes, uint32_t ip)
{
	if (!storageReady) return { 503, "{\"error\":\"social storage unavailable\"}" };
	try
	{
		if (bytes.size() > 16384) return { 400, "{}" };
		auto body = Json::parse(bytes);
		std::lock_guard lock(serviceLock);
		if (path == "/v1/party-relay")
		{
			auto reply = relayService.Handle(body, ip, GetTickCount64(), service);
			reply.body["title"] = "mw2019-tu19";
			return { reply.status, reply.body.dump() };
		}
		// Only durable mutations need a rollback snapshot; polls must not copy
		// every user's subscription/inbox graph twice a second.
		std::optional<social::Backend> before;
		if (path.ends_with("/open") || path.ends_with("/request") || path.ends_with("/remove") || path.ends_with("/availability") ||
			(path == "/v1/social/userlist" && body.value("op", std::string{}) != "fetch"))
			before = service;
		const auto utc =
			uint64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		auto r = service.Handle(path, body, ip, GetTickCount64(), path == "/v1/social/open" ? random() : std::string{}, utc);
		if (r.status == 200 && path == "/v1/social/sync")
		{
			FILETIME ft{};
			GetSystemTimeAsFileTime(&ft);
			ULARGE_INTEGER ticks{};
			ticks.LowPart = ft.dwLowDateTime;
			ticks.HighPart = ft.dwHighDateTime;
			r.body["server_time"] = (ticks.QuadPart - 116444736000000000ull) / 10000000ull;
		}
		if (r.status >= 400 && path != "/v1/social/sync")
			LOG_WARN("Social", "request %.*s user=%llu failed status=%u reason=%s", int(path.size()), path.data(), chat::Id(body, "xuid"),
				r.status, r.body.value("error", std::string{}).c_str());
		if (!save_service())
		{
			if (before) service = std::move(*before);
			return { 503, "{\"error\":\"social persistence failed\"}" };
		}
		return { r.status, r.body.dump() };
	}
	catch (...)
	{
		return { 400, "{\"error\":\"malformed social request\"}" };
	}
}
