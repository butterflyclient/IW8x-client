#include "dedicated/runtime/island_plunder_loadout_policy.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
	int checks{};
	void expect(const bool condition, const char* description)
	{
		++checks;
		if (condition) return;
		std::cerr << "FAILED: " << description << '\n';
		std::exit(1);
	}
}

int main()
{
	using namespace dedicated::detail::island_plunder_loadout_policy;
	static_assert(early_class_loadout_required(104, "mp_sm_island_1", "br"));
	static_assert(!early_class_loadout_required(104, "mp_donetsk", "br"));
	static_assert(!early_class_loadout_required(104, "mp_sm_island_1", "dmz"));
	static_assert(!early_class_loadout_required(89, "mp_sm_island_1", "br"));
	static_assert(preserve_explicit_island_recycle(104, "mp_sm_island_1", "br", true, false));
	static_assert(!preserve_explicit_island_recycle(104, "mp_sm_island_1", "br", true, true));

	for (int playlist = -1; playlist <= 150; ++playlist)
	{
		for (std::string_view map : { "mp_sm_island_1", "mp_donetsk", "mp_shipment", "" })
		{
			for (std::string_view mode : { "br", "dmz", "war", "" })
			{
				const bool selected = playlist == 104 && map == "mp_sm_island_1" && mode == "br";
				expect(early_class_loadout_required(playlist, map, mode) == selected,
					"Plunder loadout preinit escaped its playlist/map/mode scope");
					for (const bool explicit_map : { false, true })
						for (const bool rotate : { false, true })
							expect(preserve_explicit_island_recycle(playlist, map, mode,
								explicit_map, rotate) == (selected && explicit_map && !rotate),
								"Recycled Island map/mode override escaped exact explicit launch scope");
			}
		}
	}
	const std::string commands(early_class_loadout_commands);
	expect(commands == "set scr_br_allowLoadout 1\nset scr_br_allowLoadoutOnlyInPreLobby 0\n",
		"Original 1.64 Plunder cached-dvar preinit commands differ");
	expect(commands.find("scr_br_loadout_option") == std::string::npos,
		"Plunder preinit must not overwrite its subclass's actual loadout option");
	expect(commands.find("rankedloadouts") == std::string::npos,
		"Plunder preinit must not change the existing ranked DDL source");
	expect(commands.find("giveweapon") == std::string::npos,
		"Plunder preinit must not inject synthetic weapons");
	expect(commands.find("allowLoadoutOnlyInPreLobby 0\n") != std::string::npos,
		"Plunder must permit class restoration after regular infil and respawn");
	std::cout << "PASSED " << checks << " Island Plunder early loadout startup scope checks\n";
}
