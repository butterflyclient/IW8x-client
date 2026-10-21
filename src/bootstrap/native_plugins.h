#pragma once
#include "engine/inline_hook.h"
namespace native_plugins
{
	// Called by the existing splash installer after dedicated/BAP routing is ready.
	[[nodiscard]] hook::Status Install();
	// The pinned initialization thread owns async work and diagnostic I/O. No
	// engine/GSC operations run here; completion callbacks await the server frame.
	void PumpWorker() noexcept;
}
