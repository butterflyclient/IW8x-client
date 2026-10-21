#pragma once
#include "network/match_http.h"
#include "thirdparty/json.hpp"

#include <filesystem>
#include <vector>
namespace backend120
{
	using Json = nlohmann::json;

	struct Log
	{
		uint64_t sequence;
		std::string time, category, text;
	};

	void Record(std::string category, std::string text);
	std::vector<Log> Logs(uint64_t after = 0);
	bool Start(uint16_t httpPort = 27128, uint16_t discoveryPort = 27126);
	void Stop();
	bool Running();
	Json Status();
	Json Players();
	bool Command(std::string_view command);
	bool SetPlaylistMode(std::string_view mode);
	Json PlaylistStatus();
	void PollPlaylistScene();
	bool ServicesStart();
	void ServicesStop();
	void PublishHost(const Json& host);
	uint64_t HostId();
	Json CamoStatus(uint64_t owner);
	match_http::Reply Request(std::string_view path, std::string_view body, uint32_t peer);
	std::string Random(size_t bytes = 16);
	bool AtomicFile(const std::filesystem::path& path, std::string_view bytes);
}
