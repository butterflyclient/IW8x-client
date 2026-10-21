#include "network/backend/backend_agent.h"
#include "network/social/roster_memory.h"
#include "network/commerce/unlock_policy.h"
#include "network/backend/backend_latency.h"
#include "network/backend/native_matchmaking.h"
#include "network/social/party_roster_snapshot.h"
#include "network/social/social_runtime.h"
#include "network/social/chat_runtime.h"
#include "network/backend/frontend_backend.h"
#include "network/social/social_backend.h"
#include "network/social/party_relay.h"
#include "network/social/social_bindings.h"
#include "auth/offline_auth.h"
#include "engine/game.h"
#include "common/console.h"
#include "engine/safemem.h"
#include "network/playlists.h"
#include "network/online_services.h"
#include "auth/profile/local_progression.h"
#include <bcrypt.h>
#include <chrono>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include "network/social/runtime/internal.hpp"
#include "client/component/scheduler.hpp"

#include <memory>

namespace social_runtime::detail
{
	extern std::atomic<uint64_t> worker_epoch;

	namespace
	{
		constexpr size_t result_capacity = 128;

		struct worker_context
		{
			uint64_t epoch = 0, next_sync = 0;
			bool credential_checked = false, fresh_session = false;
			std::string key, nonce, lease, registered_name;
			std::wstring active;
			std::map<std::wstring, unsigned> statuses;

			bool live() const
			{
				return !stopping.load(std::memory_order_acquire) && worker_epoch.load(std::memory_order_acquire) == epoch;
			}

			void publish_lease(bool valid) const
			{
				std::lock_guard lock(clientLock);
				if (!live() || origin != active) return;
				communicationsOrigin = active;
				communicationsLease = valid ? lease : std::string{};
				communicationsAt = valid ? GetTickCount64() : 0;
			}
		};

		std::optional<scheduler::milliseconds> worker_tick(worker_context& context)
		{
			using delay = scheduler::milliseconds;
			if (!context.live()) return std::nullopt;
			if (!context.credential_checked)
			{
				context.credential_checked = true;
				context.key = credential();
				context.nonce = random();
				if (context.key.empty() || context.nonce.empty())
				{
					LOG_ERR("Social", "identity credential unavailable; existing file preserved");
					return std::nullopt;
				}
			}

			Json state;
			std::wstring target;
			uint64_t captured = 0;
			{
				std::lock_guard lock(clientLock);
				state = snapshot;
				target = origin;
				captured = snapshotAt;
			}
			// Leave durable requests queued until their identity, target and lease exist.
			if (!state.is_object() || !chat::Id(state, "xuid") || !state.contains("name") || target.empty())
				return delay{ 1000 };
			if (context.active != target)
			{
				context.active = target;
				context.lease.clear();
				context.registered_name.clear();
				context.next_sync = 0;
				context.publish_lease(false);
				eventAck = 0;
				lastContact = 0;
				matchmakingLatency.Reset();
			}
			auto post = [&](std::wstring_view path, Json body)
			{
				if (!context.live()) return match_http::Reply{ 0, {} };
				body["version"] = 1;
				body["xuid"] = state["xuid"];
				body["lease"] = context.lease;
				const auto bytes = body.dump();
				const auto began = std::chrono::steady_clock::now();
				auto reply = match_http::Post(target, path, bytes);
				if (!context.live()) return match_http::Reply{ 0, {} };
				if (path == L"/v1/native-mm")
				{
					const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - began).count();
					matchmakingLatency.Report(reply.status, uint64_t((us + 999) / 1000), GetTickCount64());
				}
				if (reply.status == 200 && !context.lease.empty())
					context.publish_lease(true);
				else if (reply.status == 401)
					context.publish_lease(false);
				const auto path_key = std::wstring(path);
				if (!context.statuses.contains(path_key) || context.statuses[path_key] != reply.status)
				{
					context.statuses[path_key] = reply.status;
					LOG_INFO("Social", "service %ls status=%u", path_key.c_str(), reply.status);
				}
				return reply;
			};
			if (context.lease.empty() || context.registered_name != state.value("name", std::string{}))
			{
				auto reply = post(L"/v1/social/open", Json{ { "key", context.key }, { "nonce", context.nonce }, { "name", state["name"] } });
				if (reply.status == 200) try
					{
						auto body = Json::parse(reply.body);
						auto token = body.value("lease", std::string{});
						if (chat::Hex(token, 32))
						{
							const bool fresh = context.lease != token;
							context.lease = token;
							context.registered_name = state["name"];
							context.publish_lease(true);
							// Rename authentication preserves acknowledgements unless the lease changed.
							if (fresh)
							{
								eventAck = 0;
								context.fresh_session = true;
							}
						}
					}
					catch (...)
					{
					}
			}
			if (!context.live()) return std::nullopt;
			if (context.lease.empty()) return delay{ 1000 };

