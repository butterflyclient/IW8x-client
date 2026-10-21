#pragma once
#include "engine/inline_hook.h"
namespace voice_runtime
{
	hook::Status Install(uintptr_t base, bool dedicated);
	void Pump();
}
