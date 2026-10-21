#pragma once
#include <cstdint>

#include "engine/inline_hook.h"
namespace autostart
{
	hook::Status Install(uintptr_t moduleBase);
	void Pump();
	bool Complete();
}
