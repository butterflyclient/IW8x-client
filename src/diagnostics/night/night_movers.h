#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace night_movers
{
	hook::Status Install(std::uintptr_t base);
	void ResetServerContext(void* context);
	void MarkKeyframed(void* context, std::uint64_t reference);
	void SetOptimized(void* context, std::uint64_t reference);
	void SetAntilagged(void* context, std::uint64_t reference);
}
