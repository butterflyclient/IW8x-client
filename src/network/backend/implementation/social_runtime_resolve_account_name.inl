// Shared provider implementation; included inside its existing namespace.
uint64_t ResolveAccountName(std::string_view name)
{
	if (!server || !storageReady) return 0;
	const auto key = unlock_policy::Name(name);
	if (key.empty()) return 0;
	std::lock_guard lock(serviceLock);
	uint64_t match = 0;
	for (const auto& user : service.Status(GetTickCount64()))
		if (unlock_policy::Name(user.at("name").get<std::string>()) == key)
		{
			if (match) return 0;
			match = chat::Id(user, "xuid");
		}
	return match;
}
