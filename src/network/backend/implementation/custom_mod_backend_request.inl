// Shared provider implementation; included inside its existing namespace.
unsigned Request(uint64_t id, const Json& request, Json& response)
{
	std::lock_guard guard(serverMutex);
	if (!started || !id) return 503;
	try
	{
		auto& account = Load(id);
		if (account.damaged) return 503;
		const auto action = request.value("action", std::string{});
		unsigned status = 200;
		if (action != "sync")
		{
			auto candidate = account.doc;
			std::string result;
			status = custom_mods::Apply(candidate, request, knownWeapons, result);
			if (status == 200 && candidate != account.doc && action == "save")
			{
				const auto name = request.at("mod").at("blueprintName").get<std::string>();
				const auto verdict = name_validation::Check(name.size() >= 3 ? std::string_view(name).substr(3) : std::string_view{});
				if (verdict == "unavailable") return 503;
				if (verdict != "allowed")
				{
					response["custom_mods"] = account.doc;
					response["custom_mod_result"] = { { "transaction", request.value("transaction", std::string{}) },
						{ "status", "invalid_name" } };
					return 422;
				}
			}
			if (status == 200 && candidate != account.doc)
			{
				if (!Write(Path(id), candidate, true)) return 503;
				account.doc = std::move(candidate);
				LOG_INFO(
					"CustomMods", "%s committed xuid=%llu revision=%llu", action.c_str(), id, account.doc.at("revision").get<uint64_t>());
			}
			response["custom_mod_result"] = { { "transaction", request.value("transaction", std::string{}) }, { "status", result } };
		}
		response["custom_mods"] = account.doc;
		return status;
	}
	catch (...)
	{
		return 400;
	}
}
