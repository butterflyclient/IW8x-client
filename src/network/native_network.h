#pragma once
#include "engine/inline_hook.h"

#include <string_view>
struct in_addr;

namespace native_network
{
	// Resolve once when selecting an endpoint, never in the packet/game-frame loop.
	bool ResolveIPv4(std::string_view host, in_addr& address);
	bool ConfigurationPath(wchar_t* path, size_t capacity);
	hook::Status Install(uintptr_t base);
	bool AutoServerEnabled();
	bool PublicationReady();
	void PrintStatus();
}
