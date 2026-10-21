// Shared provider implementation; included inside its existing namespace.
bool Write(const std::filesystem::path& path, const Json& value, bool backup)
{
	try
	{
		std::filesystem::create_directories(path.parent_path());
		const auto payload = value.dump();
		const auto bytes =
			backup ? Json{ { "payload", payload }, { "checksum", std::to_string(challenges::Hash(payload)) } }.dump() : payload;
		const auto temporary = path.wstring() + L".tmp";
		HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		DWORD count = 0;
		const bool ok =
			WriteFile(file, bytes.data(), DWORD(bytes.size()), &count, nullptr) && count == bytes.size() && FlushFileBuffers(file);
		CloseHandle(file);
		if (!ok) return false;
		if (backup && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
			return ReplaceFileW(path.c_str(), temporary.c_str(), (path.wstring() + L".bak").c_str(), 0, nullptr, nullptr) != FALSE;
		return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
	}
	catch (...)
	{
		return false;
	}
}
