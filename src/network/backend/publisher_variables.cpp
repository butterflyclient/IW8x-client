#include "network/backend/backend_agent.h"
#include "network/backend/publisher_variables.h"
#include "network/backend/frontend_backend.h"
#include "network/backend/publisher_backend.h"
#include "network/backend/publisher_bindings.h"
#include "network/backend/publisher_startup_cache.h"
#include "client/component/scheduler.hpp"
#include "common/utils/completion_budget.hpp"
#include "common/console.h"
#include "engine/safemem.h"
#include <atomic>
#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
namespace publisher_variables
{
	void Pump(const std::wstring& origin);

	namespace
	{
		using publisher::Json;
		uintptr_t base = 0;
		bool installed = false, server = false, cacheReady = false, bootstrapped = false;
		alignas(16) unsigned char cache[112]{}; // native bdPublisherVariables, context ""
		std::array<bool, 7> cached{}, attempted{};
#ifndef IW8X_CLIENT_ONLY
		publisher::Backend backend;
#endif
		std::mutex backendLock, workLock;
		bool storageReady = true;
		std::atomic<bool> stopping{ false };
		scheduler::task_handle worker;
		std::wstring currentOrigin;
		std::mutex originLock;
		std::wstring configuredOrigin;
		std::atomic<bool> enabled{ false };
		uint64_t epoch = 0, sequence = 0, nextVersions = 0, nextRetry = 0;
		bool versionsPending = false;

		struct Work
		{
			uint64_t id, epoch;
			std::wstring origin;
			std::string name;
			bool prefetch = false;
		};

		struct Result
		{
			Work work;
			match_http::Reply reply;
		};

		struct Pending
		{
			uintptr_t task;
			uint64_t epoch, at;
			std::string name;
		};

		std::deque<Work> outgoing;
		std::deque<Result> completed;
		constexpr size_t work_capacity = 64;
		bool work_in_flight = false;
		struct worker_context
		{
			publisher::StartupCache startup;
		};
		std::map<uint64_t, Pending> pending; // Only the engine thread touches native objects.
		using FetchFn = unsigned (*)(uintptr_t, const char*);
		using IteratorFn = bool (*)(int, const char*, void*);
		using KeyValueFn = bool (*)(int, void*, char*, unsigned, char*, unsigned);
		std::atomic<FetchFn> originalFetch{ nullptr };
		std::atomic<IteratorFn> originalIterator{ nullptr };
		std::atomic<KeyValueFn> originalKeyValue{ nullptr };
		std::atomic<void (*)()> originalFrame{ nullptr };

		template <class T>
		T native(uintptr_t rva)
		{
			return reinterpret_cast<T>(base + rva);
		}

		template <class T>
		T read(uintptr_t at)
		{
			T value{};
			safemem::ReadBytes(reinterpret_cast<void*>(at), &value, sizeof(value));
			return value;
		}

		template <class T>
		void write(uintptr_t at, T value)
		{
			memcpy(reinterpret_cast<void*>(at), &value, sizeof(value));
		}

		void release(uintptr_t object)
		{
			if (_InterlockedDecrement(reinterpret_cast<volatile long*>(object + 8)) == 0)
				reinterpret_cast<void (*)(uintptr_t, unsigned)>(read<uintptr_t>(read<uintptr_t>(object)))(object, 1);
		}

		uintptr_t state_impl(int index)
		{
			return base + 0xC3C0160 + 16ull * index;
		}

