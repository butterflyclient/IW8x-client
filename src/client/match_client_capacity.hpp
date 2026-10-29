#pragma once

#include <cstdint>

namespace client::match_client_capacity
{
	enum class status
	{
		skipped,
		unchanged,
		synchronized,
		unreadable_state,
		invalid_capacity,
		invalid_dvar,
		update_failed,
		existing_allocation_mismatch
	};

	struct result
	{
		status state{ status::skipped };
		int party_capacity{ -1 };
		int ui_before{ -1 };
		int allocated_clients{ -1 };
		int cached_capacity{ -1 };

		bool success() const noexcept
		{
			return state == status::skipped || state == status::unchanged || state == status::synchronized;
		}
	};

	// The allocation wrapper validates these native bindings before installing its hook.
	bool validate(std::uintptr_t base) noexcept;
	result prepare(std::uintptr_t base, unsigned flags, bool authenticated_island) noexcept;
	const char* status_name(status state) noexcept;
}
