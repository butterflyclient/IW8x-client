#include "asset_mark_guard.h"

#include "engine/addresses.hpp"

#include "auto_server.h"
#include "dedicated.h"

#include "window.h"

#include "bap.h"
#include "bot_command_cache.h"
#include "bot_goal_wait.h"
#include "bot_profiler.h"
#include "bot_movement_batch.h"
#include "bot_nav_gpu.h"
#include "commands.h"
#include "console.h"

#include "game.h"

#include "join.h"
#include "noclip.h"
#include "online_services.h"
#include "playlists.h"

#include "safemem.h"

#include "thirdparty/minhook/include/MinHook.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <string_view>

#include <intrin.h>

#include "dedicated/runtime/internal.hpp"
#include "network/backend/native_matchmaking.h"
#include "dedicated/assets/infil_transients.h"
#include "auth/profile/local_progression.h"
namespace dedicated::detail
{
	static std::atomic<unsigned> g_botSpawnHandle{ 0 };

	static std::atomic<uintptr_t> g_botScriptBuffer{ 0 };

	std::atomic<unsigned> g_botScriptGeneration{ 0 };

	// Only the server-command thread accesses entries. Com_Frame merely queues a
	// command while work remains; it must never access or execute the script VM.
	static std::array<PendingBotClass, 200> g_pendingBotClasses{};

	std::atomic<bool> g_hasPendingBotClasses{ false };

	std::atomic<bool> g_botClassCommandQueued{ false };

	std::atomic<unsigned> g_pendingBotAdds{ 0 };

	uintptr_t g_botGoalWait = 0;

	// resolved again before each script lookup cleanup
	int g_botGoalInterval = 1000, g_botGoalApplied = 50;

	ULONGLONG g_nextBotClassCheck = 0;

	static constexpr size_t kScrLookupObjectOffset = 3172;

	static constexpr size_t kScrProgramLengthOffset = 3180;

	static constexpr size_t kScrProgramBufferOffset = 13640;

	bool ApplyBotGoalInterval()
	{
		BotGoalWaitBytes bytes{};
		if (!g_botGoalWait || !safemem::ReadBytes(reinterpret_cast<void*>(g_botGoalWait), bytes.data(), bytes.size()) ||
			!PrepareBotGoalWait(bytes, g_botGoalApplied, g_botGoalInterval) ||
			!safemem::WriteBytes(reinterpret_cast<void*>(g_botGoalWait + 1), bytes.data() + 1, sizeof(float)))
			return false;
		g_botGoalApplied = g_botGoalInterval;
		return true;
	}

	void ResolveBotScripts(void* context)
	{
		if (context == reinterpret_cast<ScriptContextFn>(g_base + game::kScriptContextServerRVA)())
		{
			g_botSpawnHandle.store(0, std::memory_order_release);
			g_pendingBotAdds.store(0, std::memory_order_release);
			g_botScriptGeneration.fetch_add(1, std::memory_order_relaxed);
			g_botDecisions = g_botHeldCommands = 0;
			g_botGoalWait = 0;
			g_botGoalApplied = 50;
			const auto* bytes = static_cast<const uint8_t*>(context);
			// File20 is scripts/mp/bots/bots; 0xBB54 is spawn_bot_latent in Replay's
			// canonical table. Resolve every load before native cleanup destroys
			// the lookup objects; bytecode offsets vary with loaded map/scripts.
			unsigned handle = 0;
			if (*reinterpret_cast<const unsigned*>(bytes + kScrLookupObjectOffset))
			{
				handle = reinterpret_cast<ScrGetFunctionHandleFn>(g_base + game::kScrGetFunctionHandleRVA)(context, "20", 0xBB54);
			}
			g_botScriptBuffer.store(*reinterpret_cast<const uintptr_t*>(bytes + kScrProgramBufferOffset), std::memory_order_relaxed);
			g_botSpawnHandle.store(handle, std::memory_order_release);
			LOG_INFO("Dedicated", "native bot spawn lifecycle resolved | handle=0x%X", handle);
			if (!_stricmp(g_gameType, "br") && *reinterpret_cast<const unsigned*>(bytes + kScrLookupObjectOffset))
			{
				const auto lookup = reinterpret_cast<ScrGetFunctionHandleFn>(g_base + game::kScrGetFunctionHandleRVA);
				unsigned think = lookup(context, "scripts/mp/bots/bots_gametype_br.gsc", 0x2039);
				if (!think) think = lookup(context, "scripts/mp/bots/bots_gametype_br", 0x2039);
				const auto length = *reinterpret_cast<const unsigned*>(bytes + kScrProgramLengthOffset);
				const auto buffer = g_botScriptBuffer.load(std::memory_order_relaxed);
				// Resolve by function, never scan the entire script arena. Fail closed
				// on changed bytecode; this alteration is local to dedicated BR.
				if (buffer && think && std::uint64_t(think) + kBotGoalWaitOffset + BotGoalWaitBytes{}.size() <= length)
					g_botGoalWait = buffer + think + kBotGoalWaitOffset;
				if (ApplyBotGoalInterval())
					LOG_INFO("Dedicated", "BR bot roaming loop: %d ms | function=0x%X", g_botGoalApplied, think);
				else
				{
					g_botGoalWait = 0;
					LOG_WARN("Dedicated", "BR bot roaming wait guard failed | function=0x%X; native timing retained", think);
				}
			}
		}
	}

