#include "inline_hook.h"

#include "../common/console.h"

#include "safemem.h"

#include "thirdparty/minhook/include/MinHook.h"

#include <cstring>
namespace hook::detail
{
	std::mutex& Mutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	Status Prepare(void* target, void* detour, const uint8_t* expected, size_t expectedSize, void** trampoline)
	{
		uint8_t actual[64]{};
		if (!target || !detour || !expected || expectedSize < 5 || expectedSize > sizeof(actual)) return Status::Failed;
		if (!safemem::ReadBytes(target, actual, expectedSize) || std::memcmp(actual, expected, expectedSize) != 0) return Status::NotReady;
		const MH_STATUS init = MH_Initialize();
		if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
		{
			LOG_ERR("Hook", "MinHook initialization failed: %s", MH_StatusToString(init));
			return Status::Failed;
		}
		const MH_STATUS status = MH_CreateHook(target, detour, trampoline);
		if (status != MH_OK)
		{
			LOG_ERR("Hook", "create @%p failed: %s", target, MH_StatusToString(status));
			return Status::Failed;
		}
		return Status::Installed;
	}

	bool Enable(void* target)
	{
		const MH_STATUS status = MH_EnableHook(target);
		if (status == MH_OK) return true;
		LOG_ERR("Hook", "enable @%p failed: %s", target, MH_StatusToString(status));
		MH_RemoveHook(target);
		return false;
	}

	bool EnableBatch(std::span<void* const> targets)
	{
		bool queued = true;
		for (const auto target : targets)
		{
			const auto status = MH_QueueEnableHook(target);
			if (status != MH_OK)
			{
				queued = false;
				break;
			}
		}
		if (queued && MH_ApplyQueued() == MH_OK) return true;
		// Cancel all pending enables and undo any partial apply. Originals stay
		// allocated for in-flight calls and an idempotent installation retry.
		for (const auto target : targets)
			(void)MH_QueueDisableHook(target);
		if (MH_ApplyQueued() != MH_OK)
		{
			for (const auto target : targets)
				(void)MH_DisableHook(target);
		}
		LOG_ERR("Hook", "batch enable failed; prepared originals retained, observers disarmed");
		return false;
	}
}

namespace hook
{
	Status Batch::Commit()
	{
		std::lock_guard lock(detail::Mutex());
		if (attempted_ || entries_.empty()) return Status::Failed;
		attempted_ = true;
		// Reject duplicate targets/original slots before creating any hook. A
		// non-null original may belong to a live or failed batch; never adopt it.
		for (size_t i = 0; i < entries_.size(); ++i)
		{
			if (entries_[i].occupied(entries_[i].slot)) return Status::Failed;
			for (size_t j = 0; j < i; ++j)
				if (entries_[i].target == entries_[j].target || entries_[i].slot == entries_[j].slot) return Status::Failed;
		}
		const auto discardPrepared = [&]
		{
			for (auto& e : entries_)
				if (e.trampoline)
				{
					// Never enabled/published, so no callback can hold this trampoline.
					const auto result = MH_RemoveHook(e.target);
					if (result != MH_OK) LOG_ERR("Hook", "prepared batch cleanup @%p failed: %s", e.target, MH_StatusToString(result));
					e.trampoline = nullptr;
				}
		};
		for (auto& e : entries_)
		{
			const auto status = detail::Prepare(e.target, e.detour, e.expected, e.expectedSize, &e.trampoline);
			if (status != Status::Installed)
			{
				discardPrepared();
				return status;
			}
		}
		for (auto& e : entries_)
		{
			if (MH_QueueEnableHook(e.target) != MH_OK)
			{
				discardPrepared();
				return Status::Failed;
			}
		}
		for (auto& e : entries_)
			e.publish(e.slot, e.trampoline);
		const auto result = MH_ApplyQueued();
		if (result == MH_OK) return Status::Installed;
		LOG_ERR("Hook", "batch activation failed: %s; restoring original entries", MH_StatusToString(result));
		// ApplyQueued can partially succeed. Disable every owned entry and cancel
		// pending enables, without affecting hooks belonging to other components.
		// Retain published trampolines and originals until process exit: a thread
		// may already be inside a detour and have not called its original yet.
		for (auto& e : entries_)
		{
			const auto queued = MH_QueueDisableHook(e.target);
			const auto disabled = MH_DisableHook(e.target);
			if (queued != MH_OK || (disabled != MH_OK && disabled != MH_ERROR_DISABLED))
			{
				LOG_ERR("Hook", "batch rollback failed @%p; cannot continue a partially active subsystem", e.target);
				TerminateProcess(GetCurrentProcess(), ERROR_DLL_INIT_FAILED);
			}
		}
		return Status::Failed;
	}
}
