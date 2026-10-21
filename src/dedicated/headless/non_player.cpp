#include "dedicated/headless/non_player.h"

#include "engine/addresses.hpp"

#include "common/console.h"

#include "dedicated/dedicated.h"
#include "network/backend/native_matchmaking.h"

#include "engine/safemem.h"

#include <atomic>
#include <cstring>
#include <cwchar>
#include <iterator>
namespace headless
{
	namespace
	{
		// TU19 PartyHost_StartParty: hostType1 selects native hostIndex=-2. This is
		// distinct from the PartyMigrate flag at+8, which MUST remain native.
		using StartParty = uintptr_t (*)(void*, unsigned, unsigned, unsigned, unsigned, int, uintptr_t, const void*);
		using AddLocal = uintptr_t (*)(void*, const unsigned*, int);
		std::atomic<StartParty> startParty{ nullptr };
		std::atomic<AddLocal> addLocal{ nullptr };
		std::atomic<bool> active{ false };
		uintptr_t imageBase = 0;

		uintptr_t Start(void* party, unsigned local, unsigned controller, unsigned flags, unsigned capacity, int hostType, uintptr_t extra,
			const void* session)
		{
			if (native_matchmaking::UseOnlineLobby())
			{
				// DevCmdSetup also sets forceSystemLink independently of systemlink.
				// Live_GetGameParty1527070 otherwise chooses the empty PrivateParty,
				// generating/registering a different security session for the server.
				uintptr_t forceSystemLink = 0;
				memcpy(&forceSystemLink, reinterpret_cast<void*>(imageBase + 0xee55920), sizeof(forceSystemLink));
				reinterpret_cast<void (*)(uintptr_t, bool)>(imageBase + 0x13e8b50)(forceSystemLink, false);
				// Stock matchmaking joins partyID0. Devmap normally creates partyID1;
				// initialize the generic native host on GameParty instead. Never rewrite
				// incoming packet party IDs or bypass member/security admission checks.
				party = reinterpret_cast<void*>(imageBase + native_matchmaking::ServerPartyRVA());
				reinterpret_cast<void (*)(void*, unsigned, int)>(imageBase + 0x1189160)(party, controller, 1);
				flags = reinterpret_cast<unsigned (*)()>(imageBase + 0x119a680)();
			}
			const auto result = startParty.load()(party, local, controller, flags, capacity, 1, extra, session);
			int index = 0;
			memcpy(&index, static_cast<char*>(party) + 311000, 4);
			LOG_INFO("Dedicated", "native non-player host initialized: hostIndex=%d capacity=%u", index, capacity);
			return result;
		}

		uintptr_t Local(void*, const unsigned*, int)
		{
			// Normal client StartParty unconditionally calls AddLocalPlayer11977A0.
			// A dedicated session owner has no controller/player to register. Suppress
			// that whole client operation, including its roster insertion/loopback join;
			// remote admission continues through the unchanged PartyHost handlers.
			static bool logged = false;
			if (!logged)
			{
				logged = true;
				LOG_INFO("Dedicated", "local player registration suppressed; native hostIndex=-2 has no gameplay slot");
			}
			return 0;
		}
	}

	bool NonPlayerHost()
	{
		return active.load(std::memory_order_acquire);
	}

	hook::Status InstallNonPlayer(uintptr_t base)
	{
		if (!dedicated::Enabled()) return hook::Status::Installed;
		if (active.load(std::memory_order_acquire)) return hook::Status::Installed;
		// Every dedicated role is a session owner, never a human controller/slot.
		// Explicit legacy transport changes the party route, not host population.
		if (native_matchmaking::UseOnlineLobby())
		{
			constexpr uint8_t awake[] = { 0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x30, 0x83, 0xb9, 0x50, 0xbf, 0x04, 0x00, 0x00, 0x41 };
			constexpr uint8_t flags[] = { 0x40, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x05, 0x93, 0xc1, 0x1e, 0x0c, 0xbf, 0x2f, 0x00 };
			uint8_t actual[16]{};
			if (!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x1189160), actual, 16) || memcmp(actual, awake, 16) ||
				!safemem::ReadBytes(reinterpret_cast<void*>(base + 0x119a680), actual, 16) || memcmp(actual, flags, 16))
				return hook::Status::NotReady;
		}
		imageBase = base;
		constexpr uint8_t start[] = { 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x81, 0xec, 0x88, 0x01, 0x00 };
		constexpr uint8_t local[] = { 0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
		hook::Batch batch;
		batch.Add(reinterpret_cast<void*>(base + 0x11977a0), Local, local, sizeof(local), addLocal);
		batch.Add(reinterpret_cast<void*>(base + 0x119e780), Start, start, sizeof(start), startParty);
		const auto status = batch.Commit();
		if (status == hook::Status::Installed) active.store(true, std::memory_order_release);
		return status;
	}
}
