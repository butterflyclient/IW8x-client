#pragma once
#include "auth/profile/challenge_events.h"
#include "network/social/chat_backend.h"

#include <string>
namespace backend_agent
{
	bool Enabled();
	bool Start(const std::wstring& origin);
	void Stop();
	void Publish(nlohmann::json state);
	void Roster(const std::vector<chat::Player>& players);
	bool TakePlaylist(int& playlist);
	void Reset();
	void Reject(int playlist);
	int Team(uint64_t player);
	void Event(const challenges::Event& event);
	int Owns(uint64_t player, int item);
	bool Command(std::string_view command);
	// Local automatic hosting launches the backend independently of the game.
	bool EnsureLocal();
}
