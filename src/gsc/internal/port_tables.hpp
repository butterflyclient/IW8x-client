#pragma once
#include <cstddef>
#include <cstdint>
namespace iw8x::gsc::internal::port_tables
{
	struct view
	{
		const std::uintptr_t* functions{};
		const std::uintptr_t* methods{};
		std::size_t function_count{}, method_count{};
	};

	// Providers register before plugin discovery and script initialization. The
	// plugin runtime remains the owner of the final server dispatch table.
	inline bool (*inspect)(const void*, view&){};
	inline bool (*reserved)(bool, std::uint32_t){};
	inline std::uintptr_t (*function_at_slot)(std::uint32_t){};

	inline bool is_reserved(bool method, std::uint32_t slot)
	{
		return (!method && function_at_slot && function_at_slot(slot)) || (reserved && reserved(method, slot));
	}
}
