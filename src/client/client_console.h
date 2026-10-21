#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
#include <string_view>
namespace clientconsole
{
	hook::Status Install(uintptr_t moduleBase);
	void pump_commands();
	// Must run on Replay's main thread. A true result means dispatched, not connected.
	bool JoinSession(std::string_view hex, bool invite, uint64_t invitedXuid = 0);
	bool RequestNoclip(bool enabled);
}
