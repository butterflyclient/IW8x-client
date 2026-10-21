#include "network/profile/progression_backend.h"
#ifndef MAGMA_BACKEND_STANDALONE
#include "network/backend/backend_agent.h"
#endif
#include "network/backend/tournament_backend.h"
#include "network/profile/progression_account.h"
#include "network/profile/progression_event_queue.h"
#include "network/commerce/store_backend.h"
#include "network/social/social_runtime.h"
#include "network/commerce/unlock_policy.h"
#include "common/embedded_assets.h"
#include "common/console.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <sstream>
#include <ctime>
namespace progression_backend
{
	namespace
	{
		using server_progression::Json;
		server_progression::Catalog catalog;
		std::mutex mutex;
		std::thread worker;
		bool ready = false;
		server_progression::EventQueue events;
		std::atomic<uint64_t> maxSubmitUs = 0, maxTransactionUs = 0;

		void record_maximum(std::atomic<uint64_t>& maximum, std::chrono::steady_clock::time_point start)
		{
			const auto elapsed =
				uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
			auto previous = maximum.load(std::memory_order_relaxed);
			while (previous < elapsed && !maximum.compare_exchange_weak(previous, elapsed, std::memory_order_relaxed))
			{
			}
		}

		struct Entry
		{
			std::unique_ptr<server_progression::Account> account;
			bool damaged = false;
		};

		std::map<uint64_t, Entry> accounts;

		struct Upload
		{
			std::string token, bytes;
			size_t total = 0;
			uint64_t touched = 0;
		};

		std::map<uint64_t, Upload> uploads;

		std::filesystem::path path_impl(uint64_t id)
		{
			return std::filesystem::path(L"players/backend/progression") / (std::to_wstring(id) + L".json");
		}

		bool read(const std::filesystem::path& path, server_progression::Account& account)
		{
			try
			{
				// The checksum wrapper JSON escapes the serialized account payload.
				if (std::filesystem::file_size(path) > 9 * 1024 * 1024) return false;
				std::ifstream f(path, std::ios::binary);
				Json wrapper;
				f >> wrapper;
				const auto bytes = wrapper.at("payload").get<std::string>();
				if (bytes.size() > 4 * 1024 * 1024) return false;
				return wrapper.at("checksum") == std::to_string(challenges::Hash(bytes)) && account.Restore(Json::parse(bytes));
			}
			catch (...)
			{
				return false;
			}
		}

		Entry& load_impl(uint64_t id)
		{
			auto [it, inserted] = accounts.try_emplace(id);
			auto& entry = it->second;
			if (!inserted) return entry;
			const auto path = path_impl(id);
			std::error_code ec;
			const bool primary = std::filesystem::exists(path, ec), backup = std::filesystem::exists(path.wstring() + L".bak", ec);
			if (primary || backup)
			{
				auto candidate = std::make_unique<server_progression::Account>(catalog, id);
				if (read(path, *candidate))
					entry.account = std::move(candidate);
				else if (read(path.wstring() + L".bak", *candidate))
				{
					// Preserve the invalid primary before a later atomic replacement.
					if (!primary ||
						MoveFileExW(path.c_str(), (path.wstring() + L".rejected." + std::to_wstring(GetTickCount64())).c_str(),
							MOVEFILE_WRITE_THROUGH))
						entry.account = std::move(candidate);
					else
						entry.damaged = true;
					LOG_WARN("ServerProgression", "validated backup restored xuid=%llu", id);
				}
				else
					entry.damaged = true;
			}
			return entry;
		}

		bool save(uint64_t id, const server_progression::Account& account)
		{
			const auto path = path_impl(id);
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
			if (ec) return false;
			const auto payload = account.Snapshot().dump();
			if (payload.size() > 4 * 1024 * 1024) return false;
			const auto bytes = Json{ { "payload", payload }, { "checksum", std::to_string(challenges::Hash(payload)) } }.dump();
			const auto temporary = path.wstring() + L".tmp";
			HANDLE f = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (f == INVALID_HANDLE_VALUE) return false;
			DWORD written = 0;
			const bool flushed =
				WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() && FlushFileBuffers(f);
			CloseHandle(f);
			if (!flushed) return false;
			if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
				return ReplaceFileW(path.c_str(), temporary.c_str(), (path.wstring() + L".bak").c_str(), 0, nullptr, nullptr) != FALSE;
			return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
		}

