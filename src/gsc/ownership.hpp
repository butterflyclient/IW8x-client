#pragma once
#include "plugin/sdk/abi.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <functional>
namespace iw8x::gsc::internal
{
	struct owner
	{
		iw8x_plugin_id id{};
		std::string name;
		std::mutex mutex;
		std::condition_variable idle;
		bool accepting = true;
		bool unloading = false;
		bool cleanup_failed = false;
		uint32_t active = 0, faults = 0;
	};

	class lease
	{
	public:
		explicit lease(std::shared_ptr<owner> plugin);
		~lease();
		lease(const lease&) = delete;
		lease(lease&&) noexcept = default;

		explicit operator bool() const
		{
			return bool(owner_);
		}

	private:
		std::shared_ptr<owner> owner_;
	};

	using log_sink = void (*)(const char* plugin, const char* message);
	void set_log_sink(log_sink sink);
	void report(const std::shared_ptr<owner>& plugin, const char* message) noexcept;
	void fault(const std::shared_ptr<owner>& plugin, const char* message) noexcept;
	bool guarded(const std::shared_ptr<owner>& plugin, const std::function<void()>& callback) noexcept;
	std::shared_ptr<owner> find_owner(iw8x_plugin_id id);
	iw8x_plugin_id create_owner(std::string name);
	iw8x_status unload_owner(iw8x_plugin_id id, iw8x_plugin_unload before_release = nullptr);
	bool inside_callback() noexcept;
}
