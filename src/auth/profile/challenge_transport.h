#pragma once
#include "engine/inline_hook.h"

#include "auth/profile/challenge_events.h"
namespace challenge_transport
{
	using Receiver = void (*)(const challenges::Event&);
	hook::Status Install(uintptr_t base, bool dedicated, Receiver receiver);
	void BeginReport(uint64_t entityRef);
	void EndReport();
}
