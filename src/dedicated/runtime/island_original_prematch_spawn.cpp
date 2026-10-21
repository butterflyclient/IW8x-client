#include "dedicated/runtime/island_original_prematch_spawn.hpp"
#include "dedicated/runtime/commands.hpp"
#include "dedicated/runtime/lifecycle.hpp"

#ifdef IW8X_ISLAND_PORT_PROBE
#include "auto_server.h"
#include "dedicated.h"
#include "console.h"
#include "gsc/internal/bindings.hpp"
#include "gsc/internal/players.hpp"
#include "plugin/sdk/abi.h"
#include <array>
#include <atomic>
#include <cstring>
#include <span>
#endif

namespace dedicated::detail
{
#ifdef IW8X_ISLAND_PORT_PROBE
	namespace
	{
		std::atomic<std::uint64_t> spawn_restart_epoch{1};
		std::array<std::atomic<std::uint64_t>,200> spawn_notifications{};
	}
#endif

	void NotifyIslandOriginalPlayerSpawned(std::uint32_t client_num) noexcept
	{
#ifdef IW8X_ISLAND_PORT_PROBE
		if (client_num < spawn_notifications.size())
			spawn_notifications[client_num].fetch_add(1, std::memory_order_release);
#else
		(void)client_num;
#endif
	}

	bool RestoreIslandOriginalSpawnAtNativeEntry(
		const std::uint32_t client_num, const float* original, float* corrected) noexcept
	{
#ifdef IW8X_ISLAND_PORT_PROBE
		if (!original || !corrected || client_num >= 200 ||
			std::strcmp(g_mapName, "mp_sm_island_1") != 0 || std::strcmp(g_gameType, "br") != 0)
			return false;
		char setting[8]{};
		if (GetEnvironmentVariableA("IW8X_ISLAND_RESTORE_164_SPAWN", setting, DWORD(sizeof(setting))) == 1 &&
			setting[0] == '0') return false;
		const island_original_prematch_spawn::position at{original[0], original[1], original[2]};
		if (!island_original_prematch_spawn::invalid_fallback_position(at))
		{
			OP_INFO("Island original 1.64 spawn precommit: client=%u selected=(%.1f %.1f %.1f) valid; native engine spawn preserved",
				unsigned(client_num), at.x, at.y, at.z);
			return false;
		}
		const auto pos = island_original_prematch_spawn::source_spawn(client_num);
		corrected[0] = pos.x;
		corrected[1] = pos.y;
		corrected[2] = pos.z;
		OP_INFO("Island 1.64 GSC spawn origin restored before native ClientSpawn: client=%u old=(%.1f %.1f %.1f) new=(%.1f %.1f %.1f), radius=%u",
			unsigned(client_num), at.x, at.y, at.z, pos.x, pos.y, pos.z,
			unsigned(island_original_prematch_spawn::source_164_locations[client_num % 4].radius));
		return true;
#else
		(void)client_num;
		(void)original;
		(void)corrected;
		return false;
#endif
	}

	void ResetIslandOriginalPrematchSpawn() noexcept
	{
#ifdef IW8X_ISLAND_PORT_PROBE
		spawn_restart_epoch.fetch_add(1, std::memory_order_release);
#endif
	}

