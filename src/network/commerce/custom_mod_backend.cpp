#include "network/commerce/custom_mod_backend.h"
#include "network/backend/name_validation.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "network/social/social_runtime.h"
#include "auth/offline_auth.h"
#include "common/embedded_assets.h"
#include "auth/profile/challenge_engine.h"
#include "common/console.h"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <map>
#include <sstream>
namespace custom_mod_backend
{
	namespace
	{
		using custom_mods::Json;
#ifndef IW8X_CLIENT_ONLY
		std::mutex serverMutex;
		std::set<std::string> knownWeapons;

		struct Account
		{
			Json doc = custom_mods::Empty();
			bool damaged = false;
		};

		std::map<uint64_t, Account> accounts;
		bool started = false;

#endif
		Json snapshot = custom_mods::Empty(), mutation = Json::object();
		bool ready = false, pending = false, journalLoaded = false, journalError = false;
		uint64_t generation = 0, serial = 0, next = 0;
		std::string state;
		std::filesystem::path journalPath;
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/custom_mod_backend_path.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/custom_mod_backend_read.inl"
#endif
#include "common/json_file.inl"
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/custom_mod_backend_load.inl"
#endif
		void journal()
		{
			if (journalLoaded) return;
			journalLoaded = true;
			try
			{
				wchar_t exe[MAX_PATH]{};
				if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) throw std::runtime_error("exe path");
				const auto origin = frontend_backend::Origin();
				const auto hash =
					challenges::Hash(std::string_view(reinterpret_cast<const char*>(origin.data()), origin.size() * sizeof(wchar_t)));
				journalPath = std::filesystem::path(exe).parent_path() / L"players" /
					(L"custom_mod_pending_" + std::to_wstring(offlineauth::GetIdentity().xuid) + L"_" + std::to_wstring(hash) + L".json");
				if (std::filesystem::exists(journalPath))
				{
					if (std::filesystem::file_size(journalPath) > 8192) throw std::runtime_error("journal size");
					std::ifstream f(journalPath);
					Json candidate;
					f >> candidate;
					if (!candidate.is_object()) throw std::runtime_error("journal shape");
					if (!candidate.empty())
					{
						auto doc = custom_mods::Empty();
						doc["revision"] = candidate.at("revision");
						std::string result;
						if (custom_mods::Apply(doc, candidate, { candidate.at("weapon").get<std::string>() }, result) != 200)
							throw std::runtime_error("journal mutation");
					}
					mutation = std::move(candidate);
					if (!mutation.empty()) state = "pending";
				}
			}
			catch (...)
			{
				journalError = true;
				state = "storage_error";
				LOG_ERR("CustomMods", "pending save unreadable; file retained");
			}
		}
	}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/custom_mod_backend_start.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/custom_mod_backend_request.inl"
#endif
	void Reset()
	{
		++generation;
		ready = pending = journalLoaded = journalError = false;
		next = 0;
		state.clear();
		snapshot = custom_mods::Empty();
		mutation = Json::object();
	}

	View Status()
	{
		return { ready && frontend_backend::Authenticated() && !journalError, !mutation.empty(), snapshot.at("revision").get<uint64_t>(),
			state };
	}

	void Pump()
	{
		if (!frontend_backend::Authenticated() || pending || GetTickCount64() < next) return;
		journal();
		Json request = { { "version", 1 }, { "build", frontend_service::Build }, { "op", "custom_mods" }, { "action", "sync" } };
		if (!journalError && !mutation.empty()) request.update(mutation);
		const auto nonce = "mods:" + std::to_string(++serial);
		request["nonce"] = nonce;
		const auto epoch = generation;
		const std::string transaction = request.value("transaction", std::string{});
		pending = true;
		next = GetTickCount64() + 3000;
		if (!social_runtime::SubmitFrontend(request.dump(),
				[epoch, nonce, transaction](unsigned status, std::string bytes)
				{
					if (epoch != generation) return;
					pending = false;
					bool accepted = false;
					try
					{
						const auto reply = Json::parse(bytes);
						if ((status == 200 || status == 409 || status == 422) &&
							frontend_service::Validate(reply, std::to_string(offlineauth::GetIdentity().xuid), nonce) &&
							custom_mods::Document(reply.at("custom_mods")))
						{
							const auto& candidate = reply.at("custom_mods");
							if (!ready || candidate.at("revision") >= snapshot.at("revision"))
							{
								const bool changed = !ready || candidate.at("revision") != snapshot.at("revision");
								snapshot = candidate;
								ready = true;
								accepted = true;
								if (changed)
									LOG_INFO("CustomMods", "server snapshot revision=%llu count=%zu",
										snapshot.at("revision").get<uint64_t>(), snapshot.at("mods").size());
							}
							if (accepted && !transaction.empty() && reply.at("custom_mod_result").at("transaction") == transaction)
							{
								if (Write(journalPath, Json::object(), false))
								{
									state = reply.at("custom_mod_result").at("status");
									mutation = Json::object();
								}
								else
								{
									journalError = true;
									state = "storage_error";
								}
							}
						}
					}
					catch (...)
					{
					}
					next = GetTickCount64() + (accepted && mutation.empty() ? 15000 : 3000);
					if (!accepted) LOG_WARN("CustomMods", "waiting for server http=%u pending=%d", status, int(!mutation.empty()));
				}))
			pending = false;
	}

	bool Change(std::string_view bytes)
	{
		if (bytes.size() > 8192 || !Status().ready || !mutation.empty()) return false;
		try
		{
			auto candidate = Json::parse(bytes);
			candidate["revision"] = snapshot.at("revision");
			unsigned char random[16]{};
			if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
			std::string token;
			for (auto c : random)
			{
				token += "0123456789abcdef"[c >> 4];
				token += "0123456789abcdef"[c & 15];
			}
			candidate["transaction"] = token;
			auto copy = snapshot;
			std::string result;
			if (custom_mods::Apply(copy, candidate, { candidate.at("weapon").get<std::string>() }, result) != 200)
			{
				state = "invalid";
				return false;
			}
			if (!Write(journalPath, candidate, false))
			{
				state = "storage_error";
				return false;
			}
			mutation = std::move(candidate);
			state = "pending";
			next = 0;
			return true;
		}
		catch (...)
		{
			state = "invalid";
			return false;
		}
	}

	std::string Field(const std::string& path)
	{
		return ready ? custom_mods::Field(snapshot, path) : std::string{};
	}

	bool Command(std::string_view command, bool server)
	{
		if (command != "custom_mods_status" && command != "custom_mods_refresh") return false;
		if (!server)
		{
			if (command == "custom_mods_refresh") next = 0;
			const auto view = Status();
			OP_INFO("Custom mods ready=%d pending=%d revision=%llu status=%s", int(view.ready), int(view.pending), view.revision,
				view.status.c_str());
		}
		return true;
	}
}
