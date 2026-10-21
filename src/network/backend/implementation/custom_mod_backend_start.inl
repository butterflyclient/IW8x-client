// Shared provider implementation; included inside its existing namespace.
bool Start()
{
	std::lock_guard guard(serverMutex);
	try
	{
		const auto catalog = Json::parse(embedded_assets::Find("data/server_progression_catalog.json"));
		for (const auto& row : catalog.at("tables").at("loot/weapon_ids.csv"))
			if (row.size() > 1 && custom_mods::Ref(row[1])) knownWeapons.insert(row[1].get<std::string>());
		started = !knownWeapons.empty();
		return started;
	}
	catch (...)
	{
		return false;
	}
}
