#pragma once
#include <atomic>
#include <cstdint>
namespace matchmaking
{
	// One coherent worker-to-UI sample. This measures a backend HTTP exchange,
	// not the game's UDP ping or a retail matchmaking ping cutoff.
	class BackendLatency
	{
		std::atomic<uint64_t> sample{ 0 };

	public:
		void Reset()
		{
			sample.store(0, std::memory_order_relaxed);
		}

		void Report(unsigned status, uint64_t elapsedMs, uint64_t now)
		{
			if (status != 200 || elapsedMs > 60000 || !now || now >= (uint64_t(1) << 48))
			{
				Reset();
				return;
			}
			sample.store((now << 16) | (elapsedMs + 1), std::memory_order_relaxed);
		}

		int Read(uint64_t now) const
		{
			const auto value = sample.load(std::memory_order_relaxed);
			const auto at = value >> 16;
			if (!value || now < at || now - at > 5000) return -1;
			return int(value & 0xffff) - 1;
		}
	};
}
