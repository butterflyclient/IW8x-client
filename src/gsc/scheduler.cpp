#include "api.hpp"
#include "gsc/internal/state.hpp"
#include "gsc/internal/bindings.hpp"

#include <atomic>
#include <optional>
namespace iw8x::gsc::internal
{
	namespace
	{
		std::atomic_flag pumping[2]{};
		constexpr size_t batch_limit = 64;
		constexpr auto game_budget = std::chrono::milliseconds(2);

		iw8x_status add_task(iw8x_plugin_id id, uint32_t delay, uint32_t scope, iw8x_task_callback work, iw8x_task_callback complete,
			bool worker, void* data, iw8x_destroy_callback destroy, iw8x_registration* output)
		{
			if (!work || !output || scope > IW8X_SCOPE_MAP || (worker && !complete)) return IW8X_INVALID_ARGUMENT;
			const auto cap = worker ? IW8X_CAP_ASYNC : IW8X_CAP_GAME_TASKS;
			if (!(bindings().capabilities & cap)) return IW8X_UNSUPPORTED;
			const auto plugin = find_owner(id);
			lease active(plugin);
			if (!active) return IW8X_STOPPING;
			auto task_ptr = std::make_shared<task>();
			auto& s = state();
			std::lock_guard lock(s.mutex);
			if (scope == IW8X_SCOPE_MAP && !s.map_active) return IW8X_STALE;
			if (s.tasks.size() >= 4096 || !s.next_registration) return IW8X_LIMIT;
			*task_ptr = { { plugin, data, destroy }, s.next_registration++, scope, s.epoch,
				std::chrono::steady_clock::now() + std::chrono::milliseconds(delay), work, complete, worker };
			s.tasks.emplace(task_ptr->id, task_ptr);
			s.task_wakeup[worker] = (std::min)(s.task_wakeup[worker], task_ptr->due);
			s.task_wakeup_ticks[worker].store(s.task_wakeup[worker].time_since_epoch().count(), std::memory_order_release);
			if (worker) iw8x::gsc::notify_worker();
			*output = task_ptr->id;
			return IW8X_OK;
		}
	}

	iw8x_status schedule(iw8x_plugin_id id, uint32_t delay, uint32_t scope, iw8x_task_callback callback, void* data,
		iw8x_destroy_callback destroy, iw8x_registration* out)
	{
		return add_task(id, delay, scope, callback, nullptr, false, data, destroy, out);
	}

	iw8x_status async(iw8x_plugin_id id, uint32_t scope, iw8x_task_callback work, iw8x_task_callback complete, void* data,
		iw8x_destroy_callback destroy, iw8x_registration* out)
	{
		return add_task(id, 0, scope, work, complete, true, data, destroy, out);
	}

