#include "diagnostics/island/island_loot.hpp"

#include <cstring>

namespace island_loot
{
		const char* table_name(const char* requested, bool original_source_loot)
	{
			// Island uses genuine Replay 1.20.4 BR loot tables, not the 1.64.5
			// 30-table conversion. A legacy source-loot environment setting must
			// NOT silently switch the lookup to incompatible newer-game loot.
			// Keep the argument for existing callers but deliberately ignore it.
			(void)original_source_loot;
			// Replay 1.20.4's native Donetsk BR root references its own genuine
			// distribution/cache tables; keep all item rows and child table names.
		return requested && std::strcmp(requested, "mp/loot/br/default/loot_item_defs.csv") == 0
			? "mp/loot_item_defs.csv" : requested;
	}
}
