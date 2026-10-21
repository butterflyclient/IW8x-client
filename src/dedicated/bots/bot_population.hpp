#pragma once
#include "dedicated/runtime/types.hpp"
namespace dedicated::detail
{
	extern std::atomic<unsigned> g_botScriptGeneration;

	extern std::atomic<bool> g_hasPendingBotClasses;

	extern std::atomic<bool> g_botClassCommandQueued;

	extern std::atomic<unsigned> g_pendingBotAdds;

	extern uintptr_t g_botGoalWait;

	extern int g_botGoalInterval, g_botGoalApplied;

	extern ULONGLONG g_nextBotClassCheck;

	bool ApplyBotGoalInterval();

	void ResolveBotScripts(void* context);

	unsigned AvailableBotSlots();

	bool AddOneBot(const char* requestedName, bool announce = true);

	BotScriptValue BotScriptField(void* context, unsigned object, unsigned canonicalName);

	void CompleteBotClassCommands();

	void AddBotCommand();

	void AddBotsCommand();

	bool ActiveBot(unsigned slot);

	bool RemoveOneBot(unsigned slot);

	void BotStatusCommand();

	void RemoveBotCommand();

	void RemoveBotsCommand();
}
