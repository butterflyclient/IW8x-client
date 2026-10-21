// Shared provider implementation; included inside its existing namespace.
bool SaveTokens()
{
	const std::filesystem::path path = L"players/native_mm_tokens.json", temp = L"players/native_mm_tokens.json.tmp";
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	if (ec) return false;
	const auto bytes = backend.SaveTokens().dump();
	{
		std::ofstream f(temp, std::ios::binary | std::ios::trunc);
		if (!f || !f.write(bytes.data(), std::streamsize(bytes.size())) || !f.flush()) return false;
	}
	return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
