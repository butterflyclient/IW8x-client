#pragma once
#include "network/match_http.h"
#include "network/backend/playlist_catalog.h"

#include <optional>
namespace playlist_backend
{
	void Configure(bool dedicated, const std::wstring& origin);
	void Shutdown();
	match_http::Reply Request(std::string_view body);
	// Dedicated-only disk read. A failed load never synthesizes a catalog.
	bool Reload();
	playlist_catalog::Snapshot ServerCatalog();
	// Client worker owns HTTP and validation; only the game thread applies results.
	std::optional<playlist_catalog::Snapshot> Take();
	void Refresh();
	bool Current(std::string_view hash);
	void LogStatus();
}