		std::string name_impl(const char* ptr)
		{
			std::string name;
			for (size_t i = 0; ptr && i < 32; ++i)
			{
				char c = 0;
				if (!safemem::ReadBytes(ptr + i, &c, 1)) return {};
				if (!c) return name;
				name += c;
			}
			return {};
		}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/publisher_variables_load_service.inl"
#endif
		std::optional<scheduler::milliseconds> worker_tick(worker_context& context)
		{
			if (stopping.load()) return std::nullopt;
			Work work{};
			{
				std::lock_guard lock(workLock);
				if (outgoing.empty()) return scheduler::milliseconds{ 3600000 };
				// Native tasks retain priority over entitlement-excluded prefetches.
				auto next = std::find_if(outgoing.begin(), outgoing.end(),
					[](const Work& candidate)
					{
						return !candidate.prefetch;
					});
				if (next == outgoing.end()) next = outgoing.begin();
				work = std::move(*next);
				outgoing.erase(next);
				work_in_flight = true;
			}
			match_http::Reply reply{ 0, {} };
			try
			{
				context.startup.Select(work.origin);
				if (!work.prefetch || context.startup.Needed(work.origin, work.name))
				{
					std::optional<std::string> cached_body;
					if (!work.prefetch && !work.name.empty()) cached_body = context.startup.Take(work.origin, work.name, GetTickCount64());
					if (cached_body)
					{
						LOG_INFO("PublisherVars", "using startup download namespace=%s for native task", work.name.c_str());
						reply = { 200, std::move(*cached_body) };
					}
					else
					{
						Json request = { { "version", 1 }, { "context", "" }, { "op", work.name.empty() ? "versions" : "fetch" } };
						if (!work.name.empty()) request["namespace"] = work.name;
						reply = match_http::Post(work.origin, L"/v1/publisher-variables", request.dump());
					}
					if (work.prefetch)
					{
						const bool accepted = context.startup.Store(work.origin, work.name, reply.status, reply.body, GetTickCount64());
						LOG_INFO("PublisherVars", "startup download namespace=%s http=%u validated=%d", work.name.c_str(), reply.status,
							int(accepted));
					}
				}
			}
			catch (...)
			{
				reply = { 503, {} };
				LOG_WARN("PublisherVars", "publisher request failed; native completion retained");
			}
			std::lock_guard lock(workLock);
			work_in_flight = false;
			if (!stopping.load() && !work.prefetch) completed.push_back({ std::move(work), std::move(reply) });
			return stopping.load() ? std::nullopt
				: std::optional<scheduler::milliseconds>{ scheduler::milliseconds{ outgoing.empty() ? 3600000 : 0 } };
		}

		bool ensure_worker_locked()
		{
			if (worker) return true;
			if (stopping.load() || !enabled.load()) return false;
			auto context = std::make_shared<worker_context>();
			worker = scheduler::schedule(
				[context]
				{
					return worker_tick(*context);
				});
			return bool(worker);
		}

		bool queue(Work work)
		{
			scheduler::task_handle active_worker;
			{
				std::lock_guard lock(workLock);
				if (!ensure_worker_locked() || outgoing.size() + completed.size() + size_t(work_in_flight) >= work_capacity)
				{
					LOG_WARN("PublisherVars", "publisher work admission rejected; native caller can retry");
					return false;
				}
				outgoing.push_back(std::move(work));
				active_worker = worker;
			}
			active_worker.wake();
			return true;
		}

		void finish(uint64_t id, unsigned status, const std::string& body, uint64_t generation);

		bool hydrate(const Json& response)
		{
			alignas(16) unsigned char value[16]{};
			native<void* (*)(void*)>(0x20E73B0)(value);
			const auto text = response.at("variables").dump();
			bool ok = native<bool (*)(void*, const char*, void*)>(0x20E7C00)(value, text.c_str(), nullptr);
			if (ok)
				ok = native<bool (*)(void*, const char*, void*, uint16_t, uint16_t)>(0x2116090)(cache,
					response.at("namespace").get_ref<const std::string&>().c_str(), native<void* (*)(void*)>(0x20E7A90)(value),
					response.at("major").get<uint16_t>(), response.at("minor").get<uint16_t>());
			native<void (*)(void*)>(0x20E7470)(value);
			return ok;
		}

		unsigned fetch(uintptr_t request, const char* rawName)
		{
			if (!installed) return originalFetch.load()(request, rawName);
			if (!enabled.load() || stopping.load()) return 0;
			const auto name = name_impl(rawName);
			if (!cacheReady || publisher::Index(name) < 0 || read<int>(request) != 0 || read<uintptr_t>(request + 48) != base + 0x107C1D0 ||
				pending.size() >= 7)
				return 0;
			for (const auto& [id, p] : pending)
				if (p.name == name) return 0;
			const auto object = native<uintptr_t (*)(size_t)>(0x20EBA30)(96);
			if (!object) return 0;
			native<void (*)(uintptr_t)>(0x20FDEF0)(object);
			write<long>(object + 8, 2); // TaskCreateRequest and completion-worker ownership.
			write<float>(object + 24, 15.0f);
			write<int>(object + 28, 1);
			write<uintptr_t>(request + 40, object);
			alignas(8) unsigned char result[16]{};
			if (!native<bool (*)(uintptr_t, uintptr_t, void*)>(0x13CC640)(native<uintptr_t (*)()>(0x13CC870)(), request, result))
			{
				release(object);
				return 0; // caller destroys TaskCreateRequest's reference
			}
			const auto id = ++sequence;
			pending.emplace(id, Pending{ object, epoch, GetTickCount64(), name });
			attempted[publisher::Index(name)] = true;
			if (!queue({ id, epoch, currentOrigin, name }))
				finish(id, 503, {}, epoch);
			LOG_INFO("PublisherVars", "native task requested namespace=%s", name.c_str());
			return 1;
		}

