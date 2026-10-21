#pragma once
#include "engine/inline_hook.h"

#include <cstddef>
namespace joindiag
{
	hook::Status Install(uintptr_t base);
	void BeginAttempt(const char* path);
	void Pump(); // Main thread only.
	void Snapshot(const char* event, int controller = 0);
	void ResponseName(uintptr_t base, int response, char* out, size_t size);
	void FormatAddress(const void* address, char* out, size_t size);
	unsigned TransportFailureSequence(); // native PartyJoin failure observer; no engine calls
}
