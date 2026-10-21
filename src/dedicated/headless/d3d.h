#pragma once
#include <cstdint>
namespace headless::d3d
{
	// Called before a newly created Replay device is returned to the engine.
	bool AttachDevice(void* device);
#ifdef IW8X_ISLAND_PORT_PROBE
	void EnableProbeDiagnostics();
#endif
	// Explicitly issue proxy-owned bot compute without reenabling engine rendering.
	bool DispatchCompute(void* list, unsigned x, unsigned y, unsigned z);

	struct Counters
	{
		uint64_t commandLists, draws, dispatches, indirect, clears, rays, queueSubmissions, submittedLists;
	};

	Counters ReadCounters();
}
