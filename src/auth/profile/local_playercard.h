#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace local_playercard
{
	void Prepare(uint64_t xuid);
	hook::Status Install(uintptr_t base);
	// Client main thread, after local_stats restored all six native DDL groups.
	void Pump();
	void OnNameChanged(); // main thread: repack the local card with the new name
	void MarkDirty(); // deferred repack after an authenticated profile restore
}
