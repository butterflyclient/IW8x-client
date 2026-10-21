#pragma once

#include <atomic>

namespace dedicated::detail
{
	// The interactive console runs on its own input thread. The actual GSC
	// builtin must execute later from the verified server VM/frame boundary.
	class MapRestartRequest
	{
		std::atomic<bool> ready_{ false };
		std::atomic<bool> pending_{ false };

	public:
		enum class Result { Queued, AlreadyQueued, NotReady };

		Result Queue() noexcept
		{
			if (!ready_.load(std::memory_order_acquire)) return Result::NotReady;
			bool expected = false;
			return pending_.compare_exchange_strong(expected, true, std::memory_order_acq_rel) ? Result::Queued : Result::AlreadyQueued;
		}

		bool Pending() const noexcept { return pending_.load(std::memory_order_acquire); }
		void Finish() noexcept { pending_.store(false, std::memory_order_release); }

		void SetReady(bool ready) noexcept
		{
			ready_.store(ready, std::memory_order_release);
			if (!ready) Finish(); // A request cannot survive server/map teardown.
		}
	};
}