		bool iterator_impl(int controller, const char* name, void* iterator)
		{
			if (!installed) return originalIterator.load()(controller, name, iterator);
			const auto index = publisher::Index(name_impl(name));
			return controller == 0 && cacheReady && index >= 0 && cached[index] &&
				native<unsigned (*)(void*, const char*, void*)>(0x2115E30)(cache, name, iterator) == 3;
		}

		bool key_value(int controller, void* iterator, char* key, unsigned keySize, char* value, unsigned valueSize)
		{
			if (!installed) return originalKeyValue.load()(controller, iterator, key, keySize, value, valueSize);
			return controller == 0 && cacheReady &&
				native<unsigned (*)(void*, void*, char*, unsigned, char*, unsigned)>(0x2115EE0)(
					cache, iterator, key, keySize, value, valueSize) == 3;
		}

		void notify(std::string_view name, uint16_t major, uint16_t minor)
		{
			const auto text = std::string(name);
			native<void (*)(uintptr_t, int, const char*, uint16_t, uint16_t)>(0x107BEB0)(base + 0x4596420, 0, text.c_str(), major, minor);
		}

		void finish(uint64_t id, unsigned status, const std::string& body, uint64_t generation)
		{
			const auto found = pending.find(id);
			if (found == pending.end()) return;
			const auto p = found->second;
			pending.erase(found);
			bool success = false;
			uint16_t major = 0, minor = 0;
			if (status == 200 && generation == epoch && p.epoch == epoch && read<int>(p.task + 28) == 1) try
				{
					auto response = Json::parse(body);
					if (publisher::Response(response, p.name))
					{
						major = response["major"].get<uint16_t>();
						minor = response["minor"].get<uint16_t>();
						const auto state = state_impl(publisher::Index(p.name));
						// Never replace newer cached data with a late or regressed response.
						if (major >= read<uint16_t>(state + 4) && minor >= read<uint16_t>(state + 6)) success = hydrate(response);
						if (success) cached[publisher::Index(p.name)] = true;
					}
				}
				catch (...)
				{
				}
			if (read<int>(p.task + 28) == 1)
			{
				write<unsigned>(p.task + 80, success ? 0 : 5);
				write<int>(p.task + 28, success ? 2 : 3);
			}
			LOG_INFO(
				"PublisherVars", "namespace=%s version=%u.%u http=%u completed=%d", p.name.c_str(), major, minor, status, int(success));
			release(p.task);
			if (!success) nextRetry = GetTickCount64() + 30000;
		}

		void frame()
		{
			frontend_backend::Pump();
			if (enabled.load())
			{
				std::wstring origin;
				{
					std::lock_guard lock(originLock);
					origin = configuredOrigin;
				}
				Pump(origin);
			}
			originalFrame.load()();
		}
	}

