#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace lua_injection
{
	hook::Status Install(uintptr_t moduleBase);
	using StartupCallback = void (*)(void* luaState);
	// Called on the loading thread with the stock result on the stack. Callback
	// must preserve that stack and must not execute the startup chunk itself.
	void SetStartupCallback(StartupCallback callback);
	// Register the opt-in original 1.64.5 module compiler on the already
	// validated Replay startup VM, before the replacement startup chunk executes.
	void RegisterMenu1645(void* luaState);
	// The replacement compiled and was returned to the native caller. This does
	// not prove that the caller subsequently executed the chunk successfully.
	bool StartupCompiled();
}
