#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace headless
{
	hook::Status InstallDeviceHooks(uintptr_t moduleBase);
	hook::Status Install(uintptr_t moduleBase);
}
