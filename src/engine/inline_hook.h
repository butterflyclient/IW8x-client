#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>
namespace hook
{
	enum class Status
	{
		Installed,
		NotReady,
		Failed
	};

	namespace detail
	{
		std::mutex& Mutex();
		Status Prepare(void* target, void* detour, const uint8_t* expected, size_t expectedSize, void** trampoline);
		bool Enable(void* target);
		// Caller holds Mutex() and publishes every original before enabling. Retain
		// prepared trampolines on failure: callbacks can still be returning through them.
		bool EnableBatch(std::span<void* const> targets);
	}

	class Batch
	{
		struct Entry
		{
			void* target;
			void* detour;
			const uint8_t* expected;
			size_t expectedSize;
			void* slot;
			bool (*occupied)(void*);
			void (*publish)(void*, void*);
			void* trampoline = nullptr;
		};

		std::vector<Entry> entries_;
		bool attempted_ = false;

	public:
		template <class Fn>
		void Add(void* target, Fn detour, const uint8_t* expected, size_t size, std::atomic<Fn>& original)
		{
			entries_.push_back({ target, reinterpret_cast<void*>(detour), expected, size, &original,
				[](void* slot)
				{
					return static_cast<std::atomic<Fn>*>(slot)->load(std::memory_order_acquire) != nullptr;
				},
				[](void* slot, void* value)
				{
					static_cast<std::atomic<Fn>*>(slot)->store(reinterpret_cast<Fn>(value), std::memory_order_release);
				} });
		}

		Status Commit();
	};

	// Verify and prepare, publish the original, THEN enable the detour.
	// MinHook relocates instructions and adjusts suspended instruction pointers.
	// Live trampolines remain allocated until process exit.
	template <typename Fn>
	Status Install(void* target, Fn detour, const uint8_t* expected, size_t expectedSize, std::atomic<Fn>& original)
	{
		std::lock_guard<std::mutex> lock(detail::Mutex());
		if (original.load(std::memory_order_acquire)) return Status::Installed;
		void* trampoline = nullptr;
		const Status status = detail::Prepare(target, reinterpret_cast<void*>(detour), expected, expectedSize, &trampoline);
		if (status != Status::Installed) return status;
		original.store(reinterpret_cast<Fn>(trampoline), std::memory_order_release);
		if (detail::Enable(target)) return Status::Installed;
		original.store(nullptr, std::memory_order_release);
		return Status::Failed;
	}
}
