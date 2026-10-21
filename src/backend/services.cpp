// Engine-independent service assembly. Request bodies are shared with the DLL
// through src/services; no native hooks or game entrypoints are linked here.
#include "backend/backend.h"
#include "network/social/social_runtime.h"
#include "network/social/social_backend.h"
#include "network/social/party_relay.h"
#include "network/commerce/unlock_policy.h"
#include "network/social/chat_runtime.h"
#include "network/social/chat_backend.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "network/backend/playlist_backend.h"
#include "network/playlist_document.h"
#include "network/backend/publisher_variables.h"
#include "network/backend/publisher_backend.h"
#include "network/backend/native_matchmaking.h"
#include "network/backend/native_mm_backend.h"
#include "network/online_services.h"
#include "network/backend/tournament_backend.h"
#include "network/profile/progression_backend.h"
#include "network/profile/profile_backend.h"
#include "network/profile/settings_backend.h"
#include "network/commerce/custom_mod_backend.h"
#include "network/backend/name_validation.h"
#include "network/commerce/store_backend.h"
#include "engine/camo_changer.h"
#include "common/embedded_assets.h"
#include "common/console.h"
#include <bcrypt.h>
#include <fstream>
#include <mutex>
#include <chrono>
#include <charconv>
namespace social_runtime
{
	namespace
	{
		using social::Json;
		bool storageReady = true, server = true;
		social::Backend service;
		party_relay120::Service relayService;
		std::mutex serviceLock;

		std::string random()
		{
			return backend120::Random();
		}

		bool save_service()
		{
			if (!service.Dirty()) return true;
			if (!backend120::AtomicFile(L"players/social_backend.json", service.Save().dump(2))) return false;
			service.Saved();
			return true;
		}
	}
#include "network/backend/implementation/social_runtime_discord_request.inl"
#include "network/backend/implementation/social_runtime_authorize_communications.inl"
#include "network/backend/implementation/social_runtime_communications_live.inl"
#include "network/backend/implementation/social_runtime_communications_allowed.inl"
#include "network/backend/implementation/social_runtime_blocked.inl"
#include "network/backend/implementation/social_runtime_authorize_matchmaking.inl"
#include "network/backend/implementation/social_runtime_matchmaking_party_ready.inl"
#include "network/backend/implementation/social_runtime_request.inl"
#include "network/backend/implementation/social_runtime_resolve_account_name.inl"
#include "network/backend/implementation/social_runtime_tournament_members.inl"
	bool StartService()
	{
		std::lock_guard guard(serviceLock);
		service = social::Backend{};
		relayService = party_relay120::Service{};
		storageReady = true;
		try
		{
			const std::filesystem::path path = L"players/social_backend.json";
			if (std::filesystem::exists(path))
			{
				std::ifstream f(path);
				Json j;
				f >> j;
				storageReady = service.Load(j);
			}
			return storageReady;
		}
		catch (...)
		{
			storageReady = false;
			return false;
		}
	}

	Json PlayerStatus()
	{
		std::lock_guard guard(serviceLock);
		return service.Status(GetTickCount64());
	}

	void Publish(const Json& j)
	{
		social::Session s;
		s.bytes = j.value("session", std::string{});
		s.playlist = j.value("playlist", -1);
		s.map = j.value("map", std::string{});
		s.mode = j.value("mode", std::string{});
		s.capacity = j.value("capacity", 0);
		s.gameMode = j.value("game_mode", 0);
		s.endpoint = j.value("endpoint", std::string{});
		if (s.bytes.size() == 216) s.id = s.bytes.substr(168, 16);
		for (const auto& p : j.value("players", Json::array()))
			s.members.insert(chat::Id(p, "xuid"));
		std::lock_guard guard(serviceLock);
		service.Publish(std::move(s), GetTickCount64());
	}
}

namespace chat_runtime
{
	namespace
	{
		using chat::Json;
		std::mutex serviceLock;
		chat::Backend service;

		std::string random()
		{
			return backend120::Random();
		}
	}
#include "network/backend/implementation/chat_runtime_request.inl"
	void Publish(const Json& j)
	{
		std::vector<chat::Player> players;
		std::map<std::string, int> teams;
		for (const auto& p : j.value("players", Json::array()))
		{
			chat::Player v;
			v.xuid = chat::Id(p, "xuid");
			v.name = p.at("name");
			v.team = p.at("team");
			teams[std::to_string(v.xuid)] = v.team;
			players.push_back(v);
		}
		tournament_backend::PublishRoster(teams);
		auto session = j.value("session", std::string{});
		session = session.size() == 216 ? session.substr(168, 16) : std::string{};
		std::lock_guard guard(serviceLock);
		service.Publish(session, std::move(players), GetTickCount64());
	}
}

