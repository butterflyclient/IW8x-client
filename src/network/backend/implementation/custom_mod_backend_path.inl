// Shared provider implementation; included inside its existing namespace.
std::filesystem::path Path(uint64_t id)
{
	return std::filesystem::path(L"players/backend/custom_mods") / (std::to_wstring(id) + L".json");
}
