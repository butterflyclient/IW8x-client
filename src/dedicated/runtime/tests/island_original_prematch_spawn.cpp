#include "dedicated/runtime/island_original_prematch_spawn.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace dedicated::detail::island_original_prematch_spawn;

static_assert(source_164_locations.size() == 4);
static_assert(source_164_locations[0].center.x == 1486.14f);
static_assert(source_164_locations[1].center.x == 6048.26f);
static_assert(source_164_locations[2].center.x == -646.339f);
static_assert(source_164_locations[3].center.x == -5709.13f);

int main()
{
	int checks{};
	const auto expect = [&](bool good, const char* label)
	{
		++checks;
		if (!good) { std::cerr << "FAILED: " << label << '\n'; std::exit(1); }
	};
	const float radii[] = {5500,5700,5000,4000};
	for (std::size_t i = 0; i < source_164_locations.size(); ++i)
	{
		const auto& original = source_164_locations[i];
		expect(original.radius == radii[i] && original.min_radius == 0, "Original 1.64 Island prematch radius changed");
		expect(source_spawn(i).z == original.center.z + infiltration_offset, "Original 1.64 BR infiltration offset changed");
		expect(source_spawn(i, true).z == original.center.z + map_selection_offset, "Original 1.64 map selection offset changed");
		expect(source_spawn(i + source_164_locations.size()).x == source_spawn(i).x, "Client cycling omitted original location");
	}
	expect(invalid_fallback_position({0,0,0}), "GSC fall-through origin was not detected");
	expect(invalid_fallback_position({100,200,-4000}), "Deep under-map player was not detected");
	expect(invalid_fallback_position({31.6f,10.7f,-245.4f}), "Observed Replay ClientSpawn near-origin under-map fallback was not detected");
	expect(invalid_fallback_position({31.7f,10.7f,-295.9f}), "Observed falling Replay ClientSpawn fallback was not detected");
	expect(!invalid_fallback_position({31.6f,10.7f,-64.0f}), "Ordinary shallow origin must not be relocated");
	expect(!invalid_fallback_position({80.0f,10.7f,-245.4f}), "Out-of-near-origin low altitude must not be relocated");
	expect(!invalid_fallback_position({0,0,800}), "Valid elevated island origin should not be modified");
	expect(!invalid_fallback_position({5000,3000,0}), "Normal sea-level island origin should not be modified");
	expect(!invalid_fallback_position({18000,0,-8000}), "Outside Island bounds should not be teleported into map");
	expect(!invalid_fallback_position({0,0,13000}), "Correctly infiltrating player should be untouched");
	std::cout << "PASSED " << checks << " original IW8 1.64.5 Island prematch spawn policy checks\n";
}
