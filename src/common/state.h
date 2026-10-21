#pragma once
#include <atomic>
#include <cstdint>
namespace state
{
	enum class HookState
	{
		Idle,
		WaitingForSignal,
		SignalSeen,
		Installed,
		Failed,
		PrologueMismatch
	};

	inline std::atomic<int> hookState{ static_cast<int>(HookState::Idle) };
	inline std::atomic<uint64_t> dvarsSeen{ 0 };
	inline std::atomic<uint64_t> boolDvarsSeen{ 0 };
	inline std::atomic<uintptr_t> moduleBase{ 0 };
	inline std::atomic<uint32_t> imageSize{ 0 };
}
