#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace utils
{
	class thread_pool
	{
		struct task_state;
		struct pool_state;

	public:
		using clock = std::chrono::steady_clock;
		using milliseconds = std::chrono::milliseconds;
		using callback = std::function<std::optional<milliseconds>()>;

		class task_handle
		{
		public:
			task_handle() = default;
			explicit operator bool() const;
			// Stable admission receipt; bool describes whether work is still live.
			bool accepted() const noexcept { return bool(state_); }
			void wake() const;
			// Waiting is external-only: callbacks request cancellation without joining peers.
			void cancel(bool wait = false) const;
			// True only while the callback executes; bool also includes queued work.
			bool running() const;

		private:
			friend class thread_pool;
			explicit task_handle(std::shared_ptr<task_state> state);
			std::shared_ptr<task_state> state_;
		};

		struct statistics
		{
			size_t workers = 0, queued = 0, active = 0, high_water = 0;
			uint64_t completed = 0, rejected = 0, exceptions = 0, wakeups = 0;
		};

		thread_pool();
		~thread_pool();
		thread_pool(const thread_pool&) = delete;
		thread_pool& operator=(const thread_pool&) = delete;

		bool start(size_t workers = 0, size_t capacity = 128);
		task_handle schedule(callback work, milliseconds delay = milliseconds{ 0 });
		// Draining runs each queued callback once immediately; repeats are retired.
		// The owner must stop/join outside loader callbacks and before its destruction.
		void stop(bool drain = false);
		statistics status() const;

	private:
		static void run(const std::shared_ptr<pool_state>& state);
		static std::vector<std::shared_ptr<task_state>> retire_queued(const std::shared_ptr<pool_state>& state, bool drain);
		std::shared_ptr<pool_state> state_;
		std::mutex lifetime_mutex_;
		std::vector<std::thread> workers_;
	};
}
