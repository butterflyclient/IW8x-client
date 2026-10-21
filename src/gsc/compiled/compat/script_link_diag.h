#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace script_link_diag
{
	// Optional, read-only native error probe. Enabled only for the 1.24 frontend.
	hook::Status Install(uintptr_t gameBase);
}
