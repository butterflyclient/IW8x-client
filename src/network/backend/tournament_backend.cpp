#include "network/backend/tournament_backend.h"
#include "network/online_services.h"
#include "network/backend/native_matchmaking.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/frontend_service.h"
#include "network/profile/progression_backend.h"
#include "network/social/social_runtime.h"
#include "auth/offline_auth.h"
#include "auth/profile/local_stats.h"
#include "common/console.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <ctime>
namespace tournament_backend
{
	namespace
	{
		using tournament120::Json;
#ifndef IW8X_CLIENT_ONLY
		tournament120::Service service;
		std::mutex serviceLock, rosterLock;
		std::map<std::string, int> roster;
		uint64_t rosterAt = 0;
		std::thread worker;
		std::atomic<bool> stopping{ false }, ready{ false };
		uint64_t observedAllocation = 0;
		bool resultWritePending = false;
		challenges::Event retryEvent;
		tournament120::Reservation publishedReservation;
		const std::filesystem::path path = L"players/backend/tournament.json";

		bool save()
		{
			try
			{
				const auto payload = service.Save().dump();
				if (payload.size() > 1024 * 1024) return false;
				const auto bytes = Json{ { "payload", payload }, { "checksum", std::to_string(challenges::Hash(payload)) } }.dump();
				std::filesystem::create_directories(path.parent_path());
				const auto tmp = path.wstring() + L".tmp";
				HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (f == INVALID_HANDLE_VALUE) return false;
				DWORD n = 0;
				const bool ok = WriteFile(f, bytes.data(), DWORD(bytes.size()), &n, nullptr) && n == bytes.size() && FlushFileBuffers(f);
				CloseHandle(f);
				if (!ok) return false;
				return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
			}
			catch (...)
			{
				return false;
			}
		}

		bool commit(const tournament120::Service& before)
		{
			if (before.Save() == service.Save() || save()) return true;
			service = before;
			return false;
		}

		void run()
		{
			while (!stopping)
			{
				try
				{
					auto host = online_services::TournamentHost();
					const auto now = GetTickCount64();
					{
						std::lock_guard lock(rosterLock);
						if (now >= rosterAt && now - rosterAt < 5000) host.teams = roster;
					}
					const bool allowed = frontend_backend::ServerPlaylistAllowed(tournament120::Playlist);
					tournament120::Reservation reservation;
					Json awards;
					{
						std::lock_guard lock(serviceLock);
						const auto before = service;
						if (resultWritePending)
							reservation = publishedReservation;
						else
						{
							reservation = service.Poll(host, now, uint32_t(std::time(nullptr)), allowed);
							if (!commit(before)) reservation = {};
							observedAllocation = host.allocation;
							publishedReservation = reservation;
						}
						awards = service.Awards();
					}
					// Never hold the bracket mutex while taking the allocator/account locks.
					online_services::ReserveTournament(reservation);
					for (const auto& [user, wins] : awards.items())
						if (progression_backend::TournamentWin(std::stoull(user), wins.get<int>()))
						{
							std::lock_guard lock(serviceLock);
							const auto before = service;
							service.AckAward(user, wins);
							commit(before);
						}
				}
				catch (const std::exception& e)
				{
					LOG_ERR("Tournament120", "worker: %s", e.what());
				}
				for (int i = 0; i < 5 && !stopping; ++i)
					Sleep(100);
			}
		}


#endif
		Json view = Json::object();
		std::string status = "connecting", queued;
		std::wstring origin;
		uint64_t epoch = 0, serial = 0, nextFetch = 0, viewUntil = 0;
		bool pending = false, interested = false;
	}
#ifndef IW8X_CLIENT_ONLY

