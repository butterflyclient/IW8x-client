#include "scheduler.hpp"
#include "client/loader/component_loader.hpp"
#include "common/console.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <deque>

namespace scheduler
{
	namespace
	{
		struct runtime
		{
			utils::thread_pool pool;
			std::mutex lifecycle_mutex, game_mutex;
			std::deque<std::function<void()>> game_queue;
			bool initialized = false;
			std::atomic<bool> stopping{ false };
			std::atomic<bool> game_pending{ false };
			std::atomic_flag pumping = ATOMIC_FLAG_INIT;
		};

		runtime& state()
		{
			// The pinned proxy outlives native shutdown. Never join under the loader
			// lock during static destruction; pre_destroy owns cooperative shutdown.
			static auto* instance = new runtime;
			return *instance;
		}
		constexpr size_t game_capacity = 256, game_batch = 64;
		constexpr auto game_budget = std::chrono::milliseconds(1);
	}

	bool initialize()
	{
		auto& s = state();
		std::lock_guard lock(s.lifecycle_mutex);
		if (s.stopping) return false;
		if (s.initialized) return true;
		wchar_t path[MAX_PATH]{};
		size_t workers = 0, capacity = 128;
		if (GetModuleFileNameW(nullptr, path, MAX_PATH))
		{
			if (auto* tail = wcsrchr(path, L'\\'))
			{
				wcscpy_s(tail + 1, MAX_PATH - size_t(tail + 1 - path), L"mw120rproxy.ini");
				workers = GetPrivateProfileIntW(L"scheduler", L"worker_threads", 0, path);
				capacity = std::clamp<size_t>(GetPrivateProfileIntW(L"scheduler", L"queue_capacity", 128, path), 16, 4096);
			}
		}
		s.initialized = s.pool.start(workers, capacity);
		if (s.initialized)
			LOG_INFO("Scheduler", "shared workers=%zu capacity=%zu; native completions budget=1ms/64",
				s.pool.status().workers, capacity);
		return s.initialized;
	}

	task_handle schedule(callback work, milliseconds delay)
	{
		if (!initialize()) return {};
		auto& s = state();
		std::lock_guard lock(s.lifecycle_mutex);
		if (s.stopping) return {};
		return s.pool.schedule(std::move(work), delay);
	}

	bool post_game(std::function<void()> work)
	{
		if (!work) return false;
		auto& s = state();
		std::lock_guard lock(s.game_mutex);
		if (s.stopping || s.game_queue.size() >= game_capacity) return false;
		s.game_queue.push_back(std::move(work));
		s.game_pending.store(true, std::memory_order_release);
		return true;
	}

	void pump_game()
	{
		auto& s = state();
		if (!s.game_pending.load(std::memory_order_acquire)) return;
		if (s.pumping.test_and_set(std::memory_order_acquire)) return;
		struct reset
		{
			std::atomic_flag& flag;
			~reset() { flag.clear(std::memory_order_release); }
		} guard{ s.pumping };
		const auto start = utils::thread_pool::clock::now();
		for (size_t count = 0; count < game_batch; ++count)
		{
			if (count && utils::thread_pool::clock::now() - start >= game_budget) break;
			std::function<void()> work;
			{
				std::lock_guard lock(s.game_mutex);
				if (s.game_queue.empty()) break;
				work = std::move(s.game_queue.front());
				s.game_queue.pop_front();
				if (s.game_queue.empty()) s.game_pending.store(false, std::memory_order_release);
			}
			try { work(); }
			catch (...) { LOG_ERR("Scheduler", "native completion threw"); }
		}
	}

	void shutdown()
	{
		auto& s = state();
		{
			std::lock_guard lock(s.lifecycle_mutex);
			s.stopping = true;
		}
		std::deque<std::function<void()>> canceled;
		{
			std::lock_guard lock(s.game_mutex);
			canceled.swap(s.game_queue);
			s.game_pending.store(false, std::memory_order_release);
		}
		s.pool.stop();
	}

	utils::thread_pool::statistics status()
	{
		return state().pool.status();
	}

	class component final : public component_interface
	{
	public:
		hook::Status post_start() override
		{
			return initialize() ? hook::Status::Installed : hook::Status::Failed;
		}
		void pre_destroy() override { shutdown(); }
		component_priority priority() override { return component_priority::scheduler; }
	};
}
REGISTER_COMPONENT(scheduler::component)
