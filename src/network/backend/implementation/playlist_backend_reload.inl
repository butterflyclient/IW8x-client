// Shared provider implementation; included inside its existing namespace.
bool Reload()
{
	{
		std::lock_guard guard(lock);
		if (!server) return false;
	}
	try
	{
		// This path belongs to the dedicated service. Clients never read it.
		const std::filesystem::path path = L"players/backend/playlists.json";
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) throw std::runtime_error("missing players/backend/playlists.json");
		const auto size = file.tellg();
		if (size <= 0 || size >= 0x50000) throw std::runtime_error("backend playlist file exceeds native capacity");
		std::string text(static_cast<size_t>(size), '\0');
		file.seekg(0);
		if (!file.read(text.data(), size)) throw std::runtime_error("short backend playlist read");
		playlists::ValidateDocument(text);
		auto catalog = playlist_catalog::Validate(playlists::ExpandBundledCategories(text));
		auto wire = playlist_catalog::Encode(catalog);
		std::lock_guard guard(lock);
		LOG_INFO("PlaylistBackend", "dedicated catalog loaded: bytes=%zu version=%d sha256=%s", catalog.text.size(), catalog.version,
			catalog.sha256.c_str());
		service = std::move(catalog);
		serviceWire = std::move(wire);
		lastError.clear();
		return true;
	}
	catch (const std::exception& e)
	{
		std::lock_guard guard(lock);
		lastError = e.what();
		LOG_ERR("PlaylistBackend", "catalog unavailable: %s", lastError.c_str());
		return false;
	}
}
