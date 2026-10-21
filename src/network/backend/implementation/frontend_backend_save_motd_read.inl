// Shared provider implementation; included inside its existing namespace.
bool SaveMotdRead(const std::string& user, const std::string& id, uint32_t revision)
{
	if (!motdReadsReady) return false;
	try
	{
		const auto key = id + ":" + std::to_string(revision);
		if (motdReads.contains(user) && motdReads[user].contains(key)) return true;
		auto updated = motdReads;
		if (!updated.contains(user)) updated[user] = Json::object();
		auto& rows = updated[user];
		if (rows.size() >= 256)
		{
			auto oldest = rows.begin();
			for (auto i = rows.begin(); i != rows.end(); ++i)
				if (i.value() < oldest.value()) oldest = i;
			rows.erase(oldest);
		}
		rows[key] = uint64_t(std::time(nullptr));
		const auto bytes = updated.dump();
		if (bytes.size() > 16 * 1024 * 1024) return false;
		const std::filesystem::path path = L"players/backend/motd-reads.json";
		std::filesystem::create_directories(path.parent_path());
		auto temp = path;
		temp += L".tmp";
		{
			std::ofstream f(temp, std::ios::binary | std::ios::trunc);
			f << bytes;
			f.flush();
			if (!f) return false;
		}
		if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
		motdReads = std::move(updated);
		return true;
	}
	catch (...)
	{
		return false;
	}
}
