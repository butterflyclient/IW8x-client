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
namespace dedicated::detail
{
	std::atomic<BotUpdateCommandFn> g_botUpdateCommand{ nullptr };

	static BotProfiler g_botProfiler;

	std::atomic<NavContextFn> g_navContext{ nullptr };

	std::atomic<botnav::NativeReachable> g_navReachable{ nullptr };

	std::atomic<BotBatchFn> g_botBatch{ nullptr };

	std::atomic<BotMovementFn> g_botMovement{ nullptr };

	std::atomic<ClientThinkFn> g_clientThink{ nullptr };

	static bool g_botMovementWorkers = true;

	// BR only; native allocation/byte guards still required
	bool g_botMovementWorkersSupported = false;

	static BotMovementBatch g_botMovementBatch;

	// owning server thread, drained every batch
	static thread_local BotMovementBatch* g_collectBotMovement = nullptr;

	static std::uint64_t g_botWorkerCommands = 0, g_botWorkerFallbacks = 0;

	std::atomic<ReachableSearchFn> g_reachableSearch{ nullptr };

	std::atomic<ScriptGoalFn> g_scriptGoal{ nullptr };

	std::atomic<NavigationProjectionFn> g_navigationProjection{ nullptr };

	std::atomic<ArchiveCharacterFn> g_archiveCharacter{ nullptr };

	std::array<BotCommandCache, 200> g_botCommands{};

	// server thread only
	int g_botThinkOverride = -1;

	// server command thread: -1=automatic, 0=native
	static std::array<std::array<float, 3>, 200> g_botHumanPositions{};

	static unsigned g_botHumanCount = 0, g_botHumanGeneration = 0;

	static int g_botHumanTime = -1;

	std::uint64_t g_botDecisions = 0, g_botHeldCommands = 0;

