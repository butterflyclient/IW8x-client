// Shared provider implementation; included inside its existing namespace.
playlist_catalog::Snapshot ServerCatalog()
{
	std::lock_guard guard(lock);
	if (!server || !service) throw std::runtime_error("dedicated backend playlist catalog unavailable");
	return *service;
}
