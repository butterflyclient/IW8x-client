#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace night_script_range
{
	// Startup-only opt-in for the 1.28 map package; never switched on a live VM.
	hook::Status Install(uintptr_t moduleBase);
}
