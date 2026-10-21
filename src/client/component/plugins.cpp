#include "client/loader/component_loader.hpp"
#include "scheduler.hpp"
#include "bootstrap/native_plugins.h"
#include "gsc/api.hpp"
#include "plugin/loader.hpp"

#include <mutex>

namespace plugins_component
{
	namespace
	{
		std::mutex handle_mutex;
		scheduler::task_handle worker;
		void wake_worker() noexcept
		{
			std::lock_guard lock(handle_mutex);
			worker.wake();
		}
	}

	class component final : public component_interface
	{
	public:
		hook::Status post_unpack() override
		{
			const auto result = native_plugins::Install();
			if (result != hook::Status::Installed) return result;
			{
				std::lock_guard lock(handle_mutex);
				worker = scheduler::schedule([]
				{
					native_plugins::PumpWorker();
					return std::optional{ iw8x::gsc::worker_delay() };
				});
				if (!worker) return hook::Status::Failed;
			}
			iw8x::gsc::set_worker_wakeup(&wake_worker);
			wake_worker();
			return hook::Status::Installed;
		}

		void pre_destroy() override
		{
			iw8x::gsc::set_worker_wakeup(nullptr);
			scheduler::task_handle task;
			{
				std::lock_guard lock(handle_mutex);
				task = std::move(worker);
				worker = {};
			}
			task.cancel(true);
			iw8x::plugins::unload_all();
		}
		component_priority priority() override { return component_priority::plugins; }
	};
}
REGISTER_COMPONENT(plugins_component::component)
