// Shared provider implementation; included inside its existing namespace.
bool LoadAccounts()
{
	try
	{
		const std::filesystem::path path = L"players/backend/frontend-accounts.json";
		if (!std::filesystem::exists(path))
		{
			linkedAccounts = Json::object();
			return true;
		}
		if (std::filesystem::file_size(path) > 1024 * 1024) return false;
		std::ifstream f(path);
		Json loaded;
		f >> loaded;
		if (!loaded.is_object()) return false;
		for (const auto& [id, value] : loaded.items())
		{
			if (id.empty() || id.size() > 20 || id.find_first_not_of("0123456789") != std::string::npos || value != true) return false;
		}
		linkedAccounts = std::move(loaded);
		return true;
	}
	catch (...)
	{
		return false;
	}
}
