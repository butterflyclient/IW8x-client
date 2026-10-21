// Shared provider implementation; included inside its existing namespace.
bool LoadPolicy()
{
	try
	{
		Json value = frontend_service::Defaults();
		std::error_code ec;
		const std::filesystem::path path = L"players/backend/frontend.json";
		const bool exists = std::filesystem::exists(path, ec);
		if (ec) return false;
		if (exists)
		{
			if (std::filesystem::file_size(path) > 16384) return false;
			std::ifstream f(path);
			if (!f) return false;
			f >> value;
		}
		if (!frontend_service::Policy(value)) return false;
		std::lock_guard lock(serviceLock);
		policy = std::move(value);
		serviceReady = true;
		return true;
	}
	catch (...)
	{
		return false;
	}
}
