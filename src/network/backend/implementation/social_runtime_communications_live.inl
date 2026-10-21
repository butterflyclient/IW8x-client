// Shared provider implementation; included inside its existing namespace.
bool CommunicationsLive(uint64_t id, const std::string& lease, uint64_t now)
{
	std::lock_guard lock(serviceLock);
	return storageReady && service.CommunicationsLive(id, lease, now);
}
