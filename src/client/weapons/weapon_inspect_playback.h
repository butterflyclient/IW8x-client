#pragma once
#include "engine/inline_hook.h"

#include <string_view>
namespace weapon_inspect_playback
{
	// Consumer component only. Do not install in a game until the controller,
	// input/cancel adapters and every admitted weapon's animation graph are ready.
	// Currently invoked only by the native component test executable.
	hook::Status Install(uintptr_t base, std::string_view specification);
}
