#include "network/social/discord_client.h"
#include "network/social/discord_secret.h"
#include "network/social/chat_backend.h"
#include "thirdparty/discord-rpc/include/discord_rpc.h"
#include "network/social/social_runtime.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "auth/offline_auth.h"
#include "auth/profile/local_stats.h"
#include "network/backend/playlist_catalog.h"
#include "common/console.h"
#include <filesystem>
#include <fstream>
#include <ctime>
#include <algorithm>
namespace discord120
{
	namespace
	{
		using Json = frontend_service::Json;
		bool started = false, connected = false, createPending = false, joinPending = false, enabled = true;
		uint64_t epoch = 0, serial = 0, nextInvite = 0, inviteUntil = 0, nextPump = 0, nextPublish = 0, joinDeadline = 0;
		std::wstring activeOrigin;
		std::filesystem::path joinFile;
		std::string scope, secret, session, pending, previous, application = "1558980613661393066";
		std::string large_image_key = "iw8x";
		int64_t began = 0;
		bool advertise = false;

		void Ready(const DiscordUser*)
		{
			connected = true;
			previous.clear();
			LOG_INFO("Discord120", "RPC connected");
		}

		void Disconnected(int code, const char*)
		{
			connected = false;
			LOG_INFO("Discord120", "RPC disconnected code=%d", code);
		}

		void Error(int code, const char*)
		{
			static uint64_t next = 0;
			const auto now = GetTickCount64();
			if (now >= next)
			{
				next = now + 30000;
				LOG_WARN("Discord120", "optional RPC error code=%d", code);
			}
		}

		void Receive(const char* text)
		{
			if (!text) return;
			const auto size = strnlen(text, 129);
			Secret parsed;
			if (!Parse(std::string_view(text, size), parsed) || parsed.scope != scope)
			{
				LOG_WARN("Discord120", "invite belongs to another build/backend or is malformed");
				return;
			}
			pending.assign(text, size);
			joinDeadline = GetTickCount64() + 60000;
		}

		bool Submit(const char* action, Json body, std::function<void(unsigned, const Json&)> done)
		{
			const auto generation = epoch;
			const auto nonce = "discord:" + std::to_string(++serial);
			const auto id = std::to_string(offlineauth::GetIdentity().xuid);
			body.update(
				{ { "version", 1 }, { "build", frontend_service::Build }, { "op", "discord" }, { "action", action }, { "nonce", nonce } });
			return social_runtime::SubmitFrontend(body.dump(),
				[generation, nonce, id, done = std::move(done)](unsigned status, std::string text)
				{
					if (generation != epoch) return;
					Json response;
					try
					{
						response = Json::parse(text);
						if (!frontend_backend::Authenticated() || !frontend_service::Validate(response, id, nonce))
						{
							done(401, Json::object());
							return;
						}
						if (!response.contains("discord") || !response.at("discord").is_object())
							throw std::runtime_error("missing capability result");
					}
					catch (...)
					{
						done(400, Json::object());
						return;
					}
					done(status, response.at("discord"));
				});
		}

