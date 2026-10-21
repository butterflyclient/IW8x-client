#include "gsc/internal/diagnostics.hpp"
#include "ownership.hpp"
#include "api.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
namespace iw8x::gsc::internal
{
	namespace
	{
		struct line
		{
			char plugin[129]{};
			char message[2049]{};
		};

		std::array<line, 256> pending;
		std::mutex queue_mutex;
		size_t head{}, count{};
		std::atomic<uint64_t> dropped{};
		std::atomic<log_sink> logger{};
		std::atomic_flag draining = ATOMIC_FLAG_INIT;
		thread_local iw8x_plugin_id current_owner{};

		void copy(char* out, size_t capacity, std::string_view text) noexcept
		{
			const auto length = (std::min)(capacity - 1, text.size());
			if (length) std::memcpy(out, text.data(), length);
			out[length] = 0;
		}
	}

	iw8x_plugin_id diagnostic_owner() noexcept
	{
		return current_owner;
	}

	iw8x_plugin_id diagnostic_owner(iw8x_plugin_id id) noexcept
	{
		const auto previous = current_owner;
		current_owner = id;
		return previous;
	}

	void set_log_sink(log_sink sink)
	{
		logger.store(sink);
	}

	bool logs_pending() noexcept
	{
		std::lock_guard lock(queue_mutex);
		return count != 0 || dropped.load(std::memory_order_relaxed) != 0;
	}

	void queue_log(std::string_view plugin, std::string_view message) noexcept
	{
		if (!logger.load(std::memory_order_relaxed)) return;
		std::unique_lock lock(queue_mutex, std::try_to_lock);
		if (!lock || count == pending.size())
		{
			++dropped;
			if (lock.owns_lock()) lock.unlock();
			iw8x::gsc::notify_worker();
			return;
		}
		auto& item = pending[(head + count++) % pending.size()];
		copy(item.plugin, sizeof(item.plugin), plugin);
		copy(item.message, sizeof(item.message), message);
		lock.unlock();
		iw8x::gsc::notify_worker();
	}

	void report(const std::shared_ptr<owner>& plugin, const char* message) noexcept
	{
		queue_log(plugin ? std::string_view(plugin->name) : "host", message ? message : "");
	}

	void drain_logs() noexcept
	{
		if (draining.test_and_set(std::memory_order_acquire)) return;

		struct reset
		{
			~reset()
			{
				draining.clear(std::memory_order_release);
			}
		} guard;

		// Cap output to 16 lines per worker tick. Release the queue lock before I/O
		// or calling an external sink; producers and other plugins keep progressing.
		for (unsigned i = 0; i < 16; ++i)
		{
			line item;
			{
				std::lock_guard lock(queue_mutex);
				if (!count) break;
				item = pending[head];
				head = (head + 1) % pending.size();
				--count;
			}
			try
			{
				if (const auto sink = logger.load()) sink(item.plugin, item.message);
			}
			catch (...)
			{
			}
		}
		if (const auto lost = dropped.exchange(0))
		{
			char message[128];
			std::snprintf(message, sizeof(message), "[PluginPerf] log queue saturated/contended: dropped=%llu", lost);
			try
			{
				if (const auto sink = logger.load()) sink("host", message);
			}
			catch (...)
			{
			}
		}
	}

	void slow_sample(const char* operation, std::string_view name, iw8x_plugin_id owner, uint64_t registration, uint64_t elapsed_us,
		uint64_t threshold_us) noexcept
	{
		if (elapsed_us < threshold_us) return;
		char message[640];
		std::snprintf(message, sizeof(message), "[PluginPerf] op=%s owner=%u id=%llu name=%.*s elapsed_us=%llu threshold_us=%llu",
			operation, owner, registration, static_cast<int>((std::min)(name.size(), size_t{ 384 })), name.empty() ? "" : name.data(),
			elapsed_us, threshold_us);
		queue_log("host", message);
	}
}
