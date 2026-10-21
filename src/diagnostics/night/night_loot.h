#pragma once
#include "engine/inline_hook.h"
namespace night_loot
{
	hook::Status Install(uintptr_t base);
	bool UseDefaultEscapeHUDTable();
	bool UseEscapeHUDArt();
}
