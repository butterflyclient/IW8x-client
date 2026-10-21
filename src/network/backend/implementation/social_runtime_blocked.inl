// Shared provider implementation; included inside its existing namespace.
bool Blocked(uint64_t a, uint64_t b)
{
	std::lock_guard lock(serviceLock);
	return service.Blocked(a, b);
}
