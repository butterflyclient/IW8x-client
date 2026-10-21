// Shared provider implementation; included inside its existing namespace.
Account& Load(uint64_t id)
{
	auto [it, added] = accounts.try_emplace(id);
	auto& account = it->second;
	if (!added) return account;
	const auto path = Path(id);
	const auto backup = path.wstring() + L".bak";
	if (std::filesystem::exists(path) || std::filesystem::exists(backup))
	{
		if (!Read(path, account.doc))
		{
			if (Read(backup, account.doc))
			{
				if (std::filesystem::exists(path) &&
					!MoveFileExW(
						path.c_str(), (path.wstring() + L".rejected." + std::to_wstring(GetTickCount64())).c_str(), MOVEFILE_WRITE_THROUGH))
					account.damaged = true;
				LOG_WARN("CustomMods", "validated backup restored xuid=%llu", id);
			}
			else
				account.damaged = true;
		}
	}
	return account;
}
