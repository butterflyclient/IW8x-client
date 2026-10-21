// Shared provider implementation; included inside its existing namespace.
bool SaveAccounts(const Json& accounts)
{
	try
	{
		const std::filesystem::path path = L"players/backend/frontend-accounts.json";
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) return false;
		auto temp = path;
		temp += L".tmp";
		{
			std::ofstream f(temp, std::ios::binary | std::ios::trunc);
			f << accounts.dump(2);
			f.flush();
			if (!f) return false;
		}
		return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
	}
	catch (...)
	{
		return false;
	}
}