		void run()
		{
			server_progression::EventQueue::Batch batch;
			while (events.Take(batch))
			{
				const auto start = std::chrono::steady_clock::now();
				const auto id = batch.front().xuid;
				bool completed = false;
				{
					// HTTP readers and award transactions still serialize, exposing
					// only durable state. Gameplay submission never takes this lock.
					std::lock_guard lock(mutex);
					auto& entry = load_impl(id);
					if (!entry.account || entry.damaged)
					{
						LOG_ERR("ServerProgression", "event before migration or unreadable account xuid=%llu", id);
						completed = true;
					}
					else
					{
						const auto before = entry.account->Snapshot();
						try
						{
							for (const auto& e : batch)
								entry.account->Apply(e, uint32_t(std::time(nullptr)));
							completed = save(id, *entry.account);
						}
						catch (const std::exception& e)
						{
							LOG_ERR("ServerProgression", "transaction failed xuid=%llu: %s", id, e.what());
						}
						if (!completed) entry.account->Restore(before);
					}
				}
				record_maximum(maxTransactionUs, start);
				// Account events are idempotent. Keep the same batch until both the
				// account and bracket have committed, including a lost bracket write.
				if (completed)
					for (const auto& e : batch)
						if (!tournament_backend::Observe(e))
						{
							completed = false;
							break;
						}
				if (completed)
					events.Complete(batch);
				else
				{
					LOG_ERR("ServerProgression", "write failed; retaining queued awards xuid=%llu", id);
					if (!events.Retry(batch)) return;
				}
			}
		}
	}

	bool Start()
	{
		std::lock_guard guard(mutex);
		if (ready) return true;
		if (!catalog.Load(
				embedded_assets::Find("data/server_progression_catalog.json"), embedded_assets::Find("data/challenge_catalog.json")))
			return false;
		events.Start();
		ready = true;
		worker = std::thread(run);
		LOG_INFO("ServerProgression", "persistent account service ready; gameplay award queue isolated from account IO");
		return true;
	}

	void Stop()
	{
		events.Stop();
		{
			std::lock_guard guard(mutex);
			ready = false;
		}
		if (worker.joinable()) worker.join();
		std::lock_guard guard(mutex);
		accounts.clear();
		uploads.clear();
	}

	bool TournamentWin(uint64_t id, int wins)
	{
		std::lock_guard guard(mutex);
		if (!ready) return false;
		auto& entry = load_impl(id);
		if (!entry.account || entry.damaged) return false;
		const auto before = entry.account->Snapshot();
		if (!entry.account->TournamentWin(wins)) return false;
		if (save(id, *entry.account)) return true;
		entry.account->Restore(before);
		return false;
	}

	std::vector<challenges::Event> CommitBatch(const std::vector<challenges::Event>& input)
	{
		std::map<uint64_t, std::vector<challenges::Event>> groups;
		for (const auto& e : input)
			groups[e.xuid].push_back(e);
		std::vector<challenges::Event> committed;
		for (const auto& [id, group] : groups)
		{
			{
				std::lock_guard guard(mutex);
				if (!ready) break;
				auto& entry = load_impl(id);
				if (entry.damaged || !entry.account) continue;
				const auto before = entry.account->Snapshot();
				try
				{
					for (const auto& e : group)
						entry.account->Apply(e, uint32_t(std::time(nullptr)));
					if (!save(id, *entry.account))
					{
						entry.account->Restore(before);
						continue;
					}
				}
				catch (...)
				{
					entry.account->Restore(before);
					continue;
				}
			}
			bool bracket = true;
			for (const auto& e : group)
				if (!tournament_backend::Observe(e))
				{
					bracket = false;
					break;
				}
			if (bracket) committed.insert(committed.end(), group.begin(), group.end());
		}
		return committed;
	}

	bool Commit(const challenges::Event& e)
	{
		return CommitBatch({ e }).size() == 1;
	}

	int Owns(uint64_t id, int item)
	{
		std::lock_guard guard(mutex);
		if (!ready) return -1;
		auto& e = load_impl(id);
		return e.damaged || !e.account ? -1 : e.account->Owns(item) ? 1 : 0;
	}

