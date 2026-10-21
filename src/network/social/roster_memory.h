#pragma once
#include "engine/safemem.h"

#include <array>
namespace party_roster
{
	// Game-thread-only, short-lived read view. Reuse permission checks inside one
	// roster scan, never across frames/map teardown. SV clients share a large arena;
	// querying that arena separately for every field stalls BR's main thread.
	class Memory
	{
		struct Region
		{
			uintptr_t begin = 0, end = 0;
		};

		std::array<Region, 32> regions{};
		size_t count = 0;

	public:
		bool Contains(uintptr_t address, size_t size)
		{
			if (!address || !size || address > UINTPTR_MAX - size) return false;
			for (size_t i = 0; i < count; ++i)
				if (address >= regions[i].begin && address + size <= regions[i].end) return true;
			size_t available = 0;
			if (!safemem::RegionReadable(reinterpret_cast<void*>(address), available) || available < size) return false;
			if (count < regions.size()) regions[count++] = { address, address + available };
			return true;
		}

		bool Copy(uintptr_t address, void* output, size_t size)
		{
			if (!Contains(address, size)) return false;
			memcpy(output, reinterpret_cast<void*>(address), size);
			return true;
		}

		template <class T>
		T Read(uintptr_t address)
		{
			T value{};
			Copy(address, &value, sizeof(value));
			return value;
		}
	};
}
