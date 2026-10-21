#include "backend/backend.h"
#include "network/backend/playlist_backend.h"

#include <fstream>
#include <mutex>
namespace
{
	std::mutex catalogLock;
	std::optional<playlist_catalog::Snapshot> published;
	std::string wire, mode = "auto", active, error;

	bool ValidMode(std::string_view value)
	{
		return value == "auto" || value == "tu19" || value == "tu27";
	}

	std::string SceneCatalog()
	{
#ifdef REPLAY_DISABLE_CUSTOM_CONTENT
		return "tu19";
#else
		const auto path = std::filesystem::absolute(L"mw120rproxy.online.ini");
		wchar_t scene[32]{};
		GetPrivateProfileStringW(L"ui", L"frontend_scene", L"1.16", scene, 32, path.c_str());
		return !wcscmp(scene, L"1.28") || !wcscmp(scene, L"1.28-test") || !wcscmp(scene, L"1.28-halloween") ? "tu27" : "tu19";
#endif
	}

	playlist_catalog::Snapshot Load(std::string_view selection)
	{
		if (selection == "tu19")
		{
			std::ifstream f(L"players/backend/playlists.json", std::ios::binary | std::ios::ate);
			if (!f || f.tellg() <= 0 || f.tellg() >= 0x50000) throw std::runtime_error("invalid TU19 catalog file");
			std::string text(size_t(f.tellg()), '\0');
			f.seekg(0);
			if (!f.read(text.data(), text.size())) throw std::runtime_error("short TU19 catalog read");
			return playlist_catalog::Validate(playlists::ExpandBundledCategories(text));
		}
		auto resource = FindResourceW(nullptr, MAKEINTRESOURCEW(6001), RT_RCDATA);
		auto bytes = resource ? LoadResource(nullptr, resource) : nullptr;
		if (!bytes) throw std::runtime_error("original TU27 resource missing");
		const std::string text(static_cast<const char*>(LockResource(bytes)), SizeofResource(nullptr, resource));
		// Preserve original TU27 category membership, maps, recipes and all rules.
		return playlist_catalog::Validate(text);
	}

	bool Publish(std::string_view wanted, bool save)
	{
		try
		{
			const auto selected = wanted == "auto" ? SceneCatalog() : std::string(wanted);
			auto candidate = Load(selected);
			auto encoded = playlist_catalog::Encode(candidate);
			if (save &&
				!backend120::AtomicFile(
					L"players/backend/playlist-selection.json", backend120::Json{ { "version", 1 }, { "mode", wanted } }.dump(2)))
				throw std::runtime_error("playlist selection could not be saved");
			published = std::move(candidate);
			wire = std::move(encoded);
			mode = wanted;
			active = selected;
			error.clear();
			backend120::Record("PLAYLISTS", "Publishing " + active + " (" + mode + ") sha256=" + published->sha256);
			return true;
		}
		catch (const std::exception& e)
		{
			error = e.what();
			backend120::Record("ERRORS", "Playlist selection: " + error);
			return false;
		}
	}
}

namespace playlist_backend
{
	bool Reload()
	{
		std::lock_guard lock(catalogLock);
		try
		{
			std::string selected = "auto";
			const auto path = L"players/backend/playlist-selection.json";
			if (std::filesystem::exists(path))
			{
				std::ifstream f(path);
				backend120::Json j;
				f >> j;
				if (j.at("version") != 1 || !ValidMode(j.at("mode").get<std::string>()))
					throw std::runtime_error("invalid saved playlist selection");
				selected = j.at("mode");
			}
			return Publish(selected, false);
		}
		catch (const std::exception& e)
		{
			backend120::Record("ERRORS", e.what());
			return false;
		}
	}

	match_http::Reply Request(std::string_view body)
	{
		if (!playlist_catalog::ValidRequest(body)) return { 400, R"({"error":"invalid playlist request"})" };
		std::lock_guard lock(catalogLock);
		if (!published) return { 503, R"({"error":"playlist catalog unavailable"})" };
		// Updated clients poll the hash without retransferring an unchanged file.
		const auto request = backend120::Json::parse(body);
		if (request.value("known_sha256", std::string{}) == published->sha256)
			return { 200, backend120::Json{ { "unchanged", true }, { "sha256", published->sha256 } }.dump() };
		backend120::Record("PLAYLISTS", "Served " + active + " sha256=" + published->sha256);
		return { 200, wire };
	}

	playlist_catalog::Snapshot ServerCatalog()
	{
		std::lock_guard lock(catalogLock);
		if (!published) throw std::runtime_error("playlist catalog unavailable");
		return *published;
	}
}

namespace backend120
{
	Json PlaylistStatus()
	{
		std::lock_guard lock(catalogLock);
		return { { "mode", mode }, { "active", active }, { "error", error }, { "sha256", published ? published->sha256 : "" } };
	}

	bool SetPlaylistMode(std::string_view requested)
	{
		if (!ValidMode(requested)) return false;
		const auto state = Status();
		if (state.value("playlist", -1) >= 0 || state.value("queue_players", size_t{}))
		{
			Record("ERRORS", "Finish the current match/search before changing playlists");
			return false;
		}
		std::lock_guard lock(catalogLock);
		return Publish(requested, true);
	}

	void PollPlaylistScene()
	{
		{
			std::lock_guard lock(catalogLock);
			if (mode != "auto" || active == SceneCatalog()) return;
		}
		const auto state = Status();
		if (state.value("playlist", -1) >= 0 || state.value("queue_players", size_t{})) return;
		std::lock_guard lock(catalogLock);
		if (mode == "auto" && active != SceneCatalog()) Publish("auto", false);
	}
}
