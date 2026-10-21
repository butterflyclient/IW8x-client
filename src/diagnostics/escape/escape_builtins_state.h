#pragma once
#include <cstdint>
namespace escape_builtins
{
	// Exact original 1.30 1DFEFB0: weapon-object class 10, 16-bit pool index,
	// alternate-mode byte +3C. Delegate the actual query to Replay's consumer.
	template <class Service>
	void IsSilenced(Service& service, std::uint64_t reference)
	{
		if (((reference >> 32) & 0xff) != 10)
		{
			service.ScriptError(0x1447);
			return;
		}
		const auto number = static_cast<std::uint32_t>(reference);
		const auto weapon = number ? service.WeaponPool() + static_cast<std::uint16_t>(number - 1) * 62ull : service.DefaultWeapon();
		service.AddBool(service.Silenced(weapon, service.Alternate(weapon)));
	}

	// Exact original 1.30 244AC60. Source +354 and native +344 are independently
	// identified by their original breath/exhaustion writers, not offset guessing.
	template <class Service>
	void IsBreathTimerActive(Service& service, std::uint64_t reference)
	{
		if ((reference >> 32) & 0xff)
		{
			service.ObjectError(0xe62);
			return;
		}
		const auto state = service.PlayerOrAgent(static_cast<std::uint32_t>(reference));
		if (!state)
		{
			service.ObjectError(0xe5f);
			return;
		}
		service.AddBool(service.BreathTimerActive(state));
	}
}
