#pragma once
#include "engine/inline_hook.h"
namespace dedicated_window
{
	// Keep Replay's native HWNDs alive for engine assumptions, but make every
	// dedicated-mode game window permanently non-visible. The DLL console is exempt.
	hook::Status Install();
	void MaintainHidden();
}
