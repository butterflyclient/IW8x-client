#pragma once

#include <cstring>

namespace island_operator_table
{
	// The shared native operators.csv is required by MP lobby LUI *before*
	// any Island map ScriptFiles are resident. Do not substitute the private
	// Island-only 39-column view into client/frontend global DB lookups.
	// Original Island dedicated GSC can read the verified private 37x39
	// StringTable from magma_island_scripts.ff after it loads.
	inline const char* resolve(const char* name, bool dedicated_island_br) noexcept
	{
		if (dedicated_island_br && name && std::strcmp(name, "operators.csv") == 0)
			return "magma/island/operators.csv";
		return name;
	}
}
