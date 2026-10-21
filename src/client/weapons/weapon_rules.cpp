#include "client/weapons/weapon_rules.h"
#include "client/weapons/weapon_loadouts.h"
#include "engine/safemem.h"
#include "common/console.h"
#include <array>
#include <cstring>
namespace weapon_rules
{
	namespace
	{
		// Pinned Replay wrappers. Both take a weapon/category reference string and
		// tail-call 159A830, the original bool recipe reader. Live script constants:
		// D20BA10=weaponRestricted; D20BA14=weaponClassRestricted. The server script
		// binding at 12681C0 and native UI share the first wrapper.
		constexpr std::array<uint8_t, 11> weaponBytes{ 0x8B, 0x15, 0x4A, 0x0F, 0xC7, 0x0B, 0xE9, 0x65, 0xFD, 0xFF, 0xFF };
		constexpr std::array<uint8_t, 11> classBytes{ 0x8B, 0x15, 0x5E, 0x0F, 0xC7, 0x0B, 0xE9, 0x75, 0xFD, 0xFF, 0xFF };
		using Query = int (*)(const char*);
		std::atomic<Query> original{};
		Query category{};

		int restricted(const char* ref)
		{
			if (weapon_loadouts::Ready())
			{
				char name[64]{};
				safemem::ReadString(ref, name, sizeof(name));
				if (const auto* admitted = weapon_loadouts::Category(name)) return category(admitted);
			}
			return original.load(std::memory_order_acquire)(ref);
		}
	}

	hook::Status Install(uintptr_t base)
	{
		if (original.load(std::memory_order_acquire)) return hook::Status::Installed;
		std::array<uint8_t, 11> bytes{};
		if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x159AAB0), bytes.data(), bytes.size()) || bytes != classBytes)
			return hook::Status::Failed;
		category = reinterpret_cast<Query>(base + 0x159AAB0);
		const auto result =
			hook::Install(reinterpret_cast<void*>(base + 0x159AAC0), &restricted, weaponBytes.data(), weaponBytes.size(), original);
		if (result == hook::Status::Installed)
			LOG_INFO("WeaponLoadouts", "native weapon restriction bridge installed: admitted ports use original category recipes");
		return result;
	}
}
