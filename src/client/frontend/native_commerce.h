#pragma once
#include "engine/inline_hook.h"
namespace native_commerce
{
	hook::Status Install(uintptr_t base);
	void Register(void* state);
	void Pump();
}