	bool Start()
	{
		std::lock_guard lock(serviceLock);
		if (ready) return true;
		try
		{
			if (std::filesystem::exists(path))
			{
				if (std::filesystem::file_size(path) > 2 * 1024 * 1024) return false;
				std::ifstream file(path);
				Json envelope;
				file >> envelope;
				const auto data = envelope.at("payload").get<std::string>();
				if (envelope.at("checksum") != std::to_string(challenges::Hash(data)) || !service.Load(Json::parse(data))) return false;
			}
			ready = true;
			stopping = false;
			worker = std::thread(run);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	void Stop()
	{
		stopping = true;
		if (worker.joinable()) worker.join();
		ready = false;
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	void PublishRoster(const std::map<std::string, int>& teams)
	{
		if (!ready) return;
		std::unique_lock lock(rosterLock, std::try_to_lock);
		if (lock.owns_lock())
		{
			roster = teams;
			rosterAt = GetTickCount64();
		}
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	unsigned Request(const Json& identity, const Json& request, Json& response)
	{
		if (!ready) return 503;
		try
		{
			const auto user = identity.at("id").get<std::string>(), op = request.at("action").get<std::string>();
			Json members = Json::array({ Json{ { "id", user }, { "name", identity.at("name") } } });
			if (op == "register")
			{
				std::set<uint64_t> ids;
				for (const auto& id : identity.at("members"))
					ids.insert(chat::Id(Json{ { "id", id } }, "id"));
				members = Json::parse(social_runtime::TournamentMembers(std::stoull(user), ids, identity.at("party")));
				if (members.empty()) return 409;
			}
			std::lock_guard lock(serviceLock);
			if (resultWritePending) return 503;
			const auto before = service;
			const auto utc = uint32_t(std::time(nullptr));
			const auto result = service.Handle(user, op, members, utc);
			if (result != 200)
			{
				service = before;
				return result;
			}
			if (!commit(before)) return 503;
			response["tournament"] = service.Projection(user, utc);
			response["tournament"]["xuid"] = user;
			return 200;
		}
		catch (...)
		{
			return 400;
		}
	}
#endif
#ifndef IW8X_CLIENT_ONLY


	bool Observe(const challenges::Event& event)
	{
		if (!(event.name == "start_match" || event.name == "end_match")) return true;
		if (!ready) return false;
		std::lock_guard lock(serviceLock);
		if (resultWritePending &&
			(event.xuid != retryEvent.xuid || event.session != retryEvent.session || event.match != retryEvent.match ||
				event.sequence != retryEvent.sequence))
			return true;
		const auto before = service;
		service.Observe(event, observedAllocation);
		const bool committed = commit(before);
		resultWritePending = !committed;
		if (!committed) retryEvent = event;
		return committed;
	}
#endif

#ifndef MAGMA_BACKEND_STANDALONE
	void Pump()
	{
		const auto target = frontend_backend::Origin();
		if (target != origin)
		{
			origin = target;
			++epoch;
			view = Json::object();
			pending = false;
			queued.clear();
			viewUntil = 0;
			status = "connecting";
			nextFetch = 0;
		}
		if (!frontend_backend::Authenticated())
		{
			viewUntil = 0;
			return;
		}
		const auto now = GetTickCount64();
		if (!interested || pending || now < nextFetch) return;
		const auto generation = epoch;
		const auto nonce = "tournament:" + std::to_string(++serial);
		const auto op = queued.empty() ? std::string("fetch") : queued;
		pending = social_runtime::SubmitFrontend(
			Json{ { "version", 1 }, { "build", frontend_service::Build }, { "op", "tournament" }, { "action", op }, { "nonce", nonce } }
				.dump(),
			[generation, nonce, op, now](unsigned code, std::string bytes)
			{
				if (generation != epoch) return;
				pending = false;
				status = "unavailable";
				try
				{
					const auto reply = Json::parse(bytes);
					if (code == 200 && frontend_backend::Authenticated() && GetTickCount64() - now < 10000 &&
						frontend_service::Validate(reply, std::to_string(offlineauth::GetIdentity().xuid), nonce))
					{
						const auto candidate = reply.at("tournament");
						if (candidate.at("playlist") != tournament120::Playlist || !candidate.at("bracket").is_object() ||
							!candidate.at("registered").is_boolean() || !candidate.at("ready").is_array() ||
							!tournament120::UInt(candidate.at("status"), 3) || !tournament120::UInt(candidate.at("queued_players"), 32))
							return;
						view = candidate;
						viewUntil = GetTickCount64() + 5000;
						status = "complete";
					}
					else if (code == 409)
						status = "party_changed";
				}
				catch (...)
				{
				}
				// Mutations are idempotent; a failed response is retried by the user,
				// while the following fetch reconciles a lost successful response.
				if (queued == op) queued.clear();
			});
		nextFetch = now + 1000;
	}

	bool Command(std::string_view op)
	{
		interested = true;
		if (op == "refresh")
		{
			nextFetch = 0;
			return true;
		}
		if (pending || !queued.empty() || !frontend_backend::Authenticated() || !local_stats::RemoteReady() ||
			(op != "register" && op != "cancel" && op != "ready" && op != "unready" && op != "rejoin"))
			return false;
		queued = op;
		nextFetch = 0;
		return true;
	}

	std::string Field(std::string_view key)
	{
		interested = true;
		if (key == "status") return status;
		if (key == "pending") return pending || !queued.empty() ? "1" : "0";
		if (key == "online") return frontend_backend::Authenticated() && GetTickCount64() < viewUntil ? "1" : "0";
		if (!frontend_backend::Authenticated() || GetTickCount64() >= viewUntil || key.size() > 128) return {};
		try
		{
			const auto& value = view.at(Json::json_pointer(std::string(key)));
			if (value.is_string()) return value.get<std::string>();
			if (value.is_boolean()) return value.get<bool>() ? "1" : "0";
			return value.is_number() ? value.dump() : std::string{};
		}
		catch (...)
		{
			return {};
		}
	}

	bool Join()
	{
		return Field("online") == "1" && Field("/join") == "1" && local_stats::RemoteReady() && !online_services::IsSearching() &&
			native_matchmaking::Enabled();
	}
#endif
}
