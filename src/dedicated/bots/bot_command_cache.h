#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
namespace dedicated
{
	inline int BotDistanceInterval(const float* position, const std::array<float, 3>* humans, unsigned count)
	{
		if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) return 100;
		int interval = 1000;
		for (unsigned i = 0; i < count; ++i)
		{
			const float x = position[0] - humans[i][0], y = position[1] - humans[i][1], z = position[2] - humans[i][2];
			const float distance = x * x + y * y + z * z;
			if (!std::isfinite(distance) || distance <= 2500.0f * 2500.0f) return 100;
			if (distance <= 6000.0f * 6000.0f) interval = 250;
		}
		return interval;
	}

	// Replay usercmd is264 bytes. The decision producer stamps all three times;
	// movement consumes a command every frame, even between AI decisions.
	struct BotCommandCache
	{
		std::array<std::uint8_t, 264> command{};
		std::uintptr_t client = 0;
		unsigned generation = 0;
		int lastTime = -1, interval = 0;
		std::int64_t nextTime = 0;

		void Invalidate()
		{
			client = 0;
		}

		bool Reuse(std::uintptr_t identity, unsigned epoch, int now, int period, void* out) const
		{
			if (!identity || identity != client || epoch != generation || period <= 0 || period != interval || now < lastTime ||
				now >= nextTime)
				return false;
			std::memcpy(out, command.data(), command.size());
			for (unsigned offset : { 12u, 16u, 20u })
				std::memcpy(static_cast<std::uint8_t*>(out) + offset, &now, sizeof(now));
			return true;
		}

		void Save(std::uintptr_t identity, unsigned epoch, unsigned slot, int now, int period, const void* in)
		{
			std::memcpy(command.data(), in, command.size());
			client = identity;
			generation = epoch;
			lastTime = now;
			interval = period;
			// Phase by slot so hundreds of bots do not all think on the same tick.
			nextTime = period > 0 ? std::int64_t(now) + period - ((std::int64_t(now) + slot * 17) % period) : now;
		}
	};
}
