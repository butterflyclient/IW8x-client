#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace frontend_animtree128
{
	hook::Status Install(uintptr_t moduleBase);
#ifdef REPLAY_DISABLE_CUSTOM_CONTENT
	inline bool Active() noexcept
	{
		return false;
	}
#else
	bool Active() noexcept;
#endif
}
