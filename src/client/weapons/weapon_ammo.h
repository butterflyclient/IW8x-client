#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace weapon_ammo
{
	// Admitted collection packages may use the later negative = inherit values.
	// The six original getter loops retain their ordering and positive behavior.
	hook::Status Install(uintptr_t base);
}