	int BotThinkInterval(uintptr_t entity, int now)
	{
		if (g_botThinkOverride >= 0) return g_botThinkOverride;
		if (_stricmp(g_gameType, "br")) return 0;
		const auto generation = g_botScriptGeneration.load(std::memory_order_relaxed);
		if (g_botHumanTime < 0 || generation != g_botHumanGeneration || now < g_botHumanTime || now - g_botHumanTime >= 250)
		{
			g_botHumanCount = 0;
			g_botHumanTime = now;
			g_botHumanGeneration = generation;
			const int count = *reinterpret_cast<const int*>(g_base + game::addresses::runtime::allocated_clients);
			for (int slot = 0; slot < count && slot < 200; ++slot)
			{
				const auto client = *reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::server_clients + 8ull * slot);
				if (!client || *reinterpret_cast<const uint8_t*>(client + 8) != 5 || *reinterpret_cast<const int*>(client + 214756) != 0)
					continue;
				const auto human = *reinterpret_cast<const uintptr_t*>(client + 280);
				if (human) std::memcpy(g_botHumanPositions[g_botHumanCount++].data(), reinterpret_cast<void*>(human + 304), 12);
			}
		}
		// Replay's navigator consumes gentity origin at+304. Refresh the small human
		// position list four times/sec; don't scan all client slots for every bot.
		return BotDistanceInterval(reinterpret_cast<const float*>(entity + 304), g_botHumanPositions.data(), g_botHumanCount);
	}

	void ProfiledBotUpdateCommand(uintptr_t client, void* command)
	{
		const auto ticket = g_botProfiler.Begin();
		g_botUpdateCommand.load(std::memory_order_acquire)(client, command);
		g_botProfiler.End(BotProfiler::Decisions, ticket);
	}

	void PrintBotProfile()
	{
		if (!g_botProfiler.Stop()) return;
		OP_INFO("Bot profile | wall=%u ms | inclusive timings overlap; not additive", g_botProfiler.ElapsedMs());
		constexpr const char* names[] = { "native AI", "AI+movement batch", "bot movement", "script goal query", "reachable search",
			"nav projection", "character archive" };
		static_assert(std::size(names) == BotProfiler::Count);
		for (unsigned i = 0; i < BotProfiler::Count; ++i)
		{
			const auto s = g_botProfiler.Read(static_cast<BotProfiler::Kind>(i));
			const auto ms = g_botProfiler.Milliseconds(s.ticks);
			OP_INFO("Bot profile | %s | calls=%llu total=%.2f ms mean=%.3f ms max=%.3f ms >=16ms=%llu", names[i],
				static_cast<unsigned long long>(s.calls), ms, s.calls ? ms / s.calls : 0, g_botProfiler.Milliseconds(s.peak),
				static_cast<unsigned long long>(s.slow));
		}
	}

	void BotBatch()
	{
		const auto ticket = g_botProfiler.Begin();
		g_botBatch.load(std::memory_order_acquire)();
		g_botProfiler.End(BotProfiler::Batch, ticket);
		if (g_botProfiler.Due()) PrintBotProfile();
	}

	void BotMovement(void* commands)
	{
		const auto ticket = g_botProfiler.Begin();
		const auto count = *reinterpret_cast<const int*>(g_base + game::addresses::runtime::allocated_clients);
		const bool collect = g_botMovementWorkers && g_botMovementWorkersSupported && !g_collectBotMovement &&
			!_stricmp(g_gameType, "br") && count > 0 && count <= 200 &&
			*reinterpret_cast<const uint8_t*>(g_base + game::addresses::runtime::client_workers_enabled) &&
			*reinterpret_cast<const unsigned*>(g_base + game::addresses::runtime::client_workers_count) >= static_cast<unsigned>(count) &&
			*reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::client_workers_storage);
		if (collect)
		{
			g_botMovementBatch.Reset();
			g_collectBotMovement = &g_botMovementBatch;
		}
		g_botMovement.load(std::memory_order_acquire)(commands);
		if (collect)
		{
			g_collectBotMovement = nullptr;
			// Keep native eligibility, reliable-command and spectator bookkeeping.
			// Only after that loop, queue the captured movement through Replay's
			// TLS/physics-aware workers. All work finishes in this server frame,
			// before 137DDC0/137C400 execute deferred gameplay tasks at 121E610.
			g_botWorkerCommands += g_botMovementBatch.Execute(
				[](const auto& entry)
				{
					return reinterpret_cast<QueueClientThinkFn>(g_base + kQueueClientThinkRVA)(entry.slot, entry.command.data());
				},
				[](const auto& entry)
				{
					++g_botWorkerFallbacks;
					g_clientThink.load(std::memory_order_acquire)(entry.client, entry.command.data());
				},
				[]
				{
					reinterpret_cast<FrameFn>(g_base + kFinishClientWorkersRVA)();
				});
		}
		g_botProfiler.End(BotProfiler::Movement, ticket);
	}

	uintptr_t ClientThink(uintptr_t client, const void* command)
	{
		if (g_collectBotMovement && client && command && *reinterpret_cast<const uint8_t*>(client + 8) == 5 &&
			*reinterpret_cast<const int*>(client + 214756) == 2)
		{
			const auto entity = *reinterpret_cast<const uintptr_t*>(client + 280);
			const unsigned slot = entity ? *reinterpret_cast<const uint16_t*>(entity) : 200;
			if (slot < 200 &&
				*reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::server_clients + 8ull * slot) == client &&
				g_collectBotMovement->Push(client, slot, command))
				return 0; // 135AF90 ignores return value
		}
		return g_clientThink.load(std::memory_order_acquire)(client, command);
	}

	void BotMovementWorkersCommand()
	{
		char value[16]{};
		ReadSVCmdArg(1, value, sizeof(value));
		if (value[0])
		{
			if (!strcmp(value, "0"))
				g_botMovementWorkers = false;
			else if (!strcmp(value, "1") && g_botMovementWorkersSupported)
				g_botMovementWorkers = true;
			else
			{
				OP_WARN("Usage: bot_movement_workers <0|1> (BR only; native worker guards must pass)");
				return;
			}
		}
		OP_INFO("Bot movement workers | requested=%d supported=%d native=%u allocated=%u | queued=%llu fallback=%llu", g_botMovementWorkers,
			g_botMovementWorkersSupported, *reinterpret_cast<const uint8_t*>(g_base + game::addresses::runtime::client_workers_enabled),
			*reinterpret_cast<const unsigned*>(g_base + game::addresses::runtime::client_workers_count),
			static_cast<unsigned long long>(g_botWorkerCommands), static_cast<unsigned long long>(g_botWorkerFallbacks));
	}

	// Replay's nearest-reachable search has seven arguments; radius is a stack
	// float in the Win64 ABI. Keep its result container and native ownership intact.
	void ReachableSearch(uintptr_t space, void* position, uintptr_t area, uintptr_t spec, float radius, int count, void* results)
	{
		const auto ticket = g_botProfiler.Begin();
		botnav::BeginQuery();
		g_reachableSearch.load(std::memory_order_acquire)(space, position, area, spec, radius, count, results);
		botnav::EndQuery();
		g_botProfiler.End(BotProfiler::ReachableSearch, ticket);
	}

	uintptr_t NavContext(uintptr_t context, uintptr_t graph, unsigned source, uintptr_t filter)
	{
		const auto result = g_navContext.load(std::memory_order_acquire)(context, graph, source, filter);
		botnav::ContextCreated(context, graph, source, filter);
		return result;
	}

	bool NavReachable(uintptr_t context, int target)
	{
		return botnav::Reachable(context, target, g_navReachable.load(std::memory_order_acquire));
	}

	void BotGpuNavCommand()
	{
		char option[24]{};
		ReadSVCmdArg(1, option, sizeof(option));
		botnav::Command(option);
	}

	uintptr_t ScriptGoal(uintptr_t context, uintptr_t entityRef)
	{
		const auto ticket = g_botProfiler.Begin();
		const auto result = g_scriptGoal.load(std::memory_order_acquire)(context, entityRef);
		g_botProfiler.End(BotProfiler::ScriptGoal, ticket);
		return result;
	}

	uintptr_t NavigationProjection(uintptr_t space, void* position, uintptr_t query)
	{
		const auto ticket = g_botProfiler.Begin();
		const auto result = g_navigationProjection.load(std::memory_order_acquire)(space, position, query);
		g_botProfiler.End(BotProfiler::Projection, ticket);
		return result;
	}

	uintptr_t ArchiveCharacter(uintptr_t antiLag, void* entity, uintptr_t frame)
	{
		const auto ticket = g_botProfiler.Begin();
		const auto result = g_archiveCharacter.load(std::memory_order_acquire)(antiLag, entity, frame);
		g_botProfiler.End(BotProfiler::Archive, ticket);
		return result;
	}

	void BotProfileCommand()
	{
		char value[32]{};
		ReadSVCmdArg(1, value, sizeof(value));
		char* end = nullptr;
		const long seconds = value[0] ? std::strtol(value, &end, 10) : 10;
		if ((value[0] && (!end || *end)) || seconds < 0 || seconds > 30)
		{
			OP_WARN("Usage: bot_profile [0..30] (seconds; 0=stop)");
			return;
		}
		PrintBotProfile();
		if (seconds)
		{
			g_botProfiler.Start(static_cast<unsigned>(seconds));
			OP_INFO("Bot profile started for %ld seconds", seconds);
		}
	}

	void BotUpdateCommand(uintptr_t client, void* command)
	{
		const auto original = &ProfiledBotUpdateCommand;
		const auto entity = *reinterpret_cast<const uintptr_t*>(client + 280);
		const unsigned slot = entity ? *reinterpret_cast<const uint16_t*>(entity) : 200;
		const auto server = *reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::server_state);
		if (slot >= g_botCommands.size() || !server ||
			*reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::server_clients + 8ull * slot) != client)
		{
			original(client, command);
			return;
		}
		auto& cached = g_botCommands[slot];
		const int now = *reinterpret_cast<const int*>(server + 28);
		const int interval = BotThinkInterval(entity, now);
		const auto gameClients = *reinterpret_cast<const uintptr_t*>(g_base + game::addresses::runtime::game_clients);
		const auto playerState = gameClients ? gameClients + 64496ull * slot : 0;
		// Let native setup, death, spectator and respawn handling run immediately.
		// No entity/script timers or physics frames are throttled. Only an active
		// living bot can keep its last input between decisions, like held input.
		if (!interval || now < 0 || *reinterpret_cast<const uint8_t*>(client + 8) != 5 ||
			*reinterpret_cast<const int*>(client + 214756) != 2 || !playerState ||
			*reinterpret_cast<const int*>(playerState + 21932) != 0 || *reinterpret_cast<const int*>(entity + 440) <= 0)
		{
			cached.Invalidate();
			original(client, command);
			return;
		}
		const auto generation = g_botScriptGeneration.load(std::memory_order_relaxed);
		if (cached.Reuse(client, generation, now, interval, command))
		{
			++g_botHeldCommands;
			return;
		}
		original(client, command);
		++g_botDecisions;
		cached.Save(client, generation, slot, now, interval, command);
	}

	void BotThinkIntervalCommand()
	{
		char value[32]{};
		ReadSVCmdArg(1, value, sizeof(value));
		if (value[0])
		{
			char* end = nullptr;
			const long number = std::strtol(value, &end, 10);
			if (!_stricmp(value, "auto"))
				g_botThinkOverride = -1;
			else if (end && !*end && (number == 0 || (number >= 25 && number <= 1000)))
				g_botThinkOverride = static_cast<int>(number);
			else
			{
				OP_WARN("Usage: bot_think_interval <auto|0|25..1000> (milliseconds; 0=native)");
				return;
			}
			for (auto& cached : g_botCommands)
				cached.Invalidate();
		}
		OP_INFO("Bot decisions: %s | override=%d ms | movement remains every server frame",
			g_botThinkOverride < 0 ? "auto BR: near100/mid250/far1000; MP:native" : "manual", g_botThinkOverride);
	}

	void BotGoalIntervalCommand()
	{
		char value[32]{};
		ReadSVCmdArg(1, value, sizeof(value));
		if (value[0])
		{
			char* end = nullptr;
			const long number = std::strtol(value, &end, 10);
			if (!end || *end || number < 50 || number > 5000)
			{
				OP_WARN("Usage: bot_goal_interval <50..5000> (milliseconds; 50=native BR roaming)");
				return;
			}
			g_botGoalInterval = static_cast<int>(number);
			if (g_stage == Stage::Running && ServerRunningForTrace() && g_botGoalWait && !ApplyBotGoalInterval())
			{
				g_botGoalWait = 0;
				OP_WARN("BR roaming wait no longer matches; no script bytes changed");
			}
		}
		OP_INFO("BR bot roaming: requested=%d ms | active=%d ms | %s", g_botGoalInterval, g_botGoalApplied,
			g_botGoalWait ? "verified loop" : "pending compatible BR script load");
	}
}