	void Configure(const std::wstring& origin)
	{
		std::lock_guard lock(originLock);
		const bool changed = configuredOrigin != origin;
		configuredOrigin = origin;
		enabled = !server && !origin.empty();
		if (!enabled.load()) return;
		scheduler::task_handle active_worker;
		{
			std::lock_guard workGuard(workLock);
			if (changed || !worker)
			{
				// Network-only prefetch starts before native namespace/profile readiness.
				std::erase_if(outgoing,
					[](const Work& work)
					{
						return work.prefetch;
					});
				for (auto name : publisher::Names)
					if (outgoing.size() + completed.size() + size_t(work_in_flight) < work_capacity)
						outgoing.push_back({ 0, 0, origin, std::string(name), true });
					else
						LOG_WARN("PublisherVars", "startup download deferred by publisher work capacity; native fetch remains available");
				ensure_worker_locked();
				LOG_INFO("PublisherVars", "startup downloads scheduled origin=%ls worker=%d", origin.c_str(), int(bool(worker)));
			}
			active_worker = worker;
		}
		active_worker.wake();
	}
#ifndef IW8X_CLIENT_ONLY
#include "network/backend/implementation/publisher_variables_request.inl"
#endif
	void Pump(const std::wstring& origin)
	{
		if (!installed || server || stopping.load()) return;
		// Namespace registration and the engine allocator must exist first.
		if (!read<uintptr_t>(base + 0xC3C01D0)) return;
		if (!cacheReady)
		{
			native<void* (*)(void*, uintptr_t, const char*)>(0x2115BD0)(cache, 0, "");
			cacheReady = true;
		}
		if (currentOrigin != origin)
		{
			currentOrigin = origin;
			++epoch;
			nextVersions = 0;
			bootstrapped = false;
		}
		{
			std::lock_guard lock(workLock);
			if (!ensure_worker_locked()) return;
		}
		const auto now = GetTickCount64();
		const bool frontend = native<bool (*)()>(0x10C67A0)();
		// Applying live variables is native frontend-only. A fetch crossing into
		// gameplay may expire, but must not change gameplay dvars mid-match.
		if (frontend)
		{
			for (utils::completion_budget budget; budget.next();)
			{
				Result result;
				{
					std::lock_guard lock(workLock);
					if (completed.empty()) break;
					result = std::move(completed.front());
					completed.pop_front();
				}
				if (!result.work.name.empty())
					finish(result.work.id, result.reply.status, result.reply.body, result.work.epoch);
				else
				{
					versionsPending = false;
					if (result.work.epoch != epoch || result.reply.status != 200) continue;
					try
					{
						const auto j = Json::parse(result.reply.body);
						if (!publisher::Manifest(j))
						{
							LOG_WARN("PublisherVars", "discarded malformed version manifest");
							continue;
						}
						for (auto name : publisher::Names)
						{
							const auto& ns = j.at("namespaces").at(std::string(name));
							const auto state = state_impl(publisher::Index(name));
							// Native entitlement handling deliberately skips paid-user
							// f2p sets. Never turn their zero version into an update popup.
							if (!attempted[publisher::Index(name)]) continue;
							// A failed initial fetch has no baseline for a native
							// major-version comparison. Retry that initial request.
							if (!read<uint16_t>(state + 4))
							{
								if (read<int>(state) == 3 || read<int>(state) == 4) write<int>(state, 1);
								continue;
							}
							if (ns["major"].get<unsigned>() > read<uint16_t>(state + 4) ||
								ns["minor"].get<unsigned>() > read<uint16_t>(state + 6))
								notify(name, ns["major"].get<uint16_t>(), ns["minor"].get<uint16_t>());
						}
					}
					catch (...)
					{
						LOG_WARN("PublisherVars", "discarded malformed version manifest");
					}
				}
			}
		}
		std::vector<uint64_t> expired;
		for (const auto& [id, p] : pending)
			if (now - p.at > 15000) expired.push_back(id);
		for (auto id : expired)
			finish(id, 408, {}, epoch);
		if (!frontend) return;
		if (!bootstrapped || read<int>(base + 0x4596410) == -1 || (!read<int>(state_impl(0)) && pending.empty()))
		{
			native<void (*)(uintptr_t, int, int)>(0x107B930)(base + 0x4596420, 0, 3);
			native<void (*)(int, bool)>(0x107C050)(0, true);
			bootstrapped = true;
			LOG_INFO("PublisherVars", "native namespace fetch enabled for local title service");
		}
		if (now >= nextRetry)
		{
			nextRetry = now + 30000;
			for (int i = 0; i < 7; ++i)
				if (read<int>(state_impl(i)) == 4) write<int>(state_impl(i), 1);
		}
		if (!versionsPending && now >= nextVersions && pending.empty())
		{
			versionsPending = queue({ ++sequence, epoch, currentOrigin, {} });
			nextVersions = now + (versionsPending ? 60000 : 1000);
		}
	}

	void Shutdown()
	{
		// Configure cannot revive this owner until cancellation and cache teardown finish.
		// The transport callback never acquires originLock.
		std::lock_guard lifecycle_guard(originLock);
		enabled = false;
		stopping = true;
		scheduler::task_handle active_worker;
		{
			std::lock_guard lock(workLock);
			active_worker = worker;
			worker = {};
		}
		active_worker.cancel(true);
		{
			std::lock_guard lock(workLock);
			outgoing.clear();
			completed.clear();
			work_in_flight = false;
		}
		for (const auto& [id, p] : pending)
		{
			if (read<int>(p.task + 28) == 1)
			{
				write<unsigned>(p.task + 80, 5);
				write<int>(p.task + 28, 3);
			}
			release(p.task);
		}
		pending.clear();
		++epoch;
		versionsPending = false;
		bootstrapped = false;
		stopping = false;
	}

