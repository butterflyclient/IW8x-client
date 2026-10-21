#pragma once
#include "auth/profile/challenge_events.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <list>
#include <mutex>
namespace server_progression
{
	// Single transaction consumer, concurrent gameplay producers. No account work,
	// allocation or disk IO may run while holding this queue's mutex.
	class EventQueue
	{
	public:
		using Batch = std::list<challenges::Event>;
		enum class Result
		{
			Accepted,
			Stopped,
			Full
		};

		struct Status
		{
			size_t pending = 0, highWater = 0;
			uint64_t accepted = 0, completed = 0, retries = 0, rejected = 0;
		};

		explicit EventQueue(size_t capacity = 8192) : capacity_(capacity)
		{
		}

		void Start()
		{
			std::lock_guard lock(mutex_);
			stopping_ = false;
		}

		void Stop()
		{
			{
				std::lock_guard lock(mutex_);
				stopping_ = true;
			}
			wake_.notify_all();
		}

		Result Push(const challenges::Event& event)
		{
			// Allocate/copy before taking the short queue lock. Splice never copies
			// event strings or allocates nodes, including when the worker retries.
			Batch incoming{ event };
			{
				std::lock_guard lock(mutex_);
				if (stopping_) return Result::Stopped;
				if (status_.pending >= capacity_)
				{
					++status_.rejected;
					return Result::Full;
				}
				events_.splice(events_.end(), incoming);
				++status_.accepted;
				status_.highWater = std::max(status_.highWater, ++status_.pending);
			}
			wake_.notify_one();
			return Result::Accepted;
		}

		bool Take(Batch& batch)
		{
			assert(batch.empty());
			std::unique_lock lock(mutex_);
			wake_.wait(lock,
				[&]
				{
					return stopping_ || !events_.empty();
				});
			if (events_.empty()) return false;
			const auto id = events_.front().xuid;
			auto end = events_.begin();
			size_t count = 0;
			while (end != events_.end() && end->xuid == id && count < 128)
			{
				++end;
				++count;
			}
			batch.splice(batch.end(), events_, events_.begin(), end);
			// In-flight events still consume capacity until durably completed.
			return true;
		}

		void Complete(Batch& batch)
		{
			{
				std::lock_guard lock(mutex_);
				assert(status_.pending >= batch.size());
				status_.pending -= batch.size();
				status_.completed += batch.size();
			}
			batch.clear();
		}

		bool Retry(Batch& batch, std::chrono::milliseconds delay = std::chrono::seconds(1))
		{
			std::unique_lock lock(mutex_);
			events_.splice(events_.begin(), batch);
			++status_.retries;
			// Failed awards stay ahead of newer reports, including on shutdown.
			// Waiting releases the queue mutex so gameplay can still submit.
			wake_.wait_for(lock, delay,
				[&]
				{
					return stopping_;
				});
			return !stopping_;
		}

		Status GetStatus() const
		{
			std::lock_guard lock(mutex_);
			return status_;
		}

	private:
		const size_t capacity_;
		mutable std::mutex mutex_;
		std::condition_variable wake_;
		bool stopping_ = true;
		Batch events_;
		Status status_;
	};
}
