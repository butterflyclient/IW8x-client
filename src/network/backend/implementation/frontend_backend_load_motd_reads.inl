// Shared provider implementation; included inside its existing namespace.
bool LoadMotdReads()
{
	try
	{
		const std::filesystem::path path = L"players/backend/motd-reads.json";
		if (!std::filesystem::exists(path))
		{
			motdReads = Json::object();
			return true;
		}
		if (std::filesystem::file_size(path) > 16 * 1024 * 1024) return false;
		std::ifstream f(path);
		Json value;
		f >> value;
		if (!value.is_object()) return false;
		for (const auto& [user, rows] : value.items())
		{
			if (user.empty() || user.size() > 20 || user.find_first_not_of("0123456789") != user.npos || !rows.is_object() ||
				rows.size() > 256)
				return false;
			for (const auto& [key, at] : rows.items())
				if (key.size() > 32 || key.find_first_not_of("0123456789:") != key.npos || !motd::Unsigned(at, 4102444800ull)) return false;
		}
		motdReads = std::move(value);
		return true;
	}
	catch (...)
	{
		return false;
	}
}