	unsigned AvailableBotSlots()
	{
		if (!ServerRunningForTrace()) return 0;
		const int allocated = *reinterpret_cast<const int*>(g_base + game::addresses::runtime::allocated_clients);
		if (allocated < 1 || allocated > 200) return 0;
		const int capacity = g_planPrepared && g_playlistPlan.maxPlayers > 0 ? (std::min)(allocated, g_playlistPlan.maxPlayers) : allocated;
		unsigned occupied = 0, freeSlots = 0;
		const auto party = reinterpret_cast<uintptr_t (*)()>(g_base + game::addresses::runtime::rva_0137d670)();
		for (int slot = 0; slot < allocated; ++slot)
		{
			const auto client = *reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::server_clients + 8ull * slot);
			if (client && *reinterpret_cast<const uint8_t*>(client + 8)) ++occupied;
			// Match DirectConnect's registered-member exclusion; a free SV client
			// may still belong to the headless host or an arriving human.
			else if (client && party &&
				!reinterpret_cast<bool (*)(uintptr_t, int)>(g_base + game::addresses::runtime::rva_0118e210)(party, slot))
				++freeSlots;
		}
		return occupied < static_cast<unsigned>(capacity) ? (std::min)(capacity - occupied, freeSlots) : 0;
	}

	bool AddOneBot(const char* requestedName, bool announce)
	{
		if (g_stage != Stage::Running || !ServerRunningForTrace())
		{
			OP_WARN("Bots unavailable until the server is running");
			return false;
		}

		if (!AvailableBotSlots())
		{
			OP_WARN("No bot slots available in the current playlist (human players also use slots)");
			return false;
		}

		// TU19 BR playlists disable player repulsors to save human-only server
		// work. Native Bot_UpdateCmd1349800 explicitly requires them, and the
		// player spawn/update paths skip creating them while this bool is true.
		// Restore the registered native setting before any bot enters the world.
		if (!SetStartupBool(game::addresses::runtime::rva_0bc21408, false))
		{
			OP_WARN("Bots unavailable: native player repulsor setting is not initialized");
			return false;
		}

		// These RVAs were established by symbol-build -> Replay structural mapping, not byte identity.
		// Exact instructions differ between builds; this exact Replay target only needs executable entry points.
		if (!safemem::RegionExecutable(reinterpret_cast<void*>(g_base + game::kSVClientMPAddBotRVA)))
		{
			OP_WARN("Bot functions are not mapped executable in the current Replay runtime state");
			return false;
		}

		void* context = reinterpret_cast<ScriptContextFn>(g_base + game::kScriptContextServerRVA)();
		const auto* bytes = static_cast<const uint8_t*>(context);
		const unsigned spawnHandle = g_botSpawnHandle.load(std::memory_order_acquire);
		if (!spawnHandle || spawnHandle >= *reinterpret_cast<const unsigned*>(bytes + kScrProgramLengthOffset) ||
			g_botScriptBuffer.load(std::memory_order_relaxed) != *reinterpret_cast<const uintptr_t*>(bytes + kScrProgramBufferOffset))
		{
			OP_WARN("Bots unavailable: native spawn script is not loaded for this map");
			return false;
		}

		// The stock console allocation API does not enable bot participation.
		// Use the native setter used by Lobby.SetBotsConnectType: it updates both
		// bot_autoconnectdefault and active match settings[32]. A dvar-only write
		// leaves the script getter off. Mode1 is enabled, not mode2 fill-open.
		if (!reinterpret_cast<uint8_t (*)()>(g_base + game::kBGGetBotsConnectTypeRVA)())
		{
			reinterpret_cast<void (*)(uint8_t)>(g_base + game::kBGSetBotsConnectTypeRVA)(1);
			LOG_INFO("Dedicated", "manual bot request enabled native bot participation mode");
		}
		const char* name = requestedName ? requestedName : "";
		auto add = reinterpret_cast<SVClientMPAddBotFn>(g_base + game::kSVClientMPAddBotRVA);
		void* ent = add(name, 0, 0, 0);
		if (!ent)
		{
			OP_WARN("Bot could not be allocated (server full or bot spawning unavailable)");
			return false;
		}
		// Follow stock spawn_bots -> spawn_bot_latent(team, callback, completion).
		// Native EnterWorld alone skips the latent readiness/setup sequence. In
		// particular, a late bot needs the script's loadout selection notification.
		// Arguments are pushed right-to-left; the VM owns the completion struct.
		reinterpret_cast<ScrContextOpFn>(g_base + game::kScrAddStructRVA)(context);
		reinterpret_cast<ScrContextOpFn>(g_base + game::kScrAddUndefinedRVA)(context);
		reinterpret_cast<ScrAddStringFn>(g_base + game::kScrAddStringRVA)(context, "autoassign");
		const unsigned thread = reinterpret_cast<GScrExecEntThreadFn>(g_base + game::kGScrExecEntThreadRVA)(ent, spawnHandle, 3);
		reinterpret_cast<ScrFreeThreadFn>(g_base + game::kScrFreeThreadRVA)(context, thread);
		const unsigned slot = *static_cast<const uint16_t*>(ent);
		if (slot < g_pendingBotClasses.size())
		{
			g_botCommands[slot].Invalidate(); // The native slot/entity address can be reused.
			const unsigned object = reinterpret_cast<ScrFindEntityIdFn>(g_base + game::kScrFindEntityIdRVA)(context, slot, 0, 0);
			g_pendingBotClasses[slot] = { object, g_botScriptGeneration.load(std::memory_order_relaxed), GetTickCount64() + 60000, false };
			g_hasPendingBotClasses.store(true, std::memory_order_release);
		}
		if (announce) OP_INFO("Bot queued through native spawn script%s%s", name[0] ? " | " : "", name[0] ? name : "");
		return true;
	}

	BotScriptValue BotScriptField(void* context, unsigned object, unsigned canonicalName)
	{
		const unsigned child = reinterpret_cast<ScrFindVariableFn>(g_base + game::kScrFindVariableRVA)(context, object, canonicalName);
		if (!child) return {};
		// Replay ChildVariableValue: 32-byte stride; value union at0, type at12.
		// Integer and string IDs only use the low DWORD of the value union.
		const auto children = *reinterpret_cast<const uintptr_t*>(static_cast<const uint8_t*>(context) + 13504);
		const auto* value = reinterpret_cast<const uint8_t*>(children + 32ull * child);
		return { *reinterpret_cast<const unsigned*>(value), static_cast<uint8_t>(value[12] & 63) };
	}

	void CompleteBotClassCommands()
	{
		g_botClassCommandQueued.store(false, std::memory_order_release);
		if (!ServerRunningForTrace())
		{
			g_pendingBotAdds = 0;
			return;
		}
		// Native allocation + script initialization is expensive. Spread bulk adds
		// over server commands instead of blocking one frame on 150 allocations.
		// Recheck capacity each time: humans can join while the queue drains.
		const auto pendingAdds = g_pendingBotAdds.load(std::memory_order_acquire);
		if (pendingAdds)
		{
			if (!AvailableBotSlots() || !AddOneBot("", false))
			{
				g_pendingBotAdds = 0;
				OP_INFO("Bot fill stopped | no available slots or spawn unavailable");
			}
			else
			{
				g_pendingBotAdds = pendingAdds - 1;
				if (pendingAdds == 1) OP_INFO("Bot fill completed");
			}
		}
		void* context = reinterpret_cast<ScriptContextFn>(g_base + game::kScriptContextServerRVA)();
		const auto findEntity = reinterpret_cast<ScrFindEntityIdFn>(g_base + game::kScrFindEntityIdRVA);
		const unsigned generation = g_botScriptGeneration.load(std::memory_order_relaxed);
		const auto now = GetTickCount64();
		const auto entities = *reinterpret_cast<const uintptr_t*>(g_base + game::kGEntitiesPtrRVA);
		const int count = *reinterpret_cast<const int*>(g_base + game::addresses::runtime::allocated_clients);
		bool remaining = false;
		for (unsigned slot = 0; slot < g_pendingBotClasses.size(); ++slot)
		{
			auto& pending = g_pendingBotClasses[slot];
			if (!pending.object) continue;
			if (!entities || pending.generation != generation || now >= pending.deadline ||
				slot >= static_cast<unsigned>(count > 0 ? count : 0))
			{
				pending = {};
				continue;
			}
			const auto client = *reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::server_clients + 8ull * slot);
			if (!client || *reinterpret_cast<const int*>(client + 214756) != 2 || findEntity(context, slot, 0, 0) != pending.object)
			{
				pending = {}; // Disconnected, replaced by a human, or map restarted.
				continue;
			}
			const auto spawned = BotScriptField(context, pending.object, 25090); // hasspawned
			if (spawned.IsTrue())
			{
				pending = {};
				continue;
			}
			const auto waiting = BotScriptField(context, pending.object, 56344); // waitingtoselectclass
			const auto ready = BotScriptField(context, pending.object, 9589); // bwaitingforteamselect
			const auto choice = BotScriptField(context, pending.object, 8277); // bot_class
			const bool needsSelection = waiting.IsTrue() && ready.IsTrue() && choice.type == 2;
			if (needsSelection && pending.wasWaiting)
			{
				const char* selected = reinterpret_cast<SLConvertToStringFn>(g_base + game::kSLConvertToStringRVA)(choice.value);
				const unsigned event = reinterpret_cast<SLFindStringFn>(g_base + game::kSLFindStringRVA)("loadout_class_selected");
				if (event && selected && selected[0] &&
					(strcmp(selected, "callback") || BotScriptField(context, pending.object, 11772).type == 9))
				{
					// Replay bot_think can select its class before setteam starts
					// waiting for this event. Pre-infiltration AI bypasses the wait;
					// late bots do not. Deliver the stock choice once the wait is
					// stable across server commands; native scripts decide spawning.
					reinterpret_cast<ScrAddStringFn>(g_base + game::kScrAddStringRVA)(context, selected);
					reinterpret_cast<GScrNotifyFn>(g_base + game::kGScrNotifyRVA)(
						reinterpret_cast<void*>(entities + game::kGEntityStride * slot), event, 1);
					LOG_INFO("Dedicated", "late bot class selection delivered | slot=%u", slot);
					pending = {};
					continue;
				}
			}
			pending.wasWaiting = needsSelection;
			remaining = true;
		}
		g_hasPendingBotClasses.store(remaining, std::memory_order_release);
	}

	void AddBotCommand()
	{
		char name[64]{};
		ReadSVCmdArg(1, name, sizeof(name));
		AddOneBot(name);
	}

	void AddBotsCommand()
	{
		char countText[32]{};
		ReadSVCmdArg(1, countText, sizeof(countText));
		char* end = nullptr;
		const long requested = std::strtol(countText, &end, 10);
		if (!countText[0] || !end || *end || requested < 1 || requested > 200)
		{
			OP_WARN("Usage: addbots <1..200> | available playlist slots=%u", AvailableBotSlots());
			return;
		}
		const unsigned available = AvailableBotSlots();
		const unsigned pending = g_pendingBotAdds.load(std::memory_order_acquire);
		const unsigned added = (std::min)(static_cast<unsigned>(requested), available > pending ? available - pending : 0);
		g_pendingBotAdds = pending + added;
		OP_INFO("Bot fill queued | %u/%ld | total pending=%u (one per 100 ms)", added, requested, pending + added);
	}

	bool ActiveBot(unsigned slot)
	{
		const int count = *reinterpret_cast<const int*>(g_base + game::addresses::runtime::allocated_clients);
		if (!ServerRunningForTrace() || count < 1 || count > 200 || slot >= static_cast<unsigned>(count)) return false;
		const auto client = *reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::server_clients + 8ull * slot);
		// Native client type2 is a bot; humans and test clients are never targets.
		return client && *reinterpret_cast<const uint8_t*>(client + 8) >= 2 && *reinterpret_cast<const int*>(client + 214756) == 2;
	}

	bool RemoveOneBot(unsigned slot)
	{
		if (!ActiveBot(slot)) return false;
		g_pendingBotClasses[slot] = {}; // Do not notify a removed/reused entity later.
		g_botCommands[slot].Invalidate();
		reinterpret_cast<void (*)(unsigned, const char*, bool)>(g_base + kKickClientRVA)(slot, "EXE/PLAYERKICKED", false);
		return true;
	}

	void BotStatusCommand()
	{
		if (!ServerRunningForTrace())
		{
			OP_INFO("Bots: server not running");
			return;
		}
		unsigned bots = 0;
		for (unsigned slot = 0; slot < 200; ++slot)
		{
			if (!ActiveBot(slot)) continue;
			const auto client = *reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::server_clients + 8ull * slot);
			char name[36]{};
			safemem::ReadString(reinterpret_cast<const char*>(client + 0x534), name, sizeof(name));
			OP_INFO("Bot | slot=%u name=\"%s\"", slot, name);
			++bots;
		}
		OP_INFO("Bots: active=%u pending=%u free playlist slots=%u", bots, g_pendingBotAdds.load(), AvailableBotSlots());
		OP_INFO("Bot decisions: %s | override=%d ms | decisions=%llu held-input frames=%llu",
			g_botThinkOverride < 0 ? "auto BR: near100/mid250/far1000; MP:native" : "manual", g_botThinkOverride,
			static_cast<unsigned long long>(g_botDecisions), static_cast<unsigned long long>(g_botHeldCommands));
	}

	void RemoveBotCommand()
	{
		char target[64]{};
		ReadSVCmdArg(1, target, sizeof(target));
		unsigned selected = 200, matches = 0;
		if (target[0])
		{
			unsigned slot = 200;
			const auto end = target + strlen(target);
			const auto parsed = std::from_chars(target, end, slot);
			if (parsed.ec == std::errc{} && parsed.ptr == end)
			{
				if (RemoveOneBot(slot))
					OP_INFO("Bot removed | slot=%u", slot);
				else
					OP_WARN("Slot %u is not an active bot; no player removed", slot);
				return;
			}
		}
		for (unsigned slot = 0; slot < 200; ++slot)
		{
			if (!ActiveBot(slot)) continue;
			const auto client = *reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::server_clients + 8ull * slot);
			char name[36]{};
			safemem::ReadString(reinterpret_cast<const char*>(client + 0x534), name, sizeof(name));
			if (!target[0] || !_stricmp(target, name))
			{
				selected = slot;
				++matches;
				if (!target[0]) break;
			}
		}
		if (matches == 1 && RemoveOneBot(selected))
			OP_INFO("Bot removed | slot=%u", selected);
		else
			OP_WARN("No unique bot matches '%s'; use bot_status then removebot <slot>", target);
	}

	void RemoveBotsCommand()
	{
		char text[32]{};
		ReadSVCmdArg(1, text, sizeof(text));
		unsigned count = 0;
		const auto end = text + strlen(text);
		const auto parsed = std::from_chars(text, end, count);
		if (!_stricmp(text, "all"))
			count = 200;
		else if (parsed.ec != std::errc{} || parsed.ptr != end || count < 1 || count > 200)
		{
			OP_WARN("Usage: removebots <1..200|all>");
			return;
		}
		// Explicit removal also cancels outstanding bulk additions, so removed bots
		// do not immediately reappear as the earlier fill request continues.
		const auto cancelled = g_pendingBotAdds.exchange(0, std::memory_order_acq_rel);
		unsigned removed = 0;
		for (unsigned slot = 0; slot < 200 && removed < count; ++slot)
			if (RemoveOneBot(slot)) ++removed;
		OP_INFO("Bots removed | %u | pending additions cancelled=%u", removed, cancelled);
	}
}
