#include "client/match_client_capacity.hpp"

#include "engine/safemem.h"

#include <cstring>

namespace client::match_client_capacity
{
	namespace
	{
		constexpr std::uintptr_t game_party_rva = 0xC73ADE0;
		constexpr std::uintptr_t party_slots_rva = 0x118C7D0;
		constexpr std::uintptr_t set_int_rva = 0x13E97B0;
		constexpr std::uintptr_t ui_maxclients_rva = 0xD386878;
		constexpr std::uintptr_t allocated_clients_rva = 0xEEB0680;
		constexpr std::uintptr_t cached_capacity_rva = 0xEEB0CE0;
		constexpr std::uintptr_t host_index_offset = 311000;
		constexpr std::uintptr_t slots_offset = 0x4BF34;
		constexpr std::uintptr_t host_offset = 0x4BF48;
		constexpr std::uintptr_t in_party_offset = 0x4BF4C;
		constexpr std::uintptr_t dvar_type_offset = 29;
		constexpr std::uintptr_t dvar_current_offset = 40;

		constexpr std::uint8_t party_slots_bytes[]{ 0x8B, 0x81, 0x34, 0xBF, 0x04, 0x00, 0xC3 };
		constexpr std::uint8_t set_int_bytes[]{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B,
			0x05, 0x3B, 0xAC, 0x66, 0x04, 0x48, 0x33, 0xC4 };
		// GameAllocate reads ui_maxclients before its existing-allocation test.
		constexpr std::uint8_t allocation_read_bytes[]{ 0x48, 0x8B, 0x05, 0x62, 0xDB, 0xD9, 0x0B,
			0x44, 0x8B, 0x78, 0x28, 0x83, 0x3D, 0x5F, 0x79, 0x8C, 0x0D, 0x00, 0x0F, 0x85, 0xE8, 0x01, 0x00, 0x00 };
		// Its completion publishes the local-client allocation count and cached capacity.
		constexpr std::uint8_t allocation_publish_bytes[]{ 0x44, 0x89, 0x35, 0x94, 0x77, 0x8C, 0x0D,
			0x44, 0x89, 0x3D, 0xED, 0x7D, 0x8C, 0x0D, 0x44, 0x89, 0x2D, 0xEA, 0x7D, 0x8C, 0x0D };

		bool valid_base(std::uintptr_t base) noexcept
		{
			return base && base <= UINTPTR_MAX - cached_capacity_rva - sizeof(int);
		}

		template <typename T>
		bool read(std::uintptr_t address, T& value) noexcept
		{
			return safemem::ReadBytes(reinterpret_cast<const void*>(address), &value, sizeof(value));
		}

		template <std::size_t size>
		bool matches(std::uintptr_t base, std::uintptr_t rva, const std::uint8_t (&expected)[size]) noexcept
		{
			std::uint8_t actual[size]{};
			return safemem::ReadBytes(reinterpret_cast<const void*>(base + rva), actual, size) &&
				std::memcmp(actual, expected, size) == 0;
		}
	}

	bool validate(std::uintptr_t base) noexcept
	{
		return valid_base(base) && matches(base, party_slots_rva, party_slots_bytes) &&
			matches(base, set_int_rva, set_int_bytes) &&
			matches(base, 0x15E8D0F, allocation_read_bytes) &&
			matches(base, 0x15E8EE5, allocation_publish_bytes);
	}

	result prepare(std::uintptr_t base, unsigned flags, bool authenticated_island) noexcept
	{
		result prepared;
		// Bit 1 allocates the frontend's single-client world independently of party size.
		if (!authenticated_island || (flags & 2)) return prepared;
		prepared.state = status::unreadable_state;
		if (!valid_base(base)) return prepared;

		const auto party = base + game_party_rva;
		int in_party{}, host{}, host_index{};
		if (!read(party + in_party_offset, in_party) || !read(party + host_offset, host) ||
			!read(party + host_index_offset, host_index))
			return prepared;
		if (!in_party || host || host_index != -2)
		{
			prepared.state = status::skipped;
			return prepared;
		}

		if (!read(party + slots_offset, prepared.party_capacity) ||
			!read(base + allocated_clients_rva, prepared.allocated_clients) ||
			!read(base + cached_capacity_rva, prepared.cached_capacity) ||
			!safemem::RegionExecutable(reinterpret_cast<const void*>(base + party_slots_rva)))
			return prepared;
		prepared.party_capacity = reinterpret_cast<int (*)(std::uintptr_t)>(base + party_slots_rva)(party);
		if (prepared.party_capacity < 1 || prepared.party_capacity > 200 || prepared.allocated_clients < 0)
		{
			prepared.state = status::invalid_capacity;
			return prepared;
		}

		std::uintptr_t dvar{};
		std::uint8_t type{};
		prepared.state = status::invalid_dvar;
		if (!read(base + ui_maxclients_rva, dvar) || !dvar || dvar > UINTPTR_MAX - dvar_current_offset - sizeof(int) ||
			!read(dvar + dvar_type_offset, type) || type != 5 || !read(dvar + dvar_current_offset, prepared.ui_before))
			return prepared;

		// An existing hunk cannot be resized by changing a dvar. Keep the native
		// allocation counters untouched and let the caller drop an inconsistent join.
		if (prepared.allocated_clients)
		{
			prepared.state = prepared.cached_capacity == prepared.party_capacity
				? status::unchanged : status::existing_allocation_mismatch;
			return prepared;
		}
		if (prepared.ui_before == prepared.party_capacity)
		{
			prepared.state = status::unchanged;
			return prepared;
		}

		prepared.state = status::update_failed;
		if (!safemem::RegionExecutable(reinterpret_cast<const void*>(base + set_int_rva))) return prepared;
		// Replay PartyHost_StartMap uses these same native getter/setter operations
		// before GameAllocate. A remote Island join must publish the received slots too.
		reinterpret_cast<void (*)(std::uintptr_t, int)>(base + set_int_rva)(dvar, prepared.party_capacity);
		int actual{};
		std::uintptr_t current_dvar{};
		if (read(base + ui_maxclients_rva, current_dvar) && current_dvar == dvar &&
			read(dvar + dvar_current_offset, actual) && actual == prepared.party_capacity)
			prepared.state = status::synchronized;
		return prepared;
	}

	const char* status_name(status state) noexcept
	{
		switch (state)
		{
		case status::skipped: return "skipped";
		case status::unchanged: return "unchanged";
		case status::synchronized: return "synchronized";
		case status::unreadable_state: return "unreadable native state";
		case status::invalid_capacity: return "invalid native capacity";
		case status::invalid_dvar: return "invalid ui_maxclients dvar";
		case status::update_failed: return "ui_maxclients native setter readback failed";
		case status::existing_allocation_mismatch: return "existing client allocation has a different capacity";
		}
		return "unknown";
	}
}
