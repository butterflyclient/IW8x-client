#include "../../src/dedicated/island/original_spawn_group_runtime.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
	std::ifstream stream(argv[1], std::ios::binary);
	if (!stream) return 3;
	const std::vector<std::uint8_t> contents{std::istreambuf_iterator<char>(stream),
		std::istreambuf_iterator<char>()};
	using namespace iw8x::island::original_spawn_group;
	state state;
	assert(!state.loaded());
	assert(!state.import(std::span(contents.data(), contents.size()-1)));
	assert(!state.loaded());
	assert(state.import(contents));
	assert(state.loaded());
	assert(state.active(2) == world_points);
	assert(state.active(3) == loot_caches);
	assert(state.active(5) == 1);
	assert(state.at(quest_index) && state.at(quest_index)->source_type == 5);
	assert(state.at(quest_index)->origin[2] == 505.5f);
	assert(!state.disabled(quest_index));
	assert(state.disable(quest_index));
	assert(state.disabled(quest_index));
	assert(state.active(5) == 0);
	assert(state.disable(quest_index));
	assert(state.active(5) == 0); // original flag OR is idempotent
	assert(state.disable(world_points));
	assert(state.active(3) == loot_caches - 1);
	assert(state.active(2) == world_points);
	assert(!state.disable(total_points));
	state.reset(); // map_restart semantics: original flags restored once
	assert(state.active(5) == 1 && state.active(3) == loot_caches);
	auto corrupted = contents;
	corrupted[quest_index * original_point_bytes + 18] = 3;
	assert(!state.import(corrupted));
	assert(state.active(5) == 1); // reject malformed source without clobbering live state
    std::ifstream memberships_stream(argv[2], std::ios::binary);
    if (!memberships_stream) return 4;
    std::vector<std::uint8_t> relations{std::istreambuf_iterator<char>(memberships_stream),
        std::istreambuf_iterator<char>()};
    memberships source_ownership;
    assert(source_ownership.import(relations));
    assert(source_ownership.loaded());
    assert(source_ownership.clusters(0).size() == 1 && source_ownership.clusters(0)[0] == 0);
    assert(source_ownership.zones(0).size() == 1 && source_ownership.zones(0)[0] == 4);
    for (std::size_t i = 0; i < total_points; ++i)
    {
        assert(source_ownership.clusters(i).size() == (i < world_points ? 1 : 0));
        assert(source_ownership.zones(i).size() == (i == quest_index ? 0 : 1));
    }
    assert(source_ownership.clusters(quest_index).empty() && source_ownership.zones(quest_index).empty());
    auto malformed_relations = relations;
    malformed_relations[0] ^= 1; // unknown source format must not replace valid ownership
    assert(!source_ownership.import(malformed_relations));
    assert(source_ownership.zones(0)[0] == 4);
	std::cout << "PASS: 3543 original Island native 20-byte points (2676 world, 866 cache, 1 unassigned quest), source OR disable flags, clean restart and fail-closed import\n";
}
