#pragma once
#include "engine/inline_hook.h"

#include <string>
namespace local_settings
{
	hook::Status Install(uintptr_t base);
	void Pump();
	std::string Status();
	bool Resolve(bool keepLocal);
}
