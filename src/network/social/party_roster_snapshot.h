#pragma once
#include "engine/safemem.h"

#include <array>
#include <vector>
namespace party_roster
{
	// Verified TU19 member stride488, status+2594, XUID+2600. The game-thread
	// snapshot avoids 400 VirtualQuery calls for every chat/social refresh.
	inline std::vector<uint64_t> Members(uintptr_t party)
	{
		std::array<unsigned char, 488 * 200> bytes{};
		std::vector<uint64_t> members;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(party + 2192), bytes.data(), bytes.size())) return members;
		for (size_t i = 0; i < 200; ++i)
		{
			const auto* member = bytes.data() + 488 * i;
			if (member[402] != 4 && member[402] != 5) continue;
			uint64_t id = 0;
			memcpy(&id, member + 408, sizeof(id));
			if (id) members.push_back(id);
		}
		return members;
	}
}
