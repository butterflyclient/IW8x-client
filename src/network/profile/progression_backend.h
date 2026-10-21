#pragma once
#include "auth/profile/challenge_events.h"
#include "thirdparty/json.hpp"
namespace progression_backend
{
	bool Start();
	bool Commit(const challenges::Event& event);
	std::vector<challenges::Event> CommitBatch(const std::vector<challenges::Event>& events);
	int Owns(uint64_t player, int item);
	bool TournamentWin(uint64_t xuid, int wins);
	void Stop();
	void Receive(const challenges::Event& event);
	unsigned Request(uint64_t xuid, const nlohmann::json& request, nlohmann::json& response);
	bool Command(std::string_view command, bool server);
	// Simulation-thread check: no disk IO and no waiting on account transactions.
	// -1 unavailable/busy, 0 not owned, 1 owned.
	int TryOwns(uint64_t xuid, int item);
}
