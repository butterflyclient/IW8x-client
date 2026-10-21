#pragma once
#include "engine/inline_hook.h"

#include "diagnostics/night/night_physics_state.h"
namespace night_physics
{
	hook::Status Install(uintptr_t base);
	// Call only for authenticated converted asset pointers, not stock names.
	bool RegisterShape(uintptr_t shape, SoundRule rule);
	void UnregisterShape(uintptr_t shape);
}
