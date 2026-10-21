// Shared provider implementation; included inside its existing namespace.
bool CommunicationsAllowed(uint64_t receiver, uint64_t sender)
{
	std::lock_guard lock(serviceLock);
	return storageReady && service.CommunicationsAllowed(receiver, sender);
}
