#pragma once
#include "network/match_http.h"

#include "engine/inline_hook.h"

#include <string>
#include <functional>
#include <set>
namespace social_runtime
{
	hook::Status Install(uintptr_t image, bool dedicated);
	void Shutdown();
	match_http::Reply Request(std::string_view path, std::string_view body, uint32_t peer);
	void ClientPump(const std::wstring& origin, std::string gameSession, bool searching);
	void PublishDedicated(std::string bytes, int playlist, const char* map, const char* mode, std::string endpoint, int capacity);
	bool HandleCommand(std::string_view command);
	// Authenticated worker requests; callback runs once on the client game thread.
	bool SubmitMatchmaking(std::string body, std::function<void(unsigned, std::string)> complete);
	std::string AuthorizeMatchmaking(std::string_view body, uint32_t ip);
	bool MatchmakingPartyReady(uint64_t owner, const std::set<uint64_t>& members, const std::string& party, uint64_t now);
	bool BackendConnected();
	// Dedicated chat delivery consults persistent two-way block policy.
	bool Blocked(uint64_t a, uint64_t b);
	std::string CommunicationsCredential(const std::wstring& origin);
	std::string AuthorizeCommunications(std::string_view body, uint32_t ip, uint64_t now);
	bool CommunicationsLive(uint64_t id, const std::string& lease, uint64_t now);
	bool CommunicationsAllowed(uint64_t receiver, uint64_t sender);
	match_http::Reply DiscordRequest(uint64_t owner, std::string_view request, const std::function<bool(uint64_t, int)>& admission);
	// Main-thread only, after successful authenticated capability resolution.
	bool JoinDiscord(uint64_t target, uint64_t validUntil);
	std::string PartySnapshot();
	std::string TournamentMembers(uint64_t owner, const std::set<uint64_t>& members, const std::string& party);
	// Trusted local console only. Unknown/ambiguous canonical names return zero.
	uint64_t ResolveAccountName(std::string_view name);
	// Bootstrap uses the same saved credential/lease as friends and matchmaking.
	// It is available before native platform sign-in, without reading party data.
	void Bootstrap(const std::wstring& origin);
	bool SubmitFrontend(std::string body, std::function<void(unsigned, std::string)> complete);
	void PumpFrontend();
	// Fresh measured HTTP round trip to the matchmaking backend, or -1 unavailable.
	int MatchmakingPing();
}
