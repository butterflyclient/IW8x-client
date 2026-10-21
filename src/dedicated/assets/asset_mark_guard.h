#pragma once
#include "engine/inline_hook.h"
namespace asset_mark_guard
{
	hook::Status Install(uintptr_t base);
	void Report();
}