	void Receive(const challenges::Event& e)
	{
#ifndef MAGMA_BACKEND_STANDALONE
		if (backend_agent::Enabled())
		{
			backend_agent::Event(e);
			return;
		}
#endif
		if (!e.xuid) return;
		const auto start = std::chrono::steady_clock::now();
		const auto result = events.Push(e);
		record_maximum(maxSubmitUs, start);
		if (result == server_progression::EventQueue::Result::Full)
			LOG_ERR("ServerProgression", "award queue exhausted xuid=%llu sequence=%llu", e.xuid, e.sequence);
	}

	int TryOwns(uint64_t id, int item)
	{
#ifndef MAGMA_BACKEND_STANDALONE
		if (backend_agent::Enabled()) return backend_agent::Owns(id, item);
#endif
		std::unique_lock guard(mutex, std::try_to_lock);
		if (!guard.owns_lock() || !ready) return -1;
		const auto it = accounts.find(id);
		if (it == accounts.end() || it->second.damaged || !it->second.account) return -1;
		return it->second.account->Owns(item) ? 1 : 0;
	}

	unsigned Request(uint64_t id, const Json& request, Json& response)
	{
		std::lock_guard guard(mutex);
		if (!ready) return 503;
		server_progression::Account* changedAccount = nullptr;
		Json rollback;
		try
		{
			auto& entry = load_impl(id);
			if (entry.damaged) return 503;
			const auto action = request.value("action", std::string{});
			const auto now = uint32_t(std::time(nullptr));
			for (auto it = uploads.begin(); it != uploads.end();)
				if (GetTickCount64() - it->second.touched > 300000)
					it = uploads.erase(it);
				else
					++it;
			if (action == "import" && !entry.account)
			{
				const auto token = request.at("upload").get<std::string>(), chunk = request.at("chunk").get<std::string>();
				const auto offset = request.at("offset").get<size_t>(), total = request.at("total").get<size_t>();
				if (token.empty() || token.size() > 64 || chunk.empty() || chunk.size() > 2048 || total == 0 || total > 4 * 1024 * 1024 ||
					offset > total || chunk.size() > total - offset)
					return 400;
				if (!uploads.contains(id) && uploads.size() >= 32) return 503;
				auto& u = uploads[id];
				if (u.token != token)
				{
					if (offset) return 409;
					u = { token, {}, total, 0 };
				}
				if (u.total != total) return 409;
				u.touched = GetTickCount64();
				if (offset == u.bytes.size())
					u.bytes += chunk;
				else if (offset > u.bytes.size() || u.bytes.substr(offset, chunk.size()) != chunk)
					return 409;
				if (u.bytes.size() == total)
				{
					auto candidate = std::make_unique<server_progression::Account>(catalog, id);
					if (!candidate->Import(Json::parse(u.bytes), now))
					{
						uploads.erase(id);
						return 422;
					}
					if (!save(id, *candidate)) return 503;
					entry.account = std::move(candidate);
					uploads.erase(id);
					LOG_INFO("ServerProgression", "one-time migration committed xuid=%llu", id);
				}
				else
				{
					response["progression"] = { { "needs_import", true }, { "offset", u.bytes.size() }, { "utc", now } };
					return 200;
				}
			}
			if (action != "sync" && action != "import" && action != "activate" && action != "ack" && action != "aar_ack" &&
				action != "commerce" && action != "token" && action != "unlock")
				return 400;
			if (!entry.account)
			{
				response["progression"] = { { "needs_import", true }, { "offset", 0 }, { "utc", now } };
				return 200;
			}
			const auto before = entry.account->Snapshot();
			rollback = before;
			changedAccount = entry.account.get();
			bool changed = entry.account->Day(now);
			unsigned status = 200;
			if (action == "unlock")
			{
				if (!request.at("enabled").is_boolean() || !request.at("revision").is_number_unsigned())
				{
					entry.account->Restore(before);
					return 400;
				}
				status = entry.account->SetUnlock(request.at("enabled").get<bool>(), request.at("revision").get<uint64_t>());
				changed = changed || entry.account->Snapshot().at("revision") != before.at("revision");
			}
			if (action == "token")
			{
				Json result;
				status = entry.account->ActivateToken(request, now, result);
				if (status != 200 && status != 409)
				{
					entry.account->Restore(before);
					return status;
				}
				response["commerce_result"] = result;
				changed = changed || entry.account->Snapshot().at("revision") != before.at("revision");
			}
			if (action == "commerce")
			{
				const auto product = request.value("product", std::string{});
				const auto number = challenges::Number(product, 0);
				Json result;
				status =
					entry.account->Trade(request, number > 0 && number <= 100000000 && store_backend::Published(uint32_t(number)), result);
				if (status != 200 && status != 409)
				{
					entry.account->Restore(before);
					return status;
				}
				response["commerce_result"] = result;
				changed = changed || entry.account->Snapshot().at("revision") != before.at("revision");
			}
			if (action == "activate")
			{
				if (!entry.account->Activate(request.at("mission").get<int>()))
				{
					entry.account->Restore(before);
					return 422;
				}
				changed = true;
			}
			if (action == "ack") changed = entry.account->Ack(request.at("sequence").get<uint64_t>()) || changed;
			if (action == "aar_ack") changed = entry.account->AckReport(request.at("id").get<std::string>()) || changed;
			if (changed && !save(id, *entry.account))
			{
				entry.account->Restore(before);
				return 503;
			}
			response["progression"] = { { "needs_import", false }, { "utc", now }, { "account", entry.account->Snapshot() } };
			response["currency_packs"] = commerce::Packs();
			return status;
		}
		catch (...)
		{
			if (changedAccount) changedAccount->Restore(rollback);
			return 400;
		}
	}

