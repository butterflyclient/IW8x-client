#include "network/backend/playlist_backend.h"
#include "client/component/scheduler.hpp"
#include "common/console.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
namespace playlist_backend
{
	namespace
	{
		std::mutex lock, lifecycle_mutex;
		bool server = false, configured = false, wanted = false;
		std::atomic<bool> stopping{ false };
		scheduler::task_handle worker;
		std::wstring origin;
		uint64_t epoch = 0, retryAt = 0, nextErrorLog = 0;
		std::optional<playlist_catalog::Snapshot> service, received;
		std::string serviceWire, acceptedHash, lastError = "not configured";

		std::optional<scheduler::milliseconds> worker_tick()
		{
			if (stopping.load()) return std::nullopt;
			std::wstring target;
			std::string known;
			uint64_t generation = 0;
			{
				std::lock_guard guard(lock);
				if (!wanted || server || origin.empty()) return scheduler::milliseconds{ 3600000 };
				const auto now = GetTickCount64();
				if (now < retryAt) return scheduler::milliseconds{ static_cast<int64_t>(retryAt - now) };
				target = origin;
				generation = epoch;
				known = acceptedHash;
			}
			std::optional<playlist_catalog::Snapshot> result;
			std::string error;
			bool unchanged = false;
			try
			{
				auto request = nlohmann::json{ { "protocol", 1 }, { "build", "iw8-1.20.4" }, { "known_sha256", known } }.dump();
				auto reply = match_http::Post(target, L"/v1/playlists", request);
				if (reply.status != 200) throw std::runtime_error("HTTP " + std::to_string(reply.status) + ": " + reply.body);
				const auto response = nlohmann::json::parse(reply.body);
				unchanged = !known.empty() && response.value("unchanged", false) && response.value("sha256", std::string{}) == known;
				if (!unchanged) result = playlist_catalog::Decode(reply.body);
			}
			catch (const std::exception& exception)
			{
				error = exception.what();
			}
			std::lock_guard guard(lock);
			if (stopping.load()) return std::nullopt;
			if (generation != epoch) return scheduler::milliseconds{ 0 }; // stale origin/refresh
			if (unchanged || result)
			{
				retryAt = GetTickCount64() + 5000;
				lastError.clear();
				if (!unchanged && result->sha256 != acceptedHash)
				{
					LOG_INFO("PlaylistBackend", "received HTTP catalog: origin=%ls bytes=%zu version=%d sha256=%s", target.c_str(),
						result->text.size(), result->version, result->sha256.c_str());
					acceptedHash = result->sha256;
					received = std::move(result);
				}
			}
			else
			{
				const auto now = GetTickCount64();
				if (error != lastError || now >= nextErrorLog)
				{
					LOG_WARN("PlaylistBackend", "waiting for backend: %s; retry in 5s", error.c_str());
					nextErrorLog = now + 30000;
				}
				lastError = std::move(error);
				retryAt = now + 5000;
			}
			return scheduler::milliseconds{ 5000 };
		}

		bool ensure_worker_locked()
		{
			if (worker) return true;
			worker = scheduler::schedule(worker_tick);
			if (!worker) lastError = "playlist HTTP work admission rejected";
			return bool(worker);
		}
	}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/playlist_backend_reload.inl"
#endif
	void Configure(bool dedicated, const std::wstring& target)
	{
		std::lock_guard lifecycle_guard(lifecycle_mutex);
		bool load = false;
		scheduler::task_handle active_worker;
		{
			std::lock_guard guard(lock);
			load = dedicated && !configured;
			server = dedicated;
			configured = true;
			if (!server && (origin != target || !worker))
			{
				origin = target;
				++epoch;
				received.reset();
				acceptedHash.clear();
				wanted = true;
				retryAt = nextErrorLog = 0;
				lastError = "waiting for backend";
				stopping = false;
				ensure_worker_locked();
				active_worker = worker;
			}
		}
		active_worker.wake();
#ifndef IW8X_CLIENT_ONLY
		if (load) Reload();
#endif
	}

	void Shutdown()
	{
		// Configuration waits for cancellation; worker_tick only acquires lock.
		std::lock_guard lifecycle_guard(lifecycle_mutex);
		stopping = true;
		scheduler::task_handle active_worker;
		{
			std::lock_guard guard(lock);
			active_worker = worker;
			worker = {};
		}
		active_worker.cancel(true);
		std::lock_guard guard(lock);
		++epoch;
		configured = false;
		wanted = false;
		received.reset();
		service.reset();
		serviceWire.clear();
		acceptedHash.clear();
	}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/playlist_backend_request.inl"
#endif
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/playlist_backend_server_catalog.inl"
#endif
	std::optional<playlist_catalog::Snapshot> Take()
	{
		std::lock_guard guard(lock);
		if (!server && configured && wanted && !worker && !stopping.load()) ensure_worker_locked();
		auto result = std::move(received);
		received.reset();
		return result;
	}

	void Refresh()
	{
		scheduler::task_handle active_worker;
		{
			std::lock_guard guard(lock);
			if (server || !configured || stopping.load()) return;
			++epoch;
			received.reset();
			acceptedHash.clear();
			wanted = true;
			retryAt = nextErrorLog = 0;
			ensure_worker_locked();
			active_worker = worker;
		}
		active_worker.wake();
	}

	bool Current(std::string_view hash)
	{
		std::lock_guard guard(lock);
		return server ? service && service->sha256 == hash : acceptedHash == hash && !hash.empty();
	}

	void LogStatus()
	{
		std::lock_guard guard(lock);
		OP_INFO("Playlist backend: role=%s origin=%ls pending=%d sha256=%s error='%s'",
			server ? "dedicated" : "client", origin.c_str(), int(wanted),
			server && service ? service->sha256.c_str() : acceptedHash.c_str(), lastError.c_str());
	}
}