			Work work{};
			{
				std::lock_guard lock(clientLock);
				if (!context.live()) return std::nullopt;
				if (origin != target) return delay{ 0 };
				if (completed.size() >= result_capacity) return delay{ 50 };
				if (context.fresh_session)
				{
					completed.push_back({ 0, 201, {} });
					context.fresh_session = false;
				}
				if (!outgoing.empty() && completed.size() < result_capacity)
				{
					work = std::move(outgoing.front());
					outgoing.pop_front();
				}
			}
			if (work.id)
			{
				const auto path = work.kind == Kind::Friends ? L"/v1/social/friends"
					: work.kind == Kind::subscribe ? L"/v1/social/subscribe"
					: work.kind == Kind::requests_impl ? L"/v1/social/requests"
					: work.kind == Kind::add_friend ? L"/v1/social/request"
					: work.kind == Kind::remove_friend ? L"/v1/social/remove"
					: work.kind == Kind::availability ? L"/v1/social/availability"
					: work.kind == Kind::Username ? L"/v1/social/search"
					: (work.kind == Kind::list_fetch || work.kind == Kind::list_mutation) ? L"/v1/social/userlist"
					: work.kind == Kind::Frontend ? L"/v1/frontend"
					: work.kind == Kind::Matchmaking ? L"/v1/native-mm" : L"/v1/social/message";
				auto reply = post(path, work.body);
				if (!reply.status && context.live()) reply = post(path, work.body);
				Json body;
				try
				{
					body = Json::parse(reply.body);
				}
				catch (...)
				{
				}
				{
					std::lock_guard lock(clientLock);
					if (!context.live()) return std::nullopt;
					// IDs retain their native owner; never dispatch callbacks from this worker.
					completed.push_back({ work.id, reply.status, std::move(body) });
				}
				if (reply.status == 401)
				{
					context.lease.clear();
					lastContact = 0;
				}
			}
			if (!context.lease.empty() && context.live() && GetTickCount64() >= context.next_sync)
			{
				const auto now = GetTickCount64();
				state["snapshot_fresh"] = captured && now >= captured && now - captured < 1500;
				state["ack"] = std::to_string(eventAck.load());
				auto reply = post(L"/v1/social/sync", state);
				context.next_sync = GetTickCount64() + 500;
				if (reply.status == 200) try
					{
						auto body = Json::parse(reply.body);
						std::lock_guard lock(clientLock);
						if (!context.live()) return std::nullopt;
						if (origin == target)
						{
							std::erase_if(completed, [](const Result& result) { return !result.id && result.status == 200; });
							if (completed.size() < result_capacity) completed.push_back({ 0, 200, std::move(body) });
						}
					}
					catch (...)
					{
					}
				else if (reply.status == 401)
				{
					context.lease.clear();
					lastContact = 0;
				}
			}
			if (!context.live()) return std::nullopt;
			std::lock_guard lock(clientLock);
			if (!outgoing.empty()) return delay{ 0 };
			const auto now = GetTickCount64();
			return delay{ static_cast<int64_t>(context.next_sync > now ? context.next_sync - now : 0) };
		}
	}

	scheduler::callback make_worker(uint64_t epoch)
	{
		auto context = std::make_shared<worker_context>();
		context->epoch = epoch;
		return [context]
		{
			return worker_tick(*context);
		};
	}

	DWORD WINAPI worker_impl(void* opaque)
	{
		// Compatibility entry performs one finite tick; shared scheduling owns repeats.
		if (opaque) (void)worker_tick(*static_cast<worker_context*>(opaque));
		return 0;
	}
}
