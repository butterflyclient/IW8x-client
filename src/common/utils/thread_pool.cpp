#include "common/utils/thread_pool.hpp"

#include <algorithm>
#include <condition_variable>
#include <utility>

namespace utils
{
	namespace
	{
		thread_local const void* current_pool = nullptr;
		thread_local const void* current_task = nullptr;

		thread_pool::clock::time_point due_after(thread_pool::milliseconds delay)
		{
			const auto now = thread_pool::clock::now();
			if (delay <= thread_pool::milliseconds::zero()) return now;
			const auto remaining = std::chrono::duration_cast<thread_pool::milliseconds>(thread_pool::clock::time_point::max() - now);
			return delay >= remaining ? thread_pool::clock::time_point::max() : now + delay;
		}
	}

	struct thread_pool::task_state
	{
		std::weak_ptr<pool_state> owner;
		callback work;
		clock::time_point due{};
		uint64_t epoch = 0;
		bool queued = false, active = false, cancelled = false, wake_pending = false, owns_slot = false;
	};

	struct thread_pool::pool_state
	{
		std::mutex mutex;
		std::condition_variable wake;
		std::vector<std::shared_ptr<task_state>> queued;
		statistics counters;
		size_t capacity = 0;
		uint64_t epoch = 0;
		bool accepting = false, stopping = true, drain = false;
	};

	thread_pool::task_handle::task_handle(std::shared_ptr<task_state> state)
		: state_(std::move(state))
	{
	}

	thread_pool::task_handle::operator bool() const
	{
		if (!state_) return false;
		const auto owner = state_->owner.lock();
		if (!owner) return false;
		std::lock_guard lock(owner->mutex);
		return state_->epoch == owner->epoch && state_->owns_slot && !state_->cancelled;
	}

	bool thread_pool::task_handle::running() const
	{
		if (!state_) return false;
		const auto owner = state_->owner.lock();
		if (!owner) return false;
		std::lock_guard lock(owner->mutex);
		return state_->epoch == owner->epoch && state_->active;
	}

	void thread_pool::task_handle::wake() const
	{
		if (!state_) return;
		const auto owner = state_->owner.lock();
		if (!owner) return;
		{
			std::lock_guard lock(owner->mutex);
			if (state_->epoch != owner->epoch || !owner->accepting || !state_->owns_slot || state_->cancelled) return;
			if (state_->active)
				state_->wake_pending = true;
			else if (state_->queued)
				state_->due = clock::now();
		}
		owner->wake.notify_all();
	}

	void thread_pool::task_handle::cancel(bool wait) const
	{
		if (!state_) return;
		const auto owner = state_->owner.lock();
		if (!owner) return;
		callback retired;
		{
			std::unique_lock lock(owner->mutex);
			if (state_->epoch != owner->epoch) return;
			state_->cancelled = true;
			state_->wake_pending = false;
			if (state_->queued)
			{
				const auto found = std::find(owner->queued.begin(), owner->queued.end(), state_);
				if (found != owner->queued.end()) owner->queued.erase(found);
				state_->queued = false;
				state_->owns_slot = false;
				retired = std::move(state_->work);
			}
			owner->wake.notify_all();
			if (wait && current_pool != owner.get())
				owner->wake.wait(lock, [&] { return !state_->active; });
		}
		// Captured resources can reenter the pool during destruction.
	}

	thread_pool::thread_pool() : state_(std::make_shared<pool_state>())
	{
	}

	thread_pool::~thread_pool()
	{
		stop();
	}

	bool thread_pool::start(size_t workers, size_t capacity)
	{
		if (current_pool == state_.get()) return false;
		std::unique_lock lifetime_lock(lifetime_mutex_);
		if (!workers_.empty() || !capacity) return false;
		const auto count = workers ? std::clamp<size_t>(workers, 1, 8)
			: std::clamp<size_t>(std::thread::hardware_concurrency() / 8, 1, 4);
		try
		{
			workers_.reserve(count);
			{
				std::lock_guard lock(state_->mutex);
				state_->queued.reserve(capacity);
				++state_->epoch;
				state_->capacity = capacity;
				state_->counters = {};
				state_->counters.workers = count;
				state_->accepting = false;
				state_->stopping = false;
				state_->drain = false;
			}
			for (size_t index = 0; index < count; ++index)
				workers_.emplace_back([state = state_] { run(state); });
			{
				std::lock_guard lock(state_->mutex);
				state_->accepting = true;
			}
			return true;
		}
		catch (...)
		{
			auto retired = retire_queued(state_, false);
			state_->wake.notify_all();
			for (auto& worker : workers_)
				if (worker.joinable()) worker.join();
			workers_.clear();
			{
				std::lock_guard lock(state_->mutex);
				state_->counters.workers = 0;
			}
			lifetime_lock.unlock();
			for (auto& task : retired) task->work = {};
			return false;
		}
	}

