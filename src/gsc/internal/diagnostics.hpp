#pragma once
#include "plugin/sdk/abi.h"

#include <chrono>
#include <mutex>
#include <string_view>
namespace iw8x::gsc::internal
{
	// Only slow samples are queued. Neither timing nor log producers wait for I/O.
	void queue_log(std::string_view plugin, std::string_view message) noexcept;
	void drain_logs() noexcept;
	bool logs_pending() noexcept;
	iw8x_plugin_id diagnostic_owner() noexcept;
	iw8x_plugin_id diagnostic_owner(iw8x_plugin_id id) noexcept;
	void slow_sample(const char* operation, std::string_view name, iw8x_plugin_id owner, uint64_t registration, uint64_t elapsed_us,
		uint64_t threshold_us = 10000) noexcept;

	class slow_scope
	{
	public:
		slow_scope(const char* operation, std::string_view name = {}, iw8x_plugin_id owner = 0, uint64_t registration = 0,
			uint64_t threshold_us = 10000) noexcept
			: operation_(operation), name_(name), owner_(owner ? owner : diagnostic_owner()), registration_(registration),
			  threshold_(threshold_us), start_(std::chrono::steady_clock::now())
		{
		}

		~slow_scope()
		{
			slow_sample(operation_, name_, owner_, registration_,
				static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_).count()),
				threshold_);
		}

		void name(std::string_view value) noexcept
		{
			name_ = value;
		}

	private:
		const char* operation_;
		std::string_view name_;
		iw8x_plugin_id owner_;
		uint64_t registration_, threshold_;
		std::chrono::steady_clock::time_point start_;
	};

	// Measures acquisition only, never the protected work. Log queue has a separate
	// nonblocking producer lock, so diagnostics cannot invert registry lock order.
	class runtime_mutex
	{
	public:
		explicit runtime_mutex(const char* name) : name_(name)
		{
		}

		void lock()
		{
			slow_scope timer("lock.wait", name_, 0, 0, 500);
			mutex_.lock();
		}

		void unlock() noexcept
		{
			mutex_.unlock();
		}

		bool try_lock()
		{
			return mutex_.try_lock();
		}

	private:
		const char* name_;
		std::mutex mutex_;
	};
}
