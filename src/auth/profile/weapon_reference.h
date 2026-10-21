#pragma once
#include <string>
#include <string_view>
namespace progression
{
	// Live TU19 kill/mp_addxp events use e.g. iw8_sm_mpapa5_mp. Inventory and
	// weaponLevel DDL use iw8_sm_mpapa5. Resolve only a known base; never collapse
	// an unknown blueprint/variant into a different weapon's progression.
	template <class IsKnown>
	std::string WeaponReference(std::string_view ref, const IsKnown& known)
	{
		if (known(ref)) return std::string(ref);
		if (ref.ends_with("_mp"))
		{
			const auto base = ref.substr(0, ref.size() - 3);
			if (known(base)) return std::string(base);
		}
		return std::string(ref);
	}
}
