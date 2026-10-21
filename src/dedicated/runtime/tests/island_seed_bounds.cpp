#include "dedicated/runtime/island_seed_bounds.hpp"
#include "dedicated/runtime/island_seed_capacity.hpp"

#include <cstdlib>
#include <iostream>

using dedicated::detail::island_seed_bounds::valid;
static_assert(valid(0, 866, 866));
static_assert(valid(865, 866, 866));
static_assert(!valid(866, 866, 866));
static_assert(!valid(867, 866, 866));
static_assert(!valid(0, 867, 866));
static_assert(!valid(0, 0, 866));
static_assert(valid(2675, 2676, 2676));
static_assert(!valid(2676, 2676, 2676));
static_assert(!valid(0, 2677, 2676));
static_assert(!valid(0, 1, 0));
using dedicated::detail::island_seed_capacity::can_spawn;
using dedicated::detail::island_seed_capacity::paused;
using dedicated::detail::island_seed_capacity::work_active;
using dedicated::detail::island_seed_capacity::resume_world_phase;
static_assert(!can_spawn(-1));
static_assert(!can_spawn(0));
static_assert(!can_spawn(64));
static_assert(paused(64));
static_assert(can_spawn(65));
static_assert(can_spawn(1376));
static_assert(!work_active(0));
static_assert(work_active(1));
static_assert(work_active(2));
static_assert(work_active(3));
static_assert(!work_active(4));
static_assert(resume_world_phase(false) == 2);
static_assert(resume_world_phase(true) == 3);

int main()
{
	if (!valid(432, 866, 866) || valid(866, 866, 866) ||
		!valid(2675, 2676, 2676) || valid(2676, 2676, 2676) ||
			can_spawn(0) || can_spawn(64) || !can_spawn(65) ||
			resume_world_phase(false) != 2 || resume_world_phase(true) != 3)
		return EXIT_FAILURE;
	std::cout << "PASS original Island source-point bounds and Replay engine-owned ScriptableSv capacity headroom\n";
}