	iw8x_status cancel_task(iw8x_plugin_id id, iw8x_registration token)
	{
		const auto plugin = find_owner(id);
		lease active(plugin);
		if (!active) return IW8X_STOPPING;
		std::shared_ptr<task> removed;
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			const auto it = s.tasks.find(token);
			if (it == s.tasks.end() || it->second->plugin != plugin) return IW8X_NOT_FOUND;
			if (it->second->running) return IW8X_BUSY;
			removed = it->second;
			s.tasks.erase(it);
		}
		destroy_data(*removed);
		return IW8X_OK;
	}

	void pump_tasks(bool worker)
	{
		if (!worker && !in_game_scope()) return;
		auto& busy = pumping[worker];
		if (busy.test_and_set(std::memory_order_acquire)) return;

		struct reset
		{
			std::atomic_flag& busy;

			~reset()
			{
				busy.clear(std::memory_order_release);
			}
		} guard{ busy };

		const auto start = std::chrono::steady_clock::now();
		if (start.time_since_epoch().count() < state().task_wakeup_ticks[worker].load(std::memory_order_acquire)) return;
		slow_scope pump_timer(worker ? "pump.worker" : "pump.game", {}, 0, 0, 10000);
		std::array<iw8x_registration, batch_limit> ready{};
		size_t ready_count{};
		auto& s = state();
		{
			std::lock_guard lock(s.mutex);
			if (start < s.task_wakeup[worker]) return;
			auto next = std::chrono::steady_clock::time_point::max();
			for (const auto& [id, t] : s.tasks)
			{
				if (t->running || t->worker != worker) continue;
				next = (std::min)(next, t->due);
				if (start >= t->due)
				{
					ready[ready_count++] = id;
					if (ready_count == ready.size()) break;
				}
			}
			s.task_wakeup[worker] = next;
			s.task_wakeup_ticks[worker].store(next.time_since_epoch().count(), std::memory_order_release);
		}
		// Snapshot IDs: tasks scheduled by callbacks wait until a later pump.
		for (size_t i = 0; i < ready_count; ++i)
		{
			// Never preempt engine work. Finish a callback, then yield the rest of
			// a burst to the next frame; a single slow callback is profiled below.
			if (!worker && i && std::chrono::steady_clock::now() - start >= game_budget) break;
			const auto id = ready[i];
			std::shared_ptr<task> current;
			std::optional<lease> active;
			{
				std::lock_guard lock(s.mutex);
				const auto it = s.tasks.find(id);
				if (it == s.tasks.end() || it->second->running || it->second->worker != worker ||
					it->second->due > std::chrono::steady_clock::now())
					continue;
				current = it->second;
				active.emplace(current->plugin);
				if (!*active) continue;
				current->running = true;
			}
			slow_sample("task.late", current->plugin->name, current->plugin->id, id,
				static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - current->due).count()),
				100000);
			slow_scope task_timer(worker ? "task.worker" : "task.game", current->plugin->name, current->plugin->id, id);
			const bool ok = guarded(current->plugin,
				[&]
				{
					current->work(current->data);
				});
			bool finished;
			{
				std::lock_guard lock(s.mutex);
				finished = !ok || !worker || current->canceled ||
					(current->scope == IW8X_SCOPE_MAP && (current->epoch != s.epoch || !s.map_active));
				if (finished)
					s.tasks.erase(id);
				else
				{
					current->work = current->complete;
					current->worker = false;
					current->running = false;
					s.task_wakeup[0] = (std::min)(s.task_wakeup[0], current->due);
					s.task_wakeup_ticks[0].store(s.task_wakeup[0].time_since_epoch().count(), std::memory_order_release);
				}
			}
			if (finished) destroy_data(*current);
		}
	}

	void end_map()
	{
		std::vector<owned_data> removed;
		std::vector<lease> active;
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			removed.reserve(s.tasks.size() + s.resources.size());
			active.reserve(s.tasks.size() + s.resources.size());
			s.map_active = false;
			++s.epoch;
			for (auto it = s.tasks.begin(); it != s.tasks.end();)
			{
				auto& t = *it->second;
				if (t.scope != IW8X_SCOPE_MAP)
				{
					++it;
					continue;
				}
				if (t.running)
				{
					t.canceled = true;
					++it;
					continue;
				}
				active.emplace_back(t.plugin);
				if (!active.back())
				{
					active.pop_back();
					++it;
					continue;
				} // Unload owns cleanup.
				removed.push_back(t);
				it = s.tasks.erase(it);
			}
			for (auto& r : s.resources)
				if (r.occupied && r.scope == IW8X_SCOPE_MAP && !r.borrows)
				{
					active.emplace_back(r.plugin);
					if (!active.back())
					{
						active.pop_back();
						continue;
					}
					removed.push_back(r);
					retire_resource(r);
				}
		}
		for (const auto& data : removed)
			destroy_data(data);
	}
}

namespace iw8x::gsc
{
	namespace
	{
		std::atomic<void (*)() noexcept> worker_notify{};
	}
	void set_worker_wakeup(void (*notify)() noexcept) noexcept
	{
		worker_notify.store(notify, std::memory_order_release);
	}
	void notify_worker() noexcept
	{
		if (const auto notify = worker_notify.load(std::memory_order_acquire)) notify();
	}
	std::chrono::milliseconds worker_delay() noexcept
	{
		if (internal::logs_pending()) return std::chrono::milliseconds(0);
		auto next = std::chrono::steady_clock::now() + std::chrono::hours(1);
		{
			auto& state = internal::state();
			std::lock_guard lock(state.mutex);
			for (const auto& [id, task] : state.tasks)
			{
				if (!task->worker || task->running) continue;
				std::lock_guard owner_lock(task->plugin->mutex);
				if (task->plugin->accepting) next = (std::min)(next, task->due);
			}
		}
		const auto now = std::chrono::steady_clock::now();
		if (next <= now) return std::chrono::milliseconds(0);
		return std::chrono::ceil<std::chrono::milliseconds>(next - now);
	}
}
