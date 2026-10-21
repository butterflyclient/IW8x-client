#pragma once
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <array>
namespace dedicated::detail
{
	using FrameFn = void (*)();

	using ModeFn = void(__fastcall*)(uint8_t, bool);

	using GetModeFn = uint8_t (*)();

	using GetServerFn = void* (*)();

	using DevmapFn = void(__fastcall*)(void*);

	using DevSetupFn = uintptr_t (*)();

	using CommandFn = void (*)();

	using SetTransientModeFn = void(__fastcall*)(int mode);

	// Replay NET_Start copies all eight bytes into NetInit before returning.
	// NetInitFlow is distinct from NetMode: OFFLINE flow=0, OFFLINE mode=2.
	struct NetConfig
	{
		int flow;
		uint8_t relayDisabled;
		uint8_t sendSignalForced;
		uint8_t padding[2];
	};

	static_assert(sizeof(NetConfig) == 8 && offsetof(NetConfig, relayDisabled) == 4);

	using NetStartFn = void(__fastcall*)(const NetConfig*, void* listener);

	using NetCallbackFn = bool(__fastcall*)(bool success, int mode, int* nextMode);

	using NetErrorFn = void(__fastcall*)(int error);

	struct bdSecurityID
	{
		uint8_t ab[8];
	};

	struct XNADDR
	{
		uint8_t addrBuff[84];
	};

	struct bdSecurityKey
	{
		uint8_t ab[16];
	};

	struct XSECURITY_INFO
	{
		bdSecurityID id;
		bdSecurityKey key;
	};

	struct XSESSION_INFO
	{
		XNADDR address;
		XSECURITY_INFO security;
	};

	static_assert(sizeof(XSESSION_INFO) == 0x6C);

	using NETCreateSessionFn = void(__fastcall*)(XSESSION_INFO* outSessionInfo);

	using ConnectionlessPacketFn = void(__fastcall*)(const void* from, void* msg, std::int64_t arg2);

	using AddressOnlyFn = void(__fastcall*)(const void* from);

	using PartyJoinRequestFn = void(__fastcall*)(void* party, const void* mainActiveClient, const void* from, void* msg);

	using PartySendAcceptFn = void(__fastcall*)(void* party, const void* addr, void* challenge, std::uint64_t xuid);

	// Replay has four arguments, including the 8-byte XUID by value in R9.
	// The later IW8 build's payload parameters are absent here (wire payload size is zero).
	using PartySendFailedFn = void(__fastcall*)(void* party, const void* addr, int response, std::uint64_t xuid);

	using SVCmdArgvFn = const char*(__fastcall*)(int index);

	using SVClientMPAddBotFn = void*(__fastcall*)(const char* botName, unsigned head, unsigned body, unsigned helmet);

	using ScriptContextFn = void* (*)();

	using ScrContextOpFn = void (*)(void* context);

	using ScrGetFunctionHandleFn = unsigned (*)(void* context, const char* filename, unsigned function);

	using ScrAddStringFn = void (*)(void* context, const char* value);

	using GScrExecEntThreadFn = unsigned (*)(void* entity, unsigned handle, unsigned argc);

	using ScrFreeThreadFn = void (*)(void* context, unsigned thread);

	using ScrFindEntityIdFn = unsigned (*)(void*, int, uint8_t, unsigned);

	using ScrFindVariableFn = unsigned (*)(void*, unsigned, unsigned);

	using SLFindStringFn = unsigned (*)(const char*);

	using SLConvertToStringFn = const char* (*)(unsigned);

	using GScrNotifyFn = void (*)(void*, unsigned, unsigned);

	struct PendingBotClass
	{
		unsigned object = 0;
		unsigned generation = 0;
		ULONGLONG deadline = 0;
		bool wasWaiting = false;
	};

	using BotUpdateCommandFn = void (*)(uintptr_t client, void* command);

	using NavContextFn = uintptr_t (*)(uintptr_t, uintptr_t, unsigned, uintptr_t);

	using BotBatchFn = void (*)();

	using BotMovementFn = void (*)(void* commands);

	using ReachableSearchFn = void (*)(
		uintptr_t space, void* position, uintptr_t area, uintptr_t spec, float radius, int count, void* results);

	using ScriptGoalFn = uintptr_t (*)(uintptr_t context, uintptr_t entityRef);

	using NavigationProjectionFn = uintptr_t (*)(uintptr_t space, void* position, uintptr_t query);

	using ArchiveCharacterFn = uintptr_t (*)(uintptr_t antiLag, void* entity, uintptr_t frame);

	using ClientThinkFn = uintptr_t (*)(uintptr_t client, const void* command);

	using QueueClientThinkFn = bool (*)(unsigned slot, const void* command);

	using MSGBeginReadingFn = void(__fastcall*)(void* msg);

	using MSGReadLongFn = int(__fastcall*)(void* msg);

	using MSGReadStringLineFn = char*(__fastcall*)(void* msg, char* out, unsigned size);

	// Cmd_AddServerCommandList uses two embedded 0x28-byte command nodes.
	struct CommandNode
	{
		CommandNode* next;
		const char* name;
		uint8_t autocomplete[16];
		CommandFn function;
	};

	struct ServerCommand
	{
		const char* name;
		CommandFn function;
		CommandNode client;
		CommandNode server;
	};

	static_assert(sizeof(CommandNode) == 0x28 && offsetof(CommandNode, function) == 0x20);

	static_assert(sizeof(ServerCommand) == 0x60 && offsetof(ServerCommand, server) == 0x38);

	enum class Stage
	{
		SelectMP,
		WaitNone,
		WaitMP,
		QueueConfig,
		WaitConfig,
		WaitServer,
		Running,
		RestartDelay,
		Failed,
		Idle,
		Stopping
	};

	enum class ArgumentStatus
	{
		Missing,
		Valid,
		Invalid
	};

	struct BotScriptValue
	{
		unsigned value = 0;
		uint8_t type = 0;

		bool IsTrue() const
		{
			return type == 6 && value == 1;
		}
	};

	using LoadoutGroupFn = int (*)();

	using RequiredStatPacketsFn = uint64_t (*)();
}
