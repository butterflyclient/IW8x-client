#pragma once
#include "engine/inline_hook.h"
namespace dedicated
{
	void ObserveScriptEndLoad(void* context);
	// True only when the process command line contains the exact --dedi flag.
#ifdef IW8X_CLIENT_ONLY
	inline constexpr bool Enabled() { return false; }
#else
	bool Enabled();
#endif
	// Called synchronously at the splash boundary, before native UI initialization.
	hook::Status Install(uintptr_t moduleBase);
	// Called only after authentication, dvar overrides and the retained guards install.
	void ArmStartup();
	bool StartRequested();
	bool AutoPlaylist(); // immutable --auto-playlist launch mode
}
