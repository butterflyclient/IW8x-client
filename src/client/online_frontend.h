#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
#include <string_view>
namespace online_frontend
{
	void Pump();
	bool local_play(); // actual LAN route, excluding service-owned dedicated handoffs
	void set_local_play(bool enabled); // main-thread menu transition; services keep running
	bool SkipIntroCinematics();
	bool HandleCommand(std::string_view command);
	hook::Status Install(uintptr_t base);
}
