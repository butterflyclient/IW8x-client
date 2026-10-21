// Shared provider implementation; included inside its existing namespace.
bool LoadMotdCatalog()
{
	try
	{
		const std::filesystem::path path = L"players/backend/motd.json";
		Json value = motd::Defaults();
		if (std::filesystem::exists(path))
		{
			if (std::filesystem::file_size(path) > 65536) return false;
			std::ifstream f(path);
			if (!f) return false;
			f >> value;
		}
		if (!motd::Catalog(value)) return false;
		std::lock_guard lock(serviceLock);
		if (motdCatalogReady && value != motdCatalog && value.at("revision") <= motdCatalog.at("revision")) return false;
		motdCatalog = std::move(value);
		motdCatalogReady = true;
		return true;
	}
	catch (...)
	{
		return false;
	}
}
