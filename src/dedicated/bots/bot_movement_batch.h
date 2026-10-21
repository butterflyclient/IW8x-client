#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace dedicated
{
	// Capture only calls selected by Replay's original bot movement loop. Own the
	// command bytes until the native queues copy them; never retain its stack data.
	class BotMovementBatch
	{
	public:
		struct Entry
		{
			std::uintptr_t client = 0;
			unsigned slot = 0;
			alignas(16) std::array<std::uint8_t, 264> command{};
		};

		void Reset()
		{
			count_ = 0;
			seen_.fill(false);
		}

		bool Push(std::uintptr_t client, unsigned slot, const void* command)
		{
			if (!client || !command || slot >= entries_.size() || seen_[slot]) return false;
			auto& entry = entries_[count_++];
			entry.client = client;
			entry.slot = slot;
			std::memcpy(entry.command.data(), command, entry.command.size());
			seen_[slot] = true;
			return true;
		}

		template <class Queue, class Serial, class Drain>
		unsigned Execute(Queue queue, Serial serial, Drain drain)
		{
			unsigned queued = 0;
			bool outstanding = false;
			for (unsigned i = 0; i < count_; ++i)
			{
				const auto& entry = entries_[i];
				if (queue(entry))
				{
					++queued;
					outstanding = true;
				}
				else
				{
					// A native refusal must not run serial movement alongside our
					// outstanding work. The normal frame still owns deferred tasks.
					if (outstanding)
					{
						drain();
						outstanding = false;
					}
					serial(entry);
				}
			}
			if (outstanding) drain();
			Reset();
			return queued;
		}

	private:
		std::array<Entry, 200> entries_{};
		std::array<bool, 200> seen_{};
		unsigned count_ = 0;
	};
}
