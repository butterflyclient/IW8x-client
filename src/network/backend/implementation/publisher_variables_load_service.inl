// Shared provider implementation; included inside its existing namespace.
bool LoadService(bool reload)
{
	const std::filesystem::path path = L"players/publisher_variables.json";
	try
	{
		std::error_code ec;
		const bool exists = std::filesystem::exists(path, ec);
		if (ec) return false;
		Json doc = publisher::Backend{}.Document();
		if (exists)
		{
			const auto size = std::filesystem::file_size(path);
			if (size > 524288) return false;
			std::ifstream file(path, std::ios::binary);
			if (!file) return false;
			file >> doc;
		}
		std::lock_guard lock(backendLock);
		if (!backend.Load(doc, reload)) return false;
		storageReady = true;
		return true;
	}
	catch (...)
	{
		return false;
	}
}