namespace publisher_variables
{
	namespace
	{
		using publisher::Json;
		publisher::Backend backend;
		std::mutex backendLock;
		bool storageReady = true;
#include "network/backend/implementation/publisher_variables_load_service.inl"
	}
#include "network/backend/implementation/publisher_variables_request.inl"
	bool StartService()
	{
		return LoadService(false);
	}
}

namespace name_validation
{
	namespace
	{
		using text_validation::Json;
		std::mutex mutex;
		Json policy;
		bool loaded = false;
	}
#include "network/backend/implementation/name_validation_load.inl"
#include "network/backend/implementation/name_validation_check.inl"
#include "network/backend/implementation/name_validation_request.inl"
}

namespace custom_mod_backend
{
	namespace
	{
		using custom_mods::Json;
		std::mutex serverMutex;
		std::set<std::string> knownWeapons;

		struct Account
		{
			Json doc = custom_mods::Empty();
			bool damaged = false;
		};

		std::map<uint64_t, Account> accounts;
		bool started = false;
#include "network/backend/implementation/custom_mod_backend_path.inl"
#include "network/backend/implementation/custom_mod_backend_read.inl"
#include "common/json_file.inl"
#include "network/backend/implementation/custom_mod_backend_load.inl"
	}
#include "network/backend/implementation/custom_mod_backend_start.inl"
#include "network/backend/implementation/custom_mod_backend_request.inl"
}

namespace frontend_backend
{
	namespace
	{
		using frontend_service::Json;
		std::mutex serviceLock;
		Json policy = frontend_service::Defaults(), linkedAccounts = Json::object(), motdCatalog = motd::Defaults(),
			 motdReads = Json::object();
		bool serviceReady = false, accountStoreReady = false, motdCatalogReady = false, motdReadsReady = false;
#include "network/backend/implementation/frontend_backend_save_accounts.inl"
#include "network/backend/implementation/frontend_backend_load_motd_catalog.inl"
#include "network/backend/implementation/frontend_backend_load_motd_reads.inl"
#include "network/backend/implementation/frontend_backend_save_motd_read.inl"
#include "network/backend/implementation/frontend_backend_load_accounts.inl"
#include "network/backend/implementation/frontend_backend_load_policy.inl"
	}
#include "network/backend/implementation/frontend_backend_request.inl"
#include "network/backend/implementation/frontend_backend_server_playlist_allowed.inl"
	bool StartService()
	{
		serviceReady = LoadPolicy();
		accountStoreReady = LoadAccounts();
		motdCatalogReady = LoadMotdCatalog();
		motdReadsReady = LoadMotdReads();
		return serviceReady && accountStoreReady && motdCatalogReady && motdReadsReady;
	}
}

namespace camo_changer
{
	nlohmann::json ServerStatus(uint64_t owner)
	{
		return backend120::CamoStatus(owner);
	}
}

namespace native_matchmaking
{
	namespace
	{
		using native_mm::Json;
		bool server = true, tokenStorageReady = true;
		native_mm::Backend backend;
		std::mutex backendLock;
#include "network/backend/implementation/native_matchmaking_save_tokens.inl"
	}
#include "network/backend/implementation/native_matchmaking_request.inl"
	bool StartService()
	{
		backend = native_mm::Backend((GetTickCount64() << 16) | GetCurrentProcessId());
		try
		{
			const std::filesystem::path p = L"players/native_mm_tokens.json";
			if (std::filesystem::exists(p))
			{
				std::ifstream f(p);
				Json j;
				f >> j;
				return backend.LoadTokens(j);
			}
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
}

namespace backend120
{
	bool ServicesStart()
	{
		profile_backend::ResetCache();
		settings_backend::ResetCache();
		if (!playlist_backend::Reload() || !publisher_variables::StartService() || !social_runtime::StartService() ||
			!native_matchmaking::StartService() || !frontend_backend::StartService() || !name_validation::Load() ||
			!store_backend::Load() || !custom_mod_backend::Start() || !progression_backend::Start())
			return false;
		return tournament_backend::Start();
	}

	void ServicesStop()
	{
		tournament_backend::Stop();
		progression_backend::Stop();
	}

	Json Players()
	{
		return social_runtime::PlayerStatus();
	}

	void PublishHost(const Json& h)
	{
		social_runtime::Publish(h);
		chat_runtime::Publish(h);
	}
}
