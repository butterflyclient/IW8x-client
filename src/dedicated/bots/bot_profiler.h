#pragma once
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
namespace dedicated
{
	// Bounded, explicitly enabled CPU timing. Buckets overlap (navigation is nested
	// inside AI/script work); totals must not be added together as a frame total.
	class BotProfiler
	{
	public:
		enum Kind
		{
			Decisions,
			Batch,
			Movement,
			ScriptGoal,
			ReachableSearch,
			Projection,
			Archive,
			Count
		};

		struct Sample
		{
			std::uint64_t calls, ticks, peak, slow;
		};

		struct Ticket
		{
			unsigned generation = 0;
			std::int64_t start = 0;
		};

		BotProfiler()
		{
			LARGE_INTEGER value{};
			QueryPerformanceFrequency(&value);
			frequency_ = value.QuadPart;
		}

		void Start(unsigned seconds)
		{
			deadline_.store(0);
			++generation_;
			for (auto& s : stats_)
			{
				s.calls = 0;
				s.ticks = 0;
				s.peak = 0;
				s.slow = 0;
			}
			started_ = GetTickCount64();
			deadline_.store(started_ + seconds * 1000ull);
		}

		Ticket Begin() const
		{
			const auto until = deadline_.load(std::memory_order_relaxed);
			if (!until || GetTickCount64() >= until) return {};
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			return { generation_.load(std::memory_order_relaxed), value.QuadPart };
		}

		void End(Kind kind, Ticket ticket)
		{
			if (!ticket.start || ticket.generation != generation_.load(std::memory_order_relaxed) ||
				!deadline_.load(std::memory_order_relaxed))
				return;
			LARGE_INTEGER value{};
			QueryPerformanceCounter(&value);
			const auto ticks = static_cast<std::uint64_t>(value.QuadPart - ticket.start);
			auto& s = stats_[kind];
			s.calls.fetch_add(1, std::memory_order_relaxed);
			s.ticks.fetch_add(ticks, std::memory_order_relaxed);
			if (ticks * 1000 >= static_cast<std::uint64_t>(frequency_) * 16) s.slow.fetch_add(1, std::memory_order_relaxed);
			auto peak = s.peak.load(std::memory_order_relaxed);
			while (ticks > peak && !s.peak.compare_exchange_weak(peak, ticks, std::memory_order_relaxed))
			{
			}
		}

		bool Due() const
		{
			const auto until = deadline_.load(std::memory_order_relaxed);
			return until && GetTickCount64() >= until;
		}

		bool Stop()
		{
			return deadline_.exchange(0) != 0;
		}

		double Milliseconds(std::uint64_t ticks) const
		{
			return ticks * 1000.0 / frequency_;
		}

		unsigned ElapsedMs() const
		{
			return static_cast<unsigned>(GetTickCount64() - started_);
		}

		Sample Read(Kind kind) const
		{
			const auto& s = stats_[kind];
			return { s.calls.load(), s.ticks.load(), s.peak.load(), s.slow.load() };
		}

	private:
		struct Stats
		{
			std::atomic<std::uint64_t> calls{ 0 }, ticks{ 0 }, peak{ 0 }, slow{ 0 };
		};

		std::array<Stats, Count> stats_{};
		std::atomic<unsigned> generation_{ 0 };
		std::atomic<ULONGLONG> deadline_{ 0 };
		ULONGLONG started_ = 0;
		std::int64_t frequency_ = 1;
	};
}
