#include "network/backend/name_validation.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "network/social/social_runtime.h"
#include "auth/offline_auth.h"

#include "common/console.h"

#include <filesystem>
#include <fstream>
#include <mutex>
namespace name_validation
{
	namespace
	{
		using text_validation::Json;
		std::mutex mutex;
		Json policy;
		bool loaded = false;
		View view;
		uint64_t epoch = 0, serial = 0;
	}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/name_validation_load.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/name_validation_check.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/name_validation_request.inl"
#endif
	void Reset()
	{
		++epoch;
		view = {};
	}

	bool Submit(const std::string& text)
	{
		if (view.pending || !frontend_backend::Authenticated() || text.size() > 48) return false;
		const auto generation = epoch;
		const auto nonce = "name:" + std::to_string(++serial);
		const Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "validate_text" },
			{ "kind", "custom_mod_name" }, { "text", text }, { "nonce", nonce } };
		view = { true, false, false, {} };
		if (!social_runtime::SubmitFrontend(request.dump(),
				[generation, nonce](unsigned status, std::string bytes)
				{
					if (generation != epoch) return;
					view = { false, true, false, "unavailable" };
					try
					{
						const auto reply = Json::parse(bytes);
						const auto& validation = reply.at("text_validation");
						if (status == 200 && frontend_service::Validate(reply, std::to_string(offlineauth::GetIdentity().xuid), nonce) &&
							validation.at("kind") == "custom_mod_name")
						{
							view.allowed = validation.at("allowed").get<bool>();
							view.reason = validation.at("reason").get<std::string>();
							if (view.allowed && view.reason != "allowed") view = { false, true, false, "unavailable" };
						}
					}
					catch (...)
					{
					}
					LOG_INFO("NameValidation", "custom mod name http=%u result=%s", status, view.reason.c_str());
				}))
		{
			view = {};
			return false;
		}
		return true;
	}

	View Status()
	{
		return view;
	}
#ifndef IW8X_CLIENT_ONLY

	bool Command(std::string_view command, bool server)
	{
		if (command != "text_validation_reload") return false;
		if (server)
			OP_INFO("Text validation reload: %s", Load() ? "accepted" : "invalid configuration; previous rules retained");
		else
			OP_WARN("text_validation_reload is a dedicated-server command");
		return true;
	}
#endif

}
