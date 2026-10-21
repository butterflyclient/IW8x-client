#pragma once
#include "dedicated/bots/bot_command_cache.h"
#include "dedicated/bots/bot_profiler.h"
#include "dedicated/bots/bot_nav_gpu.h"

#include "engine/addresses.hpp"

#include "dedicated/runtime/types.hpp"
namespace dedicated::detail
{
	inline constexpr uintptr_t kBotUpdateCommandRVA = game::addresses::runtime::bot_movement_bot_update_command;

	inline constexpr uint8_t kBotUpdateCommandBytes[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x58,
		0x48, 0x8B, 0xF2 };

	extern std::atomic<BotUpdateCommandFn> g_botUpdateCommand;

	extern std::atomic<NavContextFn> g_navContext;

	extern std::atomic<botnav::NativeReachable> g_navReachable;

	inline constexpr uint8_t kNavContextBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24,
		0x20, 0x41, 0x56 };

	inline constexpr uint8_t kNavReachableBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B, 0x49, 0x10,
		0x48, 0x8B, 0xD9 };

	extern std::atomic<BotBatchFn> g_botBatch;

	extern std::atomic<BotMovementFn> g_botMovement;

	extern std::atomic<ClientThinkFn> g_clientThink;

	inline constexpr uintptr_t kClientThinkRVA = game::addresses::runtime::bot_movement_client_think,
							   kQueueClientThinkRVA = game::addresses::runtime::bot_movement_queue_client_think,
							   kFinishClientWorkersRVA = game::addresses::runtime::bot_movement_finish_client_workers;

	inline constexpr uint8_t kClientThinkBytes[] = { 0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF2, 0x4C, 0x8D, 0x41, 0x10, 0x4C,
		0x8B, 0xD1 };

	inline constexpr uint8_t kQueueClientThinkBytes[] = { 0x40, 0x53, 0x55, 0x48, 0x83, 0xEC, 0x28, 0x80, 0x3D, 0x42, 0x80, 0x2E, 0x0C,
		0x00 };

	inline constexpr uint8_t kFinishClientWorkersBytes[] = { 0x48, 0x83, 0xEC, 0x28, 0xB9, 0x80, 0x00, 0x00, 0x00, 0xE8, 0x42, 0xA6, 0xFE,
		0xFF };

	extern bool g_botMovementWorkersSupported;

	extern std::atomic<ReachableSearchFn> g_reachableSearch;

	extern std::atomic<ScriptGoalFn> g_scriptGoal;

	extern std::atomic<NavigationProjectionFn> g_navigationProjection;

	extern std::atomic<ArchiveCharacterFn> g_archiveCharacter;

	inline constexpr uint8_t kBotBatchBytes[] = { 0xB8, 0x78, 0xCE, 0x00, 0x00, 0xE8, 0x16, 0x21, 0xE7, 0x00, 0x48, 0x2B, 0xE0 };

	inline constexpr uint8_t kBotMovementBytes[] = { 0x40, 0x55, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x33, 0xFF, 0x48, 0x8B, 0xE9 };

	inline constexpr uint8_t kReachableSearchBytes[] = { 0x40, 0x55, 0x53, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xB0, 0x6C, 0xFF, 0xFF, 0xB8,
		0x50, 0x94, 0x00, 0x00 };

	inline constexpr uint8_t kScriptGoalBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC,
		0x60 };

	inline constexpr uint8_t kProjectionBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x4C, 0x89, 0x64, 0x24,
		0x20, 0x55, 0x41, 0x56, 0x41, 0x57 };

	inline constexpr uint8_t kArchiveBytes[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48,
		0x83, 0xEC, 0x30 };

	extern std::array<BotCommandCache, 200> g_botCommands;

	extern int g_botThinkOverride;

	extern std::uint64_t g_botDecisions, g_botHeldCommands;

	int BotThinkInterval(uintptr_t entity, int now);

	void ProfiledBotUpdateCommand(uintptr_t client, void* command);

	void PrintBotProfile();

	void BotBatch();

	void BotMovement(void* commands);

	uintptr_t ClientThink(uintptr_t client, const void* command);

	void BotMovementWorkersCommand();

	// Replay's nearest-reachable search has seven arguments; radius is a stack
	// float in the Win64 ABI. Keep its result container and native ownership intact.
	void ReachableSearch(uintptr_t space, void* position, uintptr_t area, uintptr_t spec, float radius, int count, void* results);

	uintptr_t NavContext(uintptr_t context, uintptr_t graph, unsigned source, uintptr_t filter);

	bool NavReachable(uintptr_t context, int target);

	void BotGpuNavCommand();

	uintptr_t ScriptGoal(uintptr_t context, uintptr_t entityRef);

	uintptr_t NavigationProjection(uintptr_t space, void* position, uintptr_t query);

	uintptr_t ArchiveCharacter(uintptr_t antiLag, void* entity, uintptr_t frame);

	void BotProfileCommand();

	void BotUpdateCommand(uintptr_t client, void* command);

	void BotThinkIntervalCommand();

	void BotGoalIntervalCommand();
}
