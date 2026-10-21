#include <windows.h>

#include "client/loader/component_loader.hpp"
#include "common/console.h"
#include "engine/image.hpp"
#include "engine/imports.hpp"

#include <atomic>

namespace system_runtime
{
	namespace
	{
		using exit_process_fn = void (WINAPI*)(UINT);
		std::atomic<exit_process_fn> original_exit_process{};
		std::atomic<void (*)()> shutdown_handler{};
		std::atomic<bool> shutting_down{};
		bool installed{};

		[[noreturn]] void WINAPI exit_process(const UINT code)
		{
			const auto original = original_exit_process.load(std::memory_order_acquire);
			// Recursive/concurrent exits must not enter shutdown again: a callback
			// may itself request process termination while workers are draining.
			if (!shutting_down.exchange(true, std::memory_order_acq_rel))
			{
				try
				{
					if (const auto handler = shutdown_handler.load(std::memory_order_acquire)) handler();
					else component_loader::pre_destroy();
				}
				catch (...)
				{
					LOG_ERR("System", "cooperative shutdown failed; forwarding host exit");
				}
			}
			original(code);
			__assume(0);
		}

		hook::Status install_exit_import()
		{
			if (installed) return hook::Status::Installed;
			const auto kernel = GetModuleHandleW(L"kernel32.dll");
			const auto expected = kernel ? GetProcAddress(kernel, "ExitProcess") : nullptr;
			const auto slot = game::imports::find(game::base(), game::image_size(), "KERNEL32.dll", "ExitProcess");
			void* current{};
			if (!expected || !slot || !safemem::ReadBytes(slot, &current, sizeof(current)) ||
				current != reinterpret_cast<void*>(expected))
			{
				LOG_ERR("System", "Replay ExitProcess import does not match the current kernel export");
				return hook::Status::Failed;
			}
			DWORD protection{};
			if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection))
			{
				LOG_ERR("System", "cannot make Replay ExitProcess import writable");
				return hook::Status::Failed;
			}
			original_exit_process.store(reinterpret_cast<exit_process_fn>(expected), std::memory_order_release);
			const auto detour = reinterpret_cast<void*>(&exit_process);
			const auto previous = InterlockedCompareExchangePointer(slot, detour, current);
			DWORD ignored{};
			const auto restored = VirtualProtect(slot, sizeof(*slot), protection, &ignored);
			if (previous != current || !restored)
			{
				// Roll back only our own pointer. Never replace a concurrent owner's
				// IAT update with the export observed before the failed exchange.
				if (previous == current && !restored)
				{
					InterlockedCompareExchangePointer(slot, current, detour);
					VirtualProtect(slot, sizeof(*slot), protection, &ignored);
				}
				LOG_ERR("System", "Replay ExitProcess import exchange/protection restoration failed");
				return hook::Status::Failed;
			}
			installed = true;
			LOG_INFO("System", "normal host exit performs cooperative component shutdown");
			return hook::Status::Installed;
		}
	}

	void set_shutdown_handler(void (*handler)()) noexcept
	{
		shutdown_handler.store(handler, std::memory_order_release);
	}

	class component final : public component_interface
	{
	public:
		hook::Status post_load() override
		{
			return install_exit_import();
		}
		component_priority priority() override
		{
			return component_priority::min;
		}
	};
}

REGISTER_COMPONENT(system_runtime::component)
