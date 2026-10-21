#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace noclip
{
	// Installs the server client-command detour for G_CmdsMP_ClientCommand.
	hook::Status Install(uintptr_t base);
	// Main-thread authority projection; server command applies movement exactly once.
	void Pump();
	bool LocalState(bool* enabled);

	// Toggles noclip for the specified clientNum (default 0 for local player).
	// Returns true on success, and optionally writes the new state to outState.
	bool Toggle(uintptr_t base, int clientNum = 0, bool* outState = nullptr);

	// Sets noclip state explicitly (enable or disable) for the specified clientNum.
	// Returns true on success, and optionally writes previous state to outPrevious.
	bool SetState(uintptr_t base, int clientNum, bool enable, bool* outPrevious = nullptr);

	// Queries the current noclip state for the specified clientNum.
	bool GetState(uintptr_t base, int clientNum, bool* outState);
}
