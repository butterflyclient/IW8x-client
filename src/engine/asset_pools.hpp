#pragma once
#include <cstdint>

namespace asset_pools
{
	// Process attach only, before Replay's DB_InitXAssetPools allocates memory.
	bool prepare(std::uintptr_t module_base, bool port_requested);
	bool port_ready();
}
