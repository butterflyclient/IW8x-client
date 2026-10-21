// Shared provider implementation; included inside its existing namespace.
bool Load()
{
	try
	{
		const std::filesystem::path path = L"players/backend/text_validation.json";
		auto candidate = text_validation::Defaults();
		if (std::filesystem::exists(path))
		{
			if (std::filesystem::file_size(path) > 65536) return false;
			std::ifstream f(path);
			f >> candidate;
		}
		else
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream f(path, std::ios::binary);
			f << candidate.dump(2);
			f.flush();
			if (!f) return false;
		}
		if (!text_validation::Policy(candidate)) return false;
		std::lock_guard guard(mutex);
		policy = std::move(candidate);
		loaded = true;
		return true;
	}
	catch (...)
	{
		return false;
	}
}
