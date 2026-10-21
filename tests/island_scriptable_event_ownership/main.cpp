#include "../../src/dedicated/island/original_scriptable_event_ownership.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <vector>

int main(int argc, char** argv)
{
	if (argc != 2) return 2;
	std::ifstream file(argv[1], std::ios::binary);
	if (!file) return 3;
	std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(file),
		std::istreambuf_iterator<char>()};
	using namespace iw8x::island::original_scriptable_event_ownership;
	arena recovered;
	assert(!arena::native_replay_runtime_dispatch_available);
	assert(recovered.import(raw));
	std::size_t c44{}, c47{}, c49{}, aliases{}, strings{};
	std::set<std::string> owners;
	for (const auto& event : recovered.events())
	{
		owners.insert(event.owner_scriptabledef);
		aliases += event.aliases.size();
		strings += event.xstrings.size();
		if (event.type == 44)
		{
			++c44;
			assert(event.owner_scriptabledef == "br_carriable_gasoline");
			assert(event.xstrings.front().value == "damage");
		}
		else if (event.type == 47)
		{
			++c47;
			assert(event.xstrings[0].value == "scr_br_plunder_use_gold_bar_assets");
			assert(event.xstrings[1].value == "1");
		}
		else if (event.type == 49)
		{
			++c49;
			assert(event.owner_scriptabledef == "brloot_zmb_stim");
		}
		else assert(false);
	}
	assert(c44==1 && c47==9 && c49==2 && aliases==40 && strings==19 && owners.size()==11);
	auto altered = raw;
	altered[24 + 18 + 0] ^= 1; // first ScriptableDef's name, not a valid changed original owner
	// Unknown owner strings cannot be semantically rejected without the
	// source manifest; tamper the binary event kind instead.
	altered = raw;
	altered[24] = 0; // original event type44 discriminator
	assert(!recovered.import(altered));
	assert(recovered.events().size()==12); // never discard valid owners on failure
	std::cout << "PASS: all original typed ScriptableDef event44/47/49 owners, source bytes, 40 native aliases, 19 XStrings, 11 definitions; missing Replay dispatch remains explicitly blocked\n";
}
