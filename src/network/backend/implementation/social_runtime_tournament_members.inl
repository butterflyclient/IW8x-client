// Shared provider implementation; included inside its existing namespace.
std::string TournamentMembers(uint64_t owner, const std::set<uint64_t>& members, const std::string& party)
{
	std::lock_guard lock(serviceLock);
	return storageReady ? service.TournamentMembers(owner, members, party, GetTickCount64()).dump() : "[]";
}
