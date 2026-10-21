#pragma once
#include <windows.h>

#include <cstdint>
#include <string_view>

#include "engine/inline_hook.h"

#include "auth/offline_identity.h"
namespace offlineauth
{
	// Disk-only preparation on the worker, before publishing startup readiness.
	bool Prepare(HMODULE self);
	bool HandleCommand(std::string_view command);
	Identity GetIdentity();

	hook::Status Install(uintptr_t base);
	void LogSnapshot();
	// Main-thread client profile/stats initialization, after MP assets/LUI exist.
	bool InitializeProfile();
	bool IsReady(int controller = 0);
}
