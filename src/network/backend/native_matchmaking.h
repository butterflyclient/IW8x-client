#pragma once
#include "engine/inline_hook.h"

#include "network/match_http.h"

#include <string>
namespace native_matchmaking
{
	hook::Status Install(uintptr_t base, bool dedicated);
	void Configure(bool enabled);
	bool Enabled();
	bool UseOnlineLobby();
	uintptr_t ServerPartyRVA();
	bool InMatch();
	bool Joining();
	bool StartCountdown(int& milliseconds);
	std::string GameSession();
	bool Searching();
	void Pump();
	void ServerPump();
	void Cancel();
	bool Command(std::string_view command);
	match_http::Reply Request(std::string_view body, uint32_t ip);
}
