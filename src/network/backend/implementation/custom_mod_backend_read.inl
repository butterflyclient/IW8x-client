// Shared provider implementation; included inside its existing namespace.
bool Read(const std::filesystem::path& path, Json& doc)
{
	try
	{
		if (std::filesystem::file_size(path) > 2 * 1024 * 1024) return false;
		std::ifstream f(path, std::ios::binary);
		Json wrapper;
		f >> wrapper;
		const auto bytes = wrapper.at("payload").get<std::string>();
		auto candidate = Json::parse(bytes);
		if (wrapper.at("checksum") != std::to_string(challenges::Hash(bytes)) || !custom_mods::Document(candidate)) return false;
		doc = std::move(candidate);
		return true;
	}
	catch (...)
	{
		return false;
	}
}
