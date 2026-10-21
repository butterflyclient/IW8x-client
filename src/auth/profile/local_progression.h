#pragma once
#include "engine/inline_hook.h"
namespace local_progression
{
	struct View
	{
		bool ready = false;
		int xp = 0, rank = 0, seasonXP = 0, seasonRank = 0;
	};

	hook::Status Install(uintptr_t base, bool dedicated);
	bool PrepareServer(); // main thread, stable loaded map; required before publishing readiness
	void Pump(); // main thread, after the six online DDL groups have been restored
	View Get(int season);

	struct WeaponView
	{
		bool ready = false;
		int xp = 0, rank = 0;
	};

	WeaponView GetWeapon(const char* weapon);
	bool OwnsItem(int item);
}