	bool HandleCommand(std::string_view command)
	{
		if (command == "pubvars_status")
		{
#ifndef IW8X_CLIENT_ONLY
			if (server)
			{
				std::lock_guard lock(backendLock);
				const auto doc = backend.Document();
				OP_INFO("Publisher variables catalog available=%d", int(storageReady));
				for (auto name : publisher::Names)
				{
					const auto& ns = doc["namespaces"][std::string(name)];
					OP_INFO("Publisher variables %s version=%u.%u variables=%zu", name.data(), ns["major"].get<unsigned>(),
						ns["minor"].get<unsigned>(), ns["variables"].size());
				}
				return true;
			}
#endif

			OP_INFO("Publisher variables pending=%zu cache=%d enabled=%d", pending.size(), int(cacheReady), int(enabled.load()));
			if (cacheReady)
				for (int i = 0; i < 7; ++i)
					OP_INFO("Publisher variables %s state=%d version=%u.%u retrieved_flag=%d cached=%d", publisher::Names[i].data(),
						read<int>(state_impl(i)), read<uint16_t>(state_impl(i) + 4), read<uint16_t>(state_impl(i) + 6),
						read<uint8_t>(state_impl(i) + 13), int(cached[i]));
			return true;
		}
#ifndef IW8X_CLIENT_ONLY
		if (command == "pubvars_reload")
		{
			if (!server)
				LOG_WARN("PublisherVars", "pubvars_reload runs on the dedicated server");
			else
			{
				const bool ok = LoadService(true);
				LOG_INFO("PublisherVars", "catalog reload=%d; %s", int(ok),
					ok ? "new versions available" : "invalid/stale configuration; previous catalog retained");
				nextVersions = 0;
			}
			return true;
		}
#endif

		if (command == "pubvars_refresh")
		{
			if (server)
				LOG_INFO("PublisherVars", "use pubvars_reload on the server; pubvars_refresh checks the client cache");
			else
			{
				nextVersions = 0;
				nextRetry = 0;
				LOG_INFO("PublisherVars", "refresh scheduled for frontend");
			}
			return true;
		}
		return false;
	}

	hook::Status Install(uintptr_t image, bool dedicated)
	{
		if (installed) return hook::Status::Installed;
		base = image;
		server = dedicated;
		for (const auto& s : kPublisherBindings)
		{
			unsigned char bytes[16]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + s.rva), bytes, 16) || memcmp(bytes, s.bytes, 16))
			{
				LOG_ERR("PublisherVars", "native binding mismatch RVA=%llx", s.rva);
				return hook::Status::NotReady;
			}
		}
		auto signature = [](uintptr_t rva)
		{
			for (const auto& s : kPublisherBindings)
				if (s.rva == rva) return s.bytes;
			return static_cast<const unsigned char*>(nullptr);
		};
		auto status = hook::Install(reinterpret_cast<void*>(base + 0x1506E50), &fetch, signature(0x1506E50), 16, originalFetch);
		if (status != hook::Status::Installed) return status;
		status = hook::Install(reinterpret_cast<void*>(base + 0x1506B10), &iterator_impl, signature(0x1506B10), 16, originalIterator);
		if (status != hook::Status::Installed) return status;
		status = hook::Install(reinterpret_cast<void*>(base + 0x1506A70), &key_value, signature(0x1506A70), 16, originalKeyValue);
		if (status != hook::Status::Installed) return status;
		if (!server)
		{
			status = hook::Install(reinterpret_cast<void*>(base + 0x107B8A0), &frame, signature(0x107B8A0), 16, originalFrame);
			if (status != hook::Status::Installed) return status;
		}
#ifndef IW8X_CLIENT_ONLY
		if (server && !backend_agent::Enabled())
		{
			storageReady = LoadService(false);
			LOG_INFO("PublisherVars", "dedicated catalog initialized valid=%d", int(storageReady));
		}
#endif

		installed = true;
		return hook::Status::Installed;
	}
}
