#include "client/frontend/custom_map_names.h"
#include "diagnostics/night/night_files.h"

#include <atomic>

namespace custom_map_names
{
	namespace
	{
		using translate_fn = const char* (*)(const char*);
		std::atomic<translate_fn> original{};

		const char* translate(const char* key)
		{
			// TU19 has no arena/localize entry for newer custom map names. Both
			// native UI and Lua use this resolver while the map is still preloading.
			const auto* name = key ? night_files::map_display_name(key) : nullptr;
			return name ? name : original.load(std::memory_order_acquire)(key);
		}
	}

	hook::Status install(uintptr_t base)
	{
		if (!night_files::Enabled() || original.load(std::memory_order_acquire)) return hook::Status::Installed;
		// Replay SEH_StringEd_GetString: localize type 41, value at entry+8.
		constexpr uint8_t bytes[]{ 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0xCD,
			0x1F, 0xF6, 0x0C, 0x48, 0x85, 0xC0, 0x74, 0x53 };
		return hook::Install(reinterpret_cast<void*>(base + 0x13CC2A0), &translate, bytes, sizeof(bytes), original);
	}
}