	thread_pool::task_handle thread_pool::schedule(callback work, milliseconds delay)
	{
		if (!work) return {};
		auto task = std::make_shared<task_state>();
		task->owner = state_;
		task->work = std::move(work);
		task->due = due_after(delay);
		{
			std::lock_guard lock(state_->mutex);
			if (!state_->accepting || state_->queued.size() + state_->counters.active >= state_->capacity)
			{
				++state_->counters.rejected;
				return {};
			}
			task->epoch = state_->epoch;
			task->queued = true;
			task->owns_slot = true;
			state_->queued.push_back(task);
			state_->counters.high_water = std::max(state_->counters.high_water, state_->queued.size() + state_->counters.active);
		}
		state_->wake.notify_all();
		return task_handle(std::move(task));
	}

	std::vector<std::shared_ptr<thread_pool::task_state>> thread_pool::retire_queued(const std::shared_ptr<pool_state>& state, bool drain)
	{
		std::vector<std::shared_ptr<task_state>> retired;
		std::lock_guard lock(state->mutex);
		state->accepting = false;
		state->stopping = true;
		state->drain = drain;
		if (!drain)
		{
			retired.swap(state->queued);
			for (auto& task : retired)
			{
				task->queued = false;
				task->cancelled = true;
				task->owns_slot = false;
			}
		}
		return retired;
	}

	void thread_pool::stop(bool drain)
	{
		// A callback may request a stop, but only an external owner can join it.
		if (current_pool == state_.get())
		{
			auto retired = retire_queued(state_, drain);
			state_->wake.notify_all();
			for (auto& task : retired) task->work = {};
			return;
		}
		std::unique_lock lifetime_lock(lifetime_mutex_);
		auto retired = retire_queued(state_, drain);
		state_->wake.notify_all();
		for (auto& worker : workers_)
			if (worker.joinable()) worker.join();
		workers_.clear();
		{
			std::lock_guard lock(state_->mutex);
			state_->counters.workers = 0;
		}
		lifetime_lock.unlock();
		for (auto& task : retired) task->work = {};
	}

	thread_pool::statistics thread_pool::status() const
	{
		std::lock_guard lock(state_->mutex);
		auto result = state_->counters;
		result.queued = state_->queued.size();
		return result;
	}

	void thread_pool::run(const std::shared_ptr<pool_state>& state)
	{
		current_pool = state.get();
		for (;;)
		{
			std::shared_ptr<task_state> task;
			{
				std::unique_lock lock(state->mutex);
				for (;;)
				{
					if (state->stopping && (!state->drain || state->queued.empty()))
					{
						current_pool = nullptr;
						return;
					}
					if (state->queued.empty())
					{
						state->wake.wait(lock);
						++state->counters.wakeups;
						continue;
					}
					const auto next = std::min_element(state->queued.begin(), state->queued.end(),
						[](const auto& left, const auto& right) { return left->due < right->due; });
					if (!state->stopping && (*next)->due > clock::now())
					{
						const auto due = (*next)->due;
						state->wake.wait_until(lock, due);
						++state->counters.wakeups;
						continue;
					}
					task = std::move(*next);
					state->queued.erase(next);
					task->queued = false;
					task->active = true;
					++state->counters.active;
					break;
				}
			}
			std::optional<milliseconds> repeat;
			bool failed = false;
			current_task = task.get();
			try
			{
				repeat = task->work();
			}
			catch (...)
			{
				failed = true;
			}
			current_task = nullptr;
			callback retired;
			{
				std::lock_guard lock(state->mutex);
				--state->counters.active;
				task->active = false;
				++state->counters.completed;
				if (failed) ++state->counters.exceptions;
				if (repeat && !failed && state->accepting && !task->cancelled && task->epoch == state->epoch)
				{
					task->due = task->wake_pending ? clock::now() : due_after(*repeat);
					task->wake_pending = false;
					task->queued = true;
					state->queued.push_back(task);
				}
				else
				{
					task->owns_slot = false;
					retired = std::move(task->work);
				}
			}
			state->wake.notify_all();
		}
	}
}
