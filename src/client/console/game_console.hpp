#pragma once

#include "engine/inline_hook.h"
#include <string_view>

namespace game_console
{
	using submit_command = bool (*)(std::string_view);
	hook::Status install(uintptr_t base, submit_command submit);
	void shutdown();
	void draw();
	// True consumes the event. Native key-up bookkeeping stays with the caller.
	bool key_event(int key, bool down, int virtual_key, bool shift);
	void print(std::string_view text);
	void print_engine(const char* text, int flags);
}