		void Init()
		{
			HMODULE module = nullptr;
			wchar_t path[MAX_PATH]{};
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&Pump), &module) ||
				!GetModuleFileNameW(module, path, MAX_PATH))
				return;
			const auto root = std::filesystem::path(path).parent_path(), ini = root / L"mw120rproxy.ini";
			enabled = GetPrivateProfileIntW(L"discord", L"enabled", 1, ini.c_str()) != 0;
			if (!enabled) return;
			wchar_t configured[32]{};
			GetPrivateProfileStringW(L"discord", L"application_id", L"1558980613661393066", configured, 32, ini.c_str());
			application.clear();
			for (auto c : std::wstring_view(configured))
			{
				if (c < L'0' || c > L'9')
				{
					enabled = false;
					return;
				}
				application += char(c);
			}
			if (application.size() < 17 || application.size() > 20)
			{
				enabled = false;
				return;
			}
			wchar_t configured_image[33]{};
			GetPrivateProfileStringW(L"discord", L"large_image_key", L"iw8x", configured_image, 33, ini.c_str());
			large_image_key.clear();
			for (const auto c : std::wstring_view(configured_image))
			{
				if (c > 127)
				{
					large_image_key = "iw8x";
					break;
				}
				large_image_key += char(c);
			}
			if (large_image_key.empty()) large_image_key = "iw8x";
			joinFile = root / L"mw120.discord-join.json";
			DiscordEventHandlers handlers{};
			handlers.ready = Ready;
			handlers.disconnected = Disconnected;
			handlers.errored = Error;
			handlers.joinGame = Receive;
			Discord_Initialize(application.c_str(), &handlers, 0, nullptr);
			began = int64_t(std::time(nullptr));
			started = true;
		}

		void ReadJoin()
		{
			std::error_code ec;
			const auto size = std::filesystem::file_size(joinFile, ec);
			if (ec) return;
			try
			{
				if (size > 512) throw std::runtime_error("oversized invite");
				std::ifstream file(joinFile);
				Json j;
				file >> j;
				file.close();
				std::filesystem::remove(joinFile, ec);
				const auto expires = j.at("expires").get<int64_t>(), now = int64_t(std::time(nullptr));
				if (expires <= now || expires > now + 120) throw std::runtime_error("expired invite");
				Receive(j.at("secret").get_ref<const std::string&>().c_str());
			}
			catch (...)
			{
				std::filesystem::remove(joinFile, ec);
				LOG_WARN("Discord120", "launcher invite rejected");
			}
		}
	}

	void Stop()
	{
		++epoch;
		if (started) Discord_Shutdown();
		started = connected = createPending = joinPending = false;
		secret.clear();
		pending.clear();
		previous.clear();
	}

	void Pump()
	{
		if (!frontend_backend::Required()) return;
		try
		{
			const auto origin = frontend_backend::Origin();
			if (origin.empty()) return;
			if (origin != activeOrigin)
			{
				++epoch;
				activeOrigin = origin;
				secret.clear();
				pending.clear();
				previous.clear();
				createPending = joinPending = false;
				nextInvite = inviteUntil = 0;
				std::string normalized;
				for (auto c : origin)
				{
					if (c > 127) return;
					normalized += char(c);
				}
				while (normalized.ends_with('/'))
					normalized.pop_back();
				scope = playlist_catalog::Digest(normalized).substr(0, 16);
				auto host = normalized.substr(normalized.find("://") + 3);
				host = host.substr(0, host.find(':'));
				std::transform(host.begin(), host.end(), host.begin(),
					[](unsigned char c)
					{
						return char(std::tolower(c));
					});
				advertise = !host.empty() && host != "localhost" && !host.starts_with("127.") && host != "[" && host != "0.0.0.0";
			}
			if (!started && enabled) Init();
			if (!started) return;
			Discord_RunCallbacks();
			const auto now = GetTickCount64();
			if (now < nextPump) return;
			nextPump = now + 1000;
			ReadJoin();
			if (!pending.empty() && now >= joinDeadline) pending.clear();
			if (!pending.empty() && !joinPending && frontend_backend::Authenticated() && local_stats::RemoteReady())
			{
				Secret parsed;
				if (!Parse(pending, parsed))
				{
					pending.clear();
					return;
				}
				const auto received = pending;
				joinPending = Submit("resolve", { { "token", parsed.token } },
					[received](unsigned status, const Json& reply)
					{
						joinPending = false;
						if (received != pending) return;
						pending.clear();
						const auto target = chat::Id(reply, "target");
						const auto duration = reply.value("expires_in_ms", uint64_t(0));
						if (status != 200 || !target || !duration || duration > 120000 ||
							!social_runtime::JoinDiscord(target, GetTickCount64() + duration))
							LOG_WARN("Discord120", "native join unavailable or capability rejected http=%u", status);
					});
			}
			auto party = Json::parse(social_runtime::PartySnapshot(), nullptr, false);
			if (!party.is_object()) return;
			const auto current =
				party.value("game", std::string{}).empty() ? party.value("party", std::string{}) : party.value("game", std::string{});
			if (!frontend_backend::Authenticated() || session != current || now >= inviteUntil) secret.clear();
			if (advertise && frontend_backend::Authenticated() && !current.empty() && !createPending && now >= nextInvite)
			{
				nextInvite = now + 15000;
				createPending = Submit("create", Json::object(),
					[current](unsigned status, const Json& reply)
					{
						createPending = false;
						const auto duration = reply.value("expires_in_ms", uint64_t(0));
						if (status != 200 || reply.value("session", std::string{}) != current || !duration || duration > 600000)
						{
							secret.clear();
							return;
						}
						secret = Make(scope, reply.value("token", std::string{}));
						session = current;
						inviteUntil = GetTickCount64() + duration;
					});
			}
			const auto activity = party.value("activity", std::string("menus"));
			const std::string details = activity == "ingame" ? "In match"
				: activity == "searching"					 ? "Matchmaking"
				: current.empty()							 ? "Main menu"
															 : "In lobby";
			const auto members = party.value("members", Json::array());
			const int count = std::clamp(int(members.size()), 1, 200);
			const auto fingerprint = Json{ { "details", details }, { "party", current }, { "size", count }, { "secret", secret } }.dump();
			if (!connected || now < nextPublish || fingerprint == previous) return;
			const auto partyId = "magma120:" + scope + ":" + current;
			DiscordRichPresence presence{};
			presence.details = details.c_str();
			presence.state = "IW8x | MW2019 1.20.4";
			presence.startTimestamp = began;
			presence.largeImageKey = large_image_key.c_str();
			presence.largeImageText = "IW8x 1.20.4";
			presence.partyId = current.empty() ? nullptr : partyId.c_str();
			presence.partySize = count;
			presence.partyMax = std::max(count, 20);
			presence.partyPrivacy = DISCORD_PARTY_PUBLIC;
			presence.joinSecret = secret.empty() ? nullptr : secret.c_str();
			presence.instance = activity == "ingame";
			Discord_UpdatePresence(&presence);
			previous = fingerprint;
			nextPublish = now + 5000;
		}
		catch (...)
		{
			Error(-1, nullptr);
		}
	}
}
