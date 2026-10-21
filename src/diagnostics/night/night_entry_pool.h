#pragma once
#include <cstdint>
namespace night_entry_pool
{
	// Exact Replay, process attach only, before its CRT builds the free list.
	// Called for an explicitly selected converted-map port, including a
	// dedicated process with -bap. The relocation is independently validated.
	bool Prepare(uintptr_t imageBase);
	bool Report();
}
