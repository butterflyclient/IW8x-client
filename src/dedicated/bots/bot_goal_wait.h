#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace dedicated
{
	// Replay bot_br_think (canonical 0x2039): only the final roaming-loop wait.
	// Native GetFloat(0.05), wait, jumpback, End at function+0x394. Earlier
	// infiltration/gulag waits must remain untouched. See RESEARCH.md.
	inline constexpr unsigned kBotGoalWaitOffset = 0x394;
	using BotGoalWaitBytes = std::array<std::uint8_t, 10>;

	inline bool PrepareBotGoalWait(BotGoalWaitBytes& bytes, int previousMs, int requestedMs)
	{
		if (requestedMs < 50 || requestedMs > 5000 || previousMs < 50 || previousMs > 5000) return false;
		BotGoalWaitBytes expected{ 0x70, 0, 0, 0, 0, 0x5d, 0x1b, 0x54, 0x03, 0x3b };
		const float previous = previousMs / 1000.0f, next = requestedMs / 1000.0f;
		std::memcpy(expected.data() + 1, &previous, sizeof(previous));
		if (bytes != expected) return false;
		std::memcpy(bytes.data() + 1, &next, sizeof(next));
		return true;
	}
}
