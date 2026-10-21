#include "network/commerce/store_backend.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "network/social/social_runtime.h"
#include "auth/offline_auth.h"

#include "common/console.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <ctime>
namespace store_backend
{
	namespace
	{
		using store_catalog::Json;
		std::mutex lock;
#ifndef IW8X_CLIENT_ONLY
		Json catalog;
		bool loaded = false;
#endif
		View view;
		bool pending = false;
		uint64_t epoch = 0, sequence = 0, next = 0, expires = 0;
	}
#ifndef IW8X_CLIENT_ONLY

	bool Load()
	{
		try
		{
			auto candidate = store_catalog::Defaults();
			const std::filesystem::path path = L"players/backend/store.json";
			if (std::filesystem::exists(path))
			{
				if (std::filesystem::file_size(path) > 65536) return false;
				std::ifstream file(path);
				file >> candidate;
			}
			if (!store_catalog::Validate(candidate)) return false;
			std::lock_guard guard(lock);
			if (loaded && candidate != catalog && candidate.at("revision") <= catalog.at("revision")) return false;
			catalog = std::move(candidate);
			loaded = true;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	bool Reply(Json& response)
	{
		std::lock_guard guard(lock);
		if (!loaded) return false;
		response["store"] = store_catalog::Select(catalog, uint64_t(std::time(nullptr)));
		return true;
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	bool Published(uint32_t bundle)
	{
		std::lock_guard guard(lock);
		if (!loaded) return false;
		const auto active = store_catalog::Select(catalog, uint64_t(std::time(nullptr)));
		for (const auto& row : active.at("categories"))
			for (const auto& offer : row.at("offers"))
				if (offer.at("bundle_id") == bundle) return true;
		return false;
	}
#endif

#ifndef MAGMA_BACKEND_STANDALONE
	void Reset()
	{
		std::lock_guard guard(lock);
		++epoch;
		pending = false;
		view = {};
		next = expires = 0;
	}

	View Get()
	{
		std::lock_guard guard(lock);
		if (!frontend_backend::Authenticated() || GetTickCount64() >= expires) return {};
		return view;
	}

	void Pump()
	{
		uint64_t generation, began;
		std::string nonce;
		{
			std::lock_guard guard(lock);
			began = GetTickCount64();
			if (pending || began < next || !frontend_backend::Authenticated()) return;
			generation = epoch;
			nonce = "store:" + std::to_string(++sequence);
			pending = true;
			next = began + 5000;
		}
		const Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "store_catalog" }, { "nonce", nonce } };
		const bool queued = social_runtime::SubmitFrontend(request.dump(),
			[generation, began, nonce](unsigned status, std::string body)
			{
				View result;
				bool valid = false;
				try
				{
					const auto response = Json::parse(body);
					const auto& data = response.at("store");
					valid = status == 200 && frontend_backend::Authenticated() && GetTickCount64() - began < 10000 &&
						frontend_service::Validate(response, std::to_string(offlineauth::GetIdentity().xuid), nonce) &&
						store_catalog::Validate(data);
					if (valid)
					{
						result.ready = true;
						result.revision = data.at("revision");
						for (const auto& c : data.at("categories"))
						{
							Row row{ c.at("id"), c.at("name"), {} };
							for (const auto& o : c.at("offers"))
							{
								if (!row.bundles.empty()) row.bundles += ",";
								row.bundles += std::to_string(o.at("bundle_id").get<uint32_t>());
							}
							result.rows.push_back(std::move(row));
						}
					}
				}
				catch (...)
				{
					valid = false;
				}
				std::lock_guard guard(lock);
				if (generation != epoch) return;
				pending = false;
				if (valid)
				{
					view = std::move(result);
					expires = GetTickCount64() + 60000;
				}
				next = GetTickCount64() + (valid ? 45000 : 5000);
				LOG_INFO("StoreBackend", "catalog http=%u accepted=%d revision=%u categories=%zu", status, int(valid), view.revision,
					view.rows.size());
			});
		if (!queued)
		{
			std::lock_guard guard(lock);
			if (generation == epoch) pending = false;
		}
	}

	bool Command(std::string_view cmd, bool server)
	{
#ifndef IW8X_CLIENT_ONLY
		if (cmd == "store_reload")
		{
			if (server)
				OP_INFO("Store reload: %s", Load() ? "accepted" : "invalid/non-increasing revision; previous catalog retained");
			else
				OP_WARN("store_reload is a dedicated-server command");
			return true;
		}
#endif

		if (cmd == "store_refresh")
		{
			if (!server)
			{
				std::lock_guard guard(lock);
				next = 0;
			}
			return true;
		}
		if (cmd == "store_status")
		{
#ifndef IW8X_CLIENT_ONLY
			if (server)
			{
				std::lock_guard guard(lock);
				OP_INFO("Store ready=%d revision=%u categories=%zu", int(loaded), loaded ? catalog.at("revision").get<uint32_t>() : 0,
					loaded ? catalog.at("categories").size() : 0);
			}
			else
#endif

			{
				const auto v = Get();
				OP_INFO("Store ready=%d revision=%u categories=%zu (preview catalog)", int(v.ready), v.revision, v.rows.size());
			}
			return true;
		}
		return false;
	}
#endif
}
