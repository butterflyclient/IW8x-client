// Shared provider implementation; included inside its existing namespace.
bool ServerPlaylistAllowed(int playlist)
{
	if (playlist < 0 || playlist >= 129) return false;
	std::lock_guard lock(serviceLock);
	if (!serviceReady || !accountStoreReady || policy.value("maintenance", true)) return false;
	try
	{
		const auto catalog = playlist_backend::ServerCatalog();
		const auto info = playlists::ValidateTransferDocument(catalog.text);
		return info.ids[playlist] && policy.at(info.multiplayer[playlist] ? "multiplayer" : "warzone").get<bool>();
	}
	catch (...)
	{
		return false;
	}
}
