#pragma once

#include "engine/inline_hook.h"

namespace island_depth_quickfix
{
	// Replay 1.20 only; compiled with IW8X_ISLAND_PORT_PROBE.
	hook::Status install(std::uintptr_t base);
}
