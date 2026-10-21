#pragma once
#include "network/backend/tournament_service.h"
namespace tournament_backend
{
	bool Start();
	void Stop();
	unsigned Request(const nlohmann::json& identity, const nlohmann::json& request, nlohmann::json& response);
	bool Observe(const challenges::Event& event);
	void PublishRoster(const std::map<std::string, int>& teams);
	// Client methods run on the existing frontend/game thread.
	void Pump();
	bool Command(std::string_view operation);
	std::string Field(std::string_view path);
	bool Join();
}
