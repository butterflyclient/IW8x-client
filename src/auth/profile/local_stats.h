#pragma once
#include <cstdint>

#include <windows.h>

#include <string>
namespace local_stats
{
	void Prepare(HMODULE module, uint64_t xuid);
	bool Initialize(uintptr_t base, int source);
	void Pump(); // main thread, after profile readiness; no engine calls on worker threads
	void SaveNow(); // completed UI selection, same validated snapshot
	bool RemoteReady();

	struct SyncView
	{
		bool ready = false, pending = false, conflict = false;
		std::string status;
	};

	SyncView SyncStatus();
	bool ResolveConflict(bool keepLocal);
}
