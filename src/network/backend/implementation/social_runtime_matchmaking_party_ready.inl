// Shared provider implementation; included inside its existing namespace.
bool MatchmakingPartyReady(uint64_t owner, const std::set<uint64_t>& members, const std::string& party, uint64_t now)
{
	std::lock_guard lock(serviceLock);
	if (!storageReady || !service.MatchmakingPartyReady(owner, members, party, now)) return false;
	for (auto a : members)
		for (auto b : members)
			if (service.Blocked(a, b)) return false;
	return true;
}
