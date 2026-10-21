#pragma once
#include "engine/inline_hook.h"

#include <string_view>
namespace weapon_capacity
{
	hook::Status Install(uintptr_t base, std::string_view specification);
}
