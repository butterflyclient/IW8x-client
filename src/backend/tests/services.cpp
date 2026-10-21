#include "backend/backend.h"
#include "common/embedded_assets.h"
#include "network/backend/playlist_catalog.h"
#include "network/backend/frontend_backend.h"

#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace
{
	unsigned checks{};

	void require(bool condition, const char* message)
	{
		++checks;
		if (!condition) throw std::runtime_error(message);
	}
}

int main()
{
	const auto original = std::filesystem::current_path();
	const auto root = std::filesystem::temp_directory_path() / ("iw8x-backend-merge-" + std::to_string(GetCurrentProcessId()));
	try
	{
		std::filesystem::create_directories(root);
		std::filesystem::current_path(root);
		require(embedded_assets::PrepareNativeMetadata(GetModuleHandleW(nullptr)), "initialize embedded resource provider");
		const auto http = uint16_t(49152 + (GetCurrentProcessId() % 14000));
		const auto udp = uint16_t(http + 1);
		require(backend120::Start(http, udp), "start standalone backend in isolated test directory");
		require(backend120::Running(), "backend running state");
		const auto origin = L"http://127.0.0.1:" + std::to_wstring(http);
		require(match_http::Post(origin, L"/health", "{}").status == 200, "HTTP health endpoint");
		const auto request = backend120::Json{ { "protocol", 1 }, { "build", playlist_catalog::kBuild } }.dump();
		const auto reply = match_http::Post(origin, L"/v1/playlists", request);
		require(reply.status == 200, "HTTP playlist endpoint");
		const auto tu19 = playlist_catalog::Decode(reply.body);
		require(!tu19.text.empty() && tu19.sha256.size() == 64, "TU19 published catalog validates");
		require(backend120::SetPlaylistMode("tu27"), "select TU27 backend catalog");
		const auto tu27 = playlist_catalog::Decode(match_http::Post(origin, L"/v1/playlists", request).body);
		require(tu27.sha256 != tu19.sha256, "TU27 catalog switches atomically");
		require(!backend120::SetPlaylistMode("invalid"), "invalid catalog mode rejected");
		require(playlist_catalog::Decode(match_http::Post(origin, L"/v1/playlists", request).body).sha256 == tu27.sha256,
			"invalid selection preserves published catalog");
		require(match_http::Post(origin, L"/v1/frontend", "malformed").status == 401,
			"frontend authenticates before parsing untrusted requests");
		const std::string oversized(16385, 'x');
		const auto rejected = match_http::Post(origin, L"/v1/frontend", oversized);
		require(rejected.status == 0 && rejected.body == "invalid HTTP request size",
			"HTTP transport rejects oversized request before sending");
		require(frontend_backend::Request(oversized, 0).status == 400,
			"frontend provider rejects oversized request before authorization");
		require(match_http::Post(origin, L"/unknown", "{}").status == 404, "unknown endpoint rejected");
		require(match_http::Post(origin, L"/v1/native-mm", "{}").status >= 400, "unauthenticated native matchmaking rejected");
		require(match_http::Post(origin, L"/v1/social/friends", "{}").status >= 400, "unauthenticated social request rejected");
		backend120::Stop();
		require(!backend120::Running(), "backend stops and releases listeners");
		require(backend120::Start(http, udp), "backend restarts using retained service data");
		require(playlist_catalog::Decode(match_http::Post(origin, L"/v1/playlists", request).body).sha256 == tu27.sha256,
			"playlist selection survives backend restart");
		backend120::Stop();
		std::filesystem::current_path(original);
		std::filesystem::remove_all(root);
		std::printf("Standalone backend checks passed: %u\n", checks);
		return 0;
	}
	catch (const std::exception& error)
	{
		backend120::Stop();
		std::filesystem::current_path(original);
		std::fprintf(stderr, "Standalone backend check failed: %s; retained evidence: %ls\n", error.what(), root.c_str());
		return 1;
	}
}
