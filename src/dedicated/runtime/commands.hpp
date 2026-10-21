#pragma once
#include <string_view>

#include "dedicated/runtime/types.hpp"
namespace dedicated::detail
{
	bool CommandEquals(std::string_view value, std::string_view expected);

	void PrintOperatorStatus();

	bool HandleConsoleCommand(std::string_view command);
	void SetMapRestartReady(bool ready) noexcept;
	void PumpMapRestartVm() noexcept;
	void PumpIslandNativeCrateProbeVm() noexcept;
	void ResetIslandOriginalPrematchSpawn() noexcept;
	void NotifyIslandOriginalPlayerSpawned(uint32_t client_num) noexcept;
	void PumpIslandOriginalPrematchSpawnVm() noexcept;
	bool RestoreIslandOriginalSpawnAtNativeEntry(uint32_t client_num, const float* original, float* corrected) noexcept;
	void PumpIslandNativeCacheAuditVm() noexcept;

	void NoclipDedicatedCommand();

	bool CommandsAvailable(uintptr_t headRva);

	bool RegisterCommands();
}
