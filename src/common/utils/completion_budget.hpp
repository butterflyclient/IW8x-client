#pragma once
#include <chrono>
#include <cstddef>

namespace utils
{
	class completion_budget
	{
		using clock = std::chrono::steady_clock;
		clock::time_point start_ = clock::now();
		size_t count_ = 0;

	public:
		// Never preempt a native callback. Yield the remaining queue after it.
		bool next()
		{
			if (count_ >= 16 || (count_ && clock::now() - start_ >= std::chrono::milliseconds(1))) return false;
			++count_;
			return true;
		}
	};
}