	void PumpIslandOriginalPrematchSpawnVm() noexcept
	{
#ifdef IW8X_ISLAND_PORT_PROBE
		// Only the Island BR server GSC may issue this correction. In particular
		// never affect Verdansk/Shipment, frontend map, or another user's client.
		if (std::strcmp(g_mapName, "mp_sm_island_1") || std::strcmp(g_gameType, "br")) return;
		char setting[8]{};
		if (GetEnvironmentVariableA("IW8X_ISLAND_RESTORE_164_SPAWN", setting, DWORD(sizeof(setting))) == 1 &&
			setting[0] == '0') return;

		using namespace iw8x::gsc;
		using namespace iw8x::gsc::internal;
		const auto& bridge = bindings();
		if (!in_game_scope() || !(bridge.capabilities & IW8X_CAP_GSC_CALL) || !bridge.call_builtin)
			return;

		// Player identity is owned by the existing spawn event registry. Never
		// call a VM method through an arbitrary net-client or guessed pointer.
		// Only this VM thread accesses the per-epoch state.
		static std::array<std::uint64_t, 200> checked_generation{};
		static std::array<std::uint64_t, 200> checked_spawn_serial{};
		static std::array<std::uint64_t, 200> observed_spawn_serial{};
		static std::array<std::uint8_t, 200> failed_attempts{};
		static std::uint64_t local_epoch{};
		const auto epoch = spawn_restart_epoch.load(std::memory_order_acquire);
		if (epoch != local_epoch)
		{
			checked_generation.fill(0);
			checked_spawn_serial.fill(0);
			observed_spawn_serial.fill(0);
			failed_attempts.fill(0);
			local_epoch = epoch;
		}
		try
		{
			for (const auto& player : connected_players())
			{
				if (player.client_num >= checked_generation.size()) continue;
				const auto slot = player.client_num;
				const auto serial = spawn_notifications[slot].load(std::memory_order_acquire);
				if (!serial || checked_spawn_serial[slot] == serial) continue;
				if (observed_spawn_serial[slot] != serial)
				{
					observed_spawn_serial[slot] = serial;
					failed_attempts[slot] = 0;
				}
				if (failed_attempts[slot] >= 3) continue;
				const auto snapshot = find_player(slot, false);
				if (!snapshot || !snapshot.entity.generation) continue; // not spawned yet
				value current;
				const auto observed = bridge.call_builtin(true, snapshot.entity, "getorigin", {}, current);
				if (observed == IW8X_BUSY) continue;
				if (observed != IW8X_OK || current.type() != value_type::vector)
				{
					++failed_attempts[slot];
					if (failed_attempts[slot] == 3)
						OP_WARN("Island original 1.64 prematch spawn: failed to read client=%u origin status=%u; no relocation attempted",
							unsigned(slot), unsigned(observed));
					continue;
				}
				const auto origin = current.get<vector>();
				const island_original_prematch_spawn::position at{origin.x, origin.y, origin.z};
				if (!island_original_prematch_spawn::invalid_fallback_position(at))
				{
					checked_generation[slot] = player.generation;
					checked_spawn_serial[slot] = serial;
					OP_INFO("Island 1.64 prematch spawn check: client=%u origin=(%.2f %.2f %.2f) valid-fallback=false; native BR placement retained",
						unsigned(slot), at.x, at.y, at.z);
					continue; // original BR GSC already positioned this player
				}
				const auto target = island_original_prematch_spawn::source_spawn(slot);
				const std::array<value, 1> args{value(vector{target.x, target.y, target.z})};
				value ignored;
				const auto moved = bridge.call_builtin(true, snapshot.entity, "setorigin", args, ignored);
				if (moved == IW8X_BUSY) continue;
				checked_generation[slot] = player.generation; // never loop a failed mutation
				checked_spawn_serial[slot] = serial; // never loop a failed mutation
				if (moved == IW8X_OK)
					OP_INFO("Island original 1.64 prematch spawn restored: client=%u from=(%.2f %.2f %.2f) to=(%.2f %.2f %.2f), source_center=%u offset=12000",
						unsigned(slot), at.x, at.y, at.z, target.x, target.y, target.z,
						unsigned(slot % island_original_prematch_spawn::source_164_locations.size()));
				else
					OP_WARN("Island original 1.64 prematch spawn relocation rejected: client=%u status=%u; no success claimed",
						unsigned(slot), unsigned(moved));
			}
		}
		catch (...)
		{
			OP_WARN("Island original 1.64 prematch spawn: GSC VM rejected diagnostic request");
		}
#endif
	}
}