	bool Command(std::string_view command, bool server)
	{
		if (const auto parsed = unlock_policy::Parse(command))
		{
			if (!server)
			{
				OP_WARN("unlock all / relock all are dedicated-server commands");
				return true;
			}
			const auto id = social_runtime::ResolveAccountName(parsed->name);
			if (!id)
			{
				OP_WARN("Unknown or ambiguous player. Use unlock all <name#0> or relock all <name#0>");
				return true;
			}
			std::lock_guard guard(mutex);
			if (!ready)
			{
				OP_WARN("Progression service not ready");
				return true;
			}
			auto& entry = load_impl(id);
			if (entry.damaged || !entry.account)
			{
				OP_WARN("Player must complete their first authenticated progression sync");
				return true;
			}
			const auto before = entry.account->Snapshot();
			if (entry.account->SetUnlock(parsed->enabled, before.at("revision").get<uint64_t>()) != 200 || !save(id, *entry.account))
			{
				entry.account->Restore(before);
				OP_WARN("Unlock policy was not saved; previous state retained");
			}
			else
				OP_INFO("Unlock all %s for %llu; earned progression preserved", parsed->enabled ? "enabled" : "disabled", id);
			return true;
		}
		if (command == "progression_perf")
		{
			if (!server)
			{
				OP_WARN("progression_perf is a dedicated-server command");
				return true;
			}
			const auto s = events.GetStatus();
			OP_INFO(
				"Progression queue: accepted=%llu completed=%llu pending=%zu peak=%zu retries=%llu rejected=%llu submitMaxUs=%llu transactionMaxUs=%llu",
				s.accepted, s.completed, s.pending, s.highWater, s.retries, s.rejected, maxSubmitUs.load(), maxTransactionUs.load());
			return true;
		}
		if (!command.starts_with("progression_premium")) return false;
		if (!server)
		{
			OP_WARN("progression_premium is a dedicated-server command");
			return true;
		}
		std::istringstream input{ std::string(command) };
		std::string name, extra;
		uint64_t id = 0;
		int own = -1;
		if (!(input >> name >> id >> own) || name != "progression_premium" || !id || (own != 0 && own != 1) || (input >> extra))
		{
			OP_WARN("Usage: progression_premium <xuid> <0|1>");
			return true;
		}
		std::lock_guard guard(mutex);
		auto& e = load_impl(id);
		if (!ready || e.damaged || !e.account)
		{
			OP_WARN("Player must finish progression migration first");
			return true;
		}
		const auto before = e.account->Snapshot();
		e.account->Premium(own == 1);
		if (!save(id, *e.account))
		{
			e.account->Restore(before);
			OP_WARN("Premium update failed; previous state retained");
		}
		else
			OP_INFO("Season 3 premium=%d committed for %llu", own, id);
		return true;
	}
}
