#pragma once
#include "engine/inline_hook.h"
namespace frontend_materials
{
	hook::Status Install(uintptr_t moduleBase);
#ifdef REPLAY_DISABLE_CUSTOM_CONTENT
	inline void ReportPipelineFailure()
	{
	}
#else
	void ReportPipelineFailure();
#endif
}
