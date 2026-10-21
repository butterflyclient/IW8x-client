#pragma once
#include "engine/inline_hook.h"

#include <cstdint>
namespace bap
{
	bool Enabled();
	bool HandlesType(unsigned type);
	bool RequiredMap(const char* mapName);
	bool Ready();
	bool ShouldUseFullCollision(const char* mapName);

	// Must run from process attach, before Replay allocates its native XAsset pools.
	// Reference startup raises physicsasset to 64000 and, in BAP mode,
	// xmodeldetailcollision to 96000 before DB_InitXAssetPools.
	bool PrepareNativePools(uintptr_t moduleBase, bool nightRequested = false);

	// Process-lifetime, first-chance observer; must be armed before enabling BAP hooks.
	bool InitializeCrashDiagnostics(uintptr_t moduleBase);
	void TraceCollisionMode(uintptr_t caller, int requestedMode, int effectiveMode, bool completed);

	// Core eight DB_AssetEntryPool/Table detours. Transparent for all non-type-101 assets.
	hook::Status InstallCore(uintptr_t moduleBase);
	// Downstream DB Glob/lifecycle routing. This remains not-ready until every required
	// Replay seam has been structurally mapped; it never guesses offsets.
	hook::Status InstallLifecycle(uintptr_t moduleBase);
	// MODE_ALL backed by the registered numbered collision tiles, not the ls fastfile.
	hook::Status InstallCollision(uintptr_t moduleBase);
	// Client ports: mp_kstenod/mp_escape2, each admitted only after bundle verification. Does not
	// enable the dedicated BAP asset-pool or lifecycle hooks.
	hook::Status InstallNightCollision(uintptr_t moduleBase);
	void SetLifecycleReady(bool ready);
}
