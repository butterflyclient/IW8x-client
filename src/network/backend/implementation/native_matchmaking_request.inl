// Shared provider implementation; included inside its existing namespace.
match_http::Reply Request(std::string_view body, uint32_t ip)
{
	if (!server || body.size() > 32768) return { 400, "{}" };
	if (!tokenStorageReady) return { 503, "{\"error\":\"matchmaking token storage unavailable\"}" };
	try
	{
		const auto identity = social_runtime::AuthorizeMatchmaking(body, ip);
		if (identity.empty()) return { 401, "{}" };
		const auto j = Json::parse(identity);
		native_mm::Principal who;
		who.id = chat::Id(j, "id");
		who.name = j.at("name");
		who.party = j.at("party");
		for (const auto& member : j.at("members"))
			who.members.insert(native_mm::Number(member));
		uint64_t random = 0;
		if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&random), 8, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return { 503, "{}" };
		auto request = Json::parse(body);
		std::lock_guard lock(backendLock);
		auto result = backend.Handle(
			who, request, GetTickCount64(), random, online_services::MatchmakingHostId(),
			[ip](const match_queue::Packet& query)
			{
				return online_services::AllocateNativeMatch(query, ip);
			},
			[](const native_mm::Principal& p, uint64_t now)
			{
				return social_runtime::MatchmakingPartyReady(p.id, p.members, p.party, now);
			},
			[](const native_mm::Principal&, int playlist)
			{
				return frontend_backend::ServerPlaylistAllowed(playlist);
			});
		// Native caches its token across end_server/process recycling. Persist
		// it before acknowledging issuance; searches/leases remain ephemeral.
		if (result.status == 200 && request.value("op", std::string{}) == "token" && !SaveTokens())
			return { 503, "{\"error\":\"matchmaking token persistence failed\"}" };
		if (result.status != 200)
			LOG_WARN("NativeMM", "service op=%s status=%u reason=%s", request.value("op", std::string{}).c_str(), result.status,
				result.body.dump().c_str());
		return { result.status, result.body.dump() };
	}
	catch (...)
	{
		return { 400, "{}" };
	}
}
