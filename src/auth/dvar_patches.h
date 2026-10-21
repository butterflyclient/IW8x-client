#pragma once
#include <cstdint>

#include "engine/inline_hook.h"
namespace dvars
{
	void SetUnlockAll(bool enabled); // main-thread console action
	// Island BR only, opt-in and exact external-string permission restoration.
	// Call on the main thread after playlist config and before stock DevmapCmd;
	// does not create dvars or change values and refuses unknown native types.
	unsigned RestoreIslandBRScriptPermissions();
	hook::Status Install(uintptr_t moduleBase);
}
