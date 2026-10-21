#pragma once
#include <cstdint>
namespace night_builtins
{
	// Original 1.28 205EE30 and 42527A0. Services expose each engine's layout;
	// the control flow is checked against executions of the original callbacks.
	template <class Services>
	void StopPlayerMusic(Services& s, std::uint64_t reference)
	{
		if ((reference >> 32) & 255)
		{
			s.ObjectError(0xE5E);
			return;
		}
		int number{};
		if (!s.PlayerOrAgent(reference, number))
		{
			s.ObjectError(0xE5D);
			return;
		}
		if (s.AgentMode() && !s.AgentClient(number))
		{
			s.ObjectError(0x1967);
			return;
		}
		if (number >= s.ClientCount())
		{
			s.ScriptError(0x1968);
			return;
		}
		if (s.NumParams() != 1)
		{
			s.ScriptError(0x1969);
			return;
		}
		s.SendStop(number, s.ArgumentHash());
	}

	template <class Services>
	void StopRequestedMusic(Services& s, std::uint32_t hash)
	{
		s.Lock();
		const auto state = s.Find(hash);
		if (state && state == s.Requested())
		{
			s.Clear();
			s.Update();
		}
		s.Unlock();
	}
}
