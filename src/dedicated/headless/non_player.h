#pragma once
#include "engine/inline_hook.h"
namespace headless
{
	hook::Status InstallNonPlayer(uintptr_t base);
	bool NonPlayerHost(); // True only after both opt-in native hooks installed.
}
