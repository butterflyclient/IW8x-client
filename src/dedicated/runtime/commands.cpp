#include "asset_mark_guard.h"
#include "auto_server.h"
#include "dedicated.h"

#include "window.h"

#include "bap.h"
#include "bot_command_cache.h"
#include "bot_goal_wait.h"
#include "bot_profiler.h"
#include "bot_movement_batch.h"
#include "bot_nav_gpu.h"
#include "commands.h"
#include "console.h"

#include "game.h"

#include "join.h"
#include "noclip.h"
#include "online_services.h"
#include "playlists.h"

#include "safemem.h"

#include "thirdparty/minhook/include/MinHook.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <string_view>

#include <intrin.h>

#include "dedicated/runtime/internal.hpp"
#include "network/backend/native_matchmaking.h"
#include "dedicated/assets/infil_transients.h"
#include "auth/profile/local_progression.h"
#include "dedicated/runtime/map_restart_request.hpp"
#include "dedicated/runtime/island_seed_bounds.hpp"
#include "dedicated/runtime/island_seed_capacity.hpp"
#include "gsc/internal/bindings.hpp"
#include "gsc/internal/runtime.hpp"
#include "plugin/sdk/abi.h"
namespace dedicated::detail
{
	namespace
	{
		MapRestartRequest g_mapRestart;
			// A read-only-until-command, explicitly opt-in isolated native
			// ScriptableSv probe for one exact original Island cache placement.
			// It is not an implementation of all 866 world crates or pickup.
			std::atomic<bool> g_islandCrateProbeQueued{false};
			std::atomic<bool> g_islandCrateProbeReady{false};
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
			struct IslandOriginalCachePoint
			{
				uint32_t source_index, zone_index;
				float x,y,z,pitch,yaw,roll;
			};
			// All 866 original type3 SpawnGroup placements, exact source bytes,
			// generated with the SHA-pinned Island MapEnts geometry audit.
			// Loot selection is INDEPENDENT: Replay 1.20.4 native StringTables.
			static constexpr std::array<IslandOriginalCachePoint,866> islandCachePoints{{
#include "diagnostics/island/island_original_866_crate_points.inc"
			}};
			static_assert(islandCachePoints[0].source_index==2676 &&
				islandCachePoints.back().source_index==3541);
				struct IslandOriginalWorldPoint
				{
					uint32_t source_index,zone_index,cluster_index,source_flags;
					float x,y,z,pitch,yaw,roll;
				};
				// Every source type2 world-loot point, with its exact source
				// geometry, indexed zone, cluster and flags. Item selection is
				// exclusively the genuine Replay 1.20.4 native loot function.
				static constexpr std::array<IslandOriginalWorldPoint,2676> islandWorldPoints{{
#include "diagnostics/island/island_original_2676_world_points.inc"
				}};
				static_assert(islandWorldPoints[0].source_index==0 &&
					islandWorldPoints.back().source_index==2675 &&
					islandWorldPoints[0].zone_index==4 && islandWorldPoints[0].cluster_index==0);
				// Exactly one native loot draw then one native ScriptableSv spawn
				// per source point. Run only from the verified game/VM thread and
				// require a distinct opt-in from single-world-item diagnostics.
					// 0=idle, 1=publishing, 2=draw, 3=spawn, 4=paused at
					// genuine Replay native ScriptableSv reserved-slot budget.
				std::atomic<uint32_t> g_islandWorldSeedPhase{0};
				uint32_t g_islandWorldSeedTarget{},g_islandWorldSeedPosition{},g_islandWorldSeedCreated{};
				std::string g_islandWorldSeedItem;
				std::array<iw8x::gsc::internal::native_scriptable_spawn,2676> g_islandWorldInstances{};
			// 0=idle, 1=publisher initializing, 2=queued/consumable. The
			// operator may publish from the console thread and the server's
			// game/VM thread must never read half-written batch counters.
			std::atomic<uint32_t> g_islandSeedPhase{0};
			uint32_t g_islandSeedTarget{},g_islandSeedPosition{},g_islandSeedCreated{};
			// Retain engine Scriptable class-8 references only for read-only
			// native GSC inspection in the SAME map/script-generation epoch.
			// These are never published as plugin class-0 entities.
			std::array<iw8x::gsc::internal::native_scriptable_spawn,866> g_islandCacheInstances{};
			// 0=idle, 1=operator publishing indices, 2=VM may consume.
			std::atomic<uint32_t> g_islandCacheAuditPhase{0};
			uint32_t g_islandCacheAuditPosition{},g_islandCacheAuditMethod{};
				// Separate read-only diagnostic of the *actual* Replay-native
				// part and current state, never an invented/opened crate state.
				std::atomic<uint32_t> g_islandPartAuditPhase{0};
				uint32_t g_islandPartAuditPosition{},g_islandPartAuditStep{};
				std::array<std::string,4> g_islandPartNames{};
					// Explicit operator-driven, single-cache native opening test.
					// 0=idle, 1=queued, 2=pre-state read, 3=state-exists read,
					// 4=engine native transition, 5=post-state read. No fake loot.
					std::atomic<uint32_t> g_islandOpeningProbePhase{0};
					std::atomic<bool> g_islandNativeLootDrawQueued{false};
					// Test genuine native Replay1.20 loot selection + ScriptableSv
					// item placement at original Island type-2 SpawnGroup index 0.
					// 0 idle, 1 native table draw, 2 native ScriptableDef spawn,
					// 3 read-only ScriptableSv part count validation.
					std::atomic<uint32_t> g_islandWorldPickupPhase{0};
					std::string g_islandWorldPickupName{};
					iw8x::gsc::internal::native_scriptable_spawn g_islandWorldPickup{};
			static constexpr std::array<uint32_t,4> islandAuditPositions{0,1,432,865};
			static constexpr std::array<std::string_view,3> islandAuditMethods{
				"getscriptableisloot", "getscriptablepartcount", "getscriptableisusableonanypart"
			};
#endif

			bool IslandCrateProbePermitted() noexcept
			{
#ifdef IW8X_ISLAND_PORT_PROBE
				char enabled[8]{};
				return g_islandCrateProbeReady.load(std::memory_order_acquire) &&
					std::strcmp(g_mapName, "mp_sm_island_1") == 0 &&
					std::strcmp(g_gameType, "br") == 0 &&
					GetEnvironmentVariableA("IW8X_ISLAND_TEST_REAL_CRATE", enabled, DWORD(std::size(enabled))) == 1 &&
					enabled[0] == '1';
#else
				return false;
#endif
			}
	}

	void SetMapRestartReady(bool ready) noexcept
	{
		g_mapRestart.SetReady(ready);
			if (!ready) ResetIslandOriginalPrematchSpawn();
			g_islandCrateProbeReady.store(ready, std::memory_order_release);
			if (!ready) g_islandCrateProbeQueued.store(false, std::memory_order_release);
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
			if (!ready)
			{
				g_islandSeedPhase.store(0,std::memory_order_release);
				g_islandSeedTarget=g_islandSeedPosition=g_islandSeedCreated=0;
					g_islandCacheAuditPhase.store(0,std::memory_order_release);
					g_islandCacheAuditPosition=g_islandCacheAuditMethod=0;
					g_islandCacheInstances={};
					g_islandWorldSeedPhase.store(0,std::memory_order_release);
					g_islandWorldSeedTarget=g_islandWorldSeedPosition=g_islandWorldSeedCreated=0;
					g_islandWorldSeedItem.clear();
					g_islandWorldInstances={};
					g_islandPartAuditPhase.store(0,std::memory_order_release);
					g_islandPartAuditPosition=g_islandPartAuditStep=0;
					g_islandPartNames={};
						g_islandOpeningProbePhase.store(0,std::memory_order_release);
						g_islandNativeLootDrawQueued.store(false,std::memory_order_release);
						g_islandWorldPickupPhase.store(0,std::memory_order_release);
						g_islandWorldPickupName.clear();
						g_islandWorldPickup={};
			}
#endif
	}

		void PumpIslandNativeCrateProbeVm() noexcept
		{
				const bool singular=g_islandCrateProbeQueued.load(std::memory_order_acquire);
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
				const bool batching=g_islandSeedPhase.load(std::memory_order_acquire)==2;
				if (!singular && !batching) return;
#else
				if (!singular) return;
#endif
			// These calls must run inside the same verified server-VM boundary
			// as map_restart, never on the operator input thread.
			if (!IslandCrateProbePermitted())
			{
				g_islandCrateProbeQueued.store(false, std::memory_order_release);
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
					g_islandSeedPhase.store(0,std::memory_order_release);
#endif
				OP_WARN("Island native crate probe unavailable after server transition");
				return;
			}
			const auto& binding = iw8x::gsc::internal::bindings();
			if (!iw8x::gsc::internal::in_game_scope() ||
				!(binding.capabilities & IW8X_CAP_GSC_CALL) || !binding.call_builtin)
			{
				return; // Wait only until a verified in-game VM call boundary.
			}
				try
			{
					// This runtime owns only the authentic Replay1.20 ScriptableDef,
					// never any original 1.64 loot-table data. Each placement comes
					// from the separately authenticated original Island MapEnts.
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
						if (batching && !island_seed_bounds::valid(
							g_islandSeedPosition, g_islandSeedTarget, islandCachePoints.size()))
					{
						g_islandSeedPhase.store(0,std::memory_order_release);
							OP_WARN("Island native cache batch stopped at invalid or completed source index=%u target=%u capacity=%u; no invalid engine pointer accessed",
								g_islandSeedPosition, g_islandSeedTarget, unsigned(islandCachePoints.size()));
						return;
					}
						const auto& p=islandCachePoints[batching ? g_islandSeedPosition : 0];
					const uint32_t source_index=p.source_index;
					const auto origin=iw8x::gsc::vector{p.x,p.y,p.z};
					const auto angles=iw8x::gsc::vector{p.pitch,p.yaw,p.roll};
#else
					const uint32_t source_index=2676;
					const auto origin=iw8x::gsc::vector{10417.0f,4984.0f,113.0f};
					const auto angles=iw8x::gsc::vector{356.0f,193.2f,12.8f};
#endif
				const std::array<iw8x::gsc::value, 3> args{
					iw8x::gsc::value("br_loot_cache"),
						iw8x::gsc::value(origin),
						iw8x::gsc::value(angles)
				};
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
						// Cache Scriptables consume the SAME native reserved pool as
						// world items and player-driven Scriptables. Guard both the
						// single original cache test and every source batch position.
						iw8x::gsc::value available;
						const auto capacity_status=binding.call_builtin(
							false,{},"getscriptablereservedremaining",{},available);
						if (capacity_status==IW8X_BUSY) return;
						if (capacity_status!=IW8X_OK || available.type()!=iw8x::gsc::value_type::integer)
							throw std::runtime_error("Replay native cache reserved-capacity ABI invalid");
						const auto remaining=available.get<int32_t>();
						if (island_seed_capacity::paused(remaining))
						{
							if (batching)
							{
								g_islandSeedPhase.store(4,std::memory_order_release);
								OP_INFO("Island Replay1.20 native cache placement paused at genuine engine reserved-scriptable capacity: created=%u/%u next_original_cache=%u remaining=%d gameplay_headroom=%d; original remaining source placements retained; resume island_crate_seed after pickups",
									g_islandSeedCreated,g_islandSeedTarget,source_index,remaining,
									island_seed_capacity::player_gameplay_headroom);
							}
							else
							{
								g_islandCrateProbeQueued.store(false,std::memory_order_release);
								OP_WARN("Island single native cache test rejected: engine remaining reserved Scriptables=%d, gameplay headroom=%d",
									remaining,island_seed_capacity::player_gameplay_headroom);
							}
							return;
						}
					iw8x::gsc::internal::native_scriptable_spawn result{};
					const auto status=iw8x::gsc::internal::spawn_island_native_scriptable(args,result);
#else
				iw8x::gsc::value result;
				const auto status = binding.call_builtin(false, {}, "spawnscriptable", args, result);
#endif
				if (status == IW8X_BUSY) return;
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
					if (status==IW8X_OK && result.entity_class==8)
					{
						if (!batching)
						{
							g_islandCrateProbeQueued.store(false,std::memory_order_release);
							OP_INFO("Island native Replay1.20 real ScriptableSv class=%u instance=%u source_cache=%u; opening/inventory NOT VERIFIED",
								unsigned(result.entity_class),result.instance_index,source_index);
						}
						else
						{
								g_islandCacheInstances[g_islandSeedPosition]=result;
							++g_islandSeedCreated;
							++g_islandSeedPosition;
							if (g_islandSeedCreated<=5 || g_islandSeedPosition==g_islandSeedTarget ||
								g_islandSeedCreated%32==0)
								OP_INFO("Island Replay1.20 verified native cache instance=%u original_index=%u zone=%u batch=%u/%u",
									result.instance_index,source_index,p.zone_index,g_islandSeedCreated,g_islandSeedTarget);
							if (g_islandSeedPosition==g_islandSeedTarget)
							{
									g_islandSeedPhase.store(0,std::memory_order_release);
								OP_INFO("Island Replay1.20 native cache batch completed=%u/%u; opening/loot/inventory NOT VERIFIED",
									g_islandSeedCreated,g_islandSeedTarget);
							}
						}
					}
					else
					{
						g_islandCrateProbeQueued.store(false,std::memory_order_release);
							g_islandSeedPhase.store(0,std::memory_order_release);
						OP_WARN("Island Replay1.20 native cache ABORT original_index=%u status=%u native_class=%u count=%u; no later caches spawned",
							source_index,unsigned(status),unsigned(result.entity_class),g_islandSeedCreated);
					}
#else
					g_islandCrateProbeQueued.store(false,std::memory_order_release);
					if(status==IW8X_OK)
						OP_INFO("Island genuine Replay 1.20 br_loot_cache spawnscriptable result: status=%u result_type=%u source_point=%u; opening and inventory UNVERIFIED",
							unsigned(status),unsigned(result.type()),source_index);
					else OP_WARN("Island native br_loot_cache spawnscriptable returned status=%u; source_point=%u; no crate claimed",unsigned(status),source_index);
#endif
			}
			catch (...)
			{
				g_islandCrateProbeQueued.store(false, std::memory_order_release);
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
						g_islandSeedPhase.store(0,std::memory_order_release);
#endif
				OP_WARN("Island native spawnscriptable VM invocation failed; no crate claimed");
			}
			}

		// Only Replay's native read-only ScriptableSv methods are called.
		// A return value is not proof of crate opening, usable pickup
		// callbacks, or inventory acquisition; these need client tests.
		void PumpIslandNativeCacheAuditVm() noexcept
		{
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
						if (const auto world_phase=g_islandWorldSeedPhase.load(std::memory_order_acquire);
							world_phase==2 || world_phase==3)
						{
							if (!IslandCrateProbePermitted() || !iw8x::gsc::internal::in_game_scope() ||
								g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
								g_islandOpeningProbePhase.load(std::memory_order_acquire)!=0)
							{
								g_islandWorldSeedPhase.store(0,std::memory_order_release);
								OP_WARN("Island original world loot generation canceled after map/server epoch transition");
								return;
							}
								if (!island_seed_bounds::valid(g_islandWorldSeedPosition,
									g_islandWorldSeedTarget, islandWorldPoints.size()))
							{
								g_islandWorldSeedPhase.store(0,std::memory_order_release);
								return;
							}
							const auto& point=islandWorldPoints[g_islandWorldSeedPosition];
							try
							{
								if (world_phase==2)
								{
										// Respect the *current engine-owned* ScriptableSv
										// reserved pool. 1376 slots were exhausted by a
										// 2676-candidate world-loot stress test, causing
										// ScriptableSv_LinkReservedScriptable Com_Error.
										// Query Replay's real native getter before EACH
										// draw, retaining source cursor and remaining points.
										iw8x::gsc::value available;
										const auto capacity_status=iw8x::gsc::internal::bindings().call_builtin(
											false,{},"getscriptablereservedremaining",{},available);
										if (capacity_status==IW8X_BUSY) return;
										if (capacity_status!=IW8X_OK ||
											available.type()!=iw8x::gsc::value_type::integer)
											throw std::runtime_error("Replay native ScriptableSv remaining-capacity ABI invalid");
										const auto remaining=available.get<int32_t>();
										if (island_seed_capacity::paused(remaining))
										{
											g_islandWorldSeedPhase.store(4,std::memory_order_release);
											OP_INFO("Island Replay1.20 world placement paused for authentic ScriptableSv resource ownership: created=%u/%u original_point=%u engine_remaining=%d reserved_gameplay_headroom=%d; all remaining original candidates retained; resume with island_world_seed after actual pickups reclaim slots",
												g_islandWorldSeedCreated,g_islandWorldSeedTarget,point.source_index,
												remaining,island_seed_capacity::player_gameplay_headroom);
											return;
										}
									iw8x::gsc::value drawn;
									const auto status=iw8x::gsc::internal::bindings().call_builtin(
										false,{},"pickscriptablelootitem",{},drawn);
									if (status==IW8X_BUSY) return;
									if (status!=IW8X_OK || drawn.type()!=iw8x::gsc::value_type::string)
										throw std::runtime_error("Replay1.20 world loot draw did not return native ScriptableDef name");
									const auto& name=drawn.get<std::string>();
									if (name.size()<=7 || name.size()>100 || name.rfind("brloot_",0)!=0 ||
										!std::all_of(name.begin(),name.end(),[](unsigned char c)
										{
											return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';
										}))
										throw std::runtime_error("Native Replay1.20 world loot item outside authenticated brloot_* namespace");
									g_islandWorldSeedItem=name;
									g_islandWorldSeedPhase.store(3,std::memory_order_release);
									return;
								}
					// The engine may consume native Scriptables *between* the
					// preceding draw frame and this actual spawn frame. Repeat
					// the native preflight without redrawing the authentic item.
					// This closes the allocation-time quota race while retaining
					// the original point, zone, cluster and drawn 1.20.4 item.
					iw8x::gsc::value available_before_spawn;
					const auto pre_spawn_status=iw8x::gsc::internal::bindings().call_builtin(
						false,{},"getscriptablereservedremaining",{},available_before_spawn);
					if (pre_spawn_status==IW8X_BUSY) return;
					if (pre_spawn_status!=IW8X_OK ||
						available_before_spawn.type()!=iw8x::gsc::value_type::integer)
						throw std::runtime_error("Replay native pre-spawn reserved-capacity ABI invalid");
					const auto remaining_before_spawn=available_before_spawn.get<int32_t>();
					if (island_seed_capacity::paused(remaining_before_spawn))
					{
						g_islandWorldSeedPhase.store(4,std::memory_order_release);
						OP_INFO("Island Replay1.20 world placement safely paused after native loot draw: created=%u/%u source_point=%u drawn_item=%s engine_remaining=%d headroom=%d; retaining exact drawn item for resumed spawn",
							g_islandWorldSeedCreated,g_islandWorldSeedTarget,point.source_index,
							g_islandWorldSeedItem.c_str(),remaining_before_spawn,
							island_seed_capacity::player_gameplay_headroom);
						return;
					}
					const std::array<iw8x::gsc::value,3> args{
									iw8x::gsc::value(g_islandWorldSeedItem),
									iw8x::gsc::value(iw8x::gsc::vector{point.x,point.y,point.z}),
									iw8x::gsc::value(iw8x::gsc::vector{point.pitch,point.yaw,point.roll})};
								iw8x::gsc::internal::native_scriptable_spawn item{};
								const auto status=iw8x::gsc::internal::spawn_island_native_scriptable(args,item);
								if (status==IW8X_BUSY) return;
								if (status!=IW8X_OK || item.entity_class!=8)
									throw std::runtime_error("Replay1.20 native world pickup ScriptableSv rejected");
								g_islandWorldInstances[g_islandWorldSeedPosition]=item;
								++g_islandWorldSeedCreated;
								++g_islandWorldSeedPosition;
								if (g_islandWorldSeedCreated<=5 || g_islandWorldSeedCreated%128==0 ||
									g_islandWorldSeedCreated==g_islandWorldSeedTarget)
									OP_INFO("Island original Replay1.20 native world loot created=%u/%u original_point=%u zone=%u cluster=%u item=%s scriptable_instance=%u",
										g_islandWorldSeedCreated,g_islandWorldSeedTarget,point.source_index,point.zone_index,
										point.cluster_index,g_islandWorldSeedItem.c_str(),item.instance_index);
								g_islandWorldSeedItem.clear();
								g_islandWorldSeedPhase.store(
									g_islandWorldSeedCreated==g_islandWorldSeedTarget ? 0u:2u,
									std::memory_order_release);
								if (g_islandWorldSeedCreated==g_islandWorldSeedTarget)
									OP_INFO("Island source-authenticated world loot native generation completed=%u/%u; actual player pickup, inventory mutation and BR match lifecycle remain UNVERIFIED",
										g_islandWorldSeedCreated,g_islandWorldSeedTarget);
							}
							catch (const std::exception& e)
							{
								g_islandWorldSeedPhase.store(0,std::memory_order_release);
								OP_WARN("Island Replay1.20 source world native generation ABORT original_point=%u created=%u reason=%s; preserving engine-owned scriptables",
									point.source_index,g_islandWorldSeedCreated,e.what());
							}
							return;
						}
					if (const auto pickup_phase=g_islandWorldPickupPhase.load(std::memory_order_acquire);pickup_phase!=0)
					{
						if (!IslandCrateProbePermitted() || !iw8x::gsc::internal::in_game_scope() ||
							g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
							g_islandOpeningProbePhase.load(std::memory_order_acquire)!=0)
						{
							g_islandWorldPickupPhase.store(0,std::memory_order_release);
							OP_WARN("Island genuine Replay1.20 world item probe canceled after map/server epoch transition");
							return;
						}
						try
						{
							iw8x_status status=IW8X_BUSY;
							if (pickup_phase==1)
							{
								iw8x::gsc::value drawn;
								status=iw8x::gsc::internal::bindings().call_builtin(false,{},"pickscriptablelootitem",{},drawn);
								if (status==IW8X_BUSY) return;
								if (status!=IW8X_OK || drawn.type()!=iw8x::gsc::value_type::string)
									throw std::runtime_error("Replay1.20 native loot selection did not return a ScriptableDef name");
								const auto& name=drawn.get<std::string>();
								if (name.size()<=7 || name.size()>100 || name.rfind("brloot_",0)!=0 ||
									!std::all_of(name.begin(),name.end(),[](unsigned char c)
									{
										return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';
									}))
									throw std::runtime_error("Replay1.20 loot selection name outside native brloot_* ScriptableDef namespace");
								g_islandWorldPickupName=name;
								g_islandWorldPickupPhase.store(2,std::memory_order_release);
								OP_INFO("Island original type2 world point0 native Replay1.20 table selected=%s; attempting genuine ScriptableSv item spawn next frame",
									name.c_str());
								return;
							}
							if (pickup_phase==2)
							{
								// First authenticated original Island world-loot SpawnGroup
								// point. No modification to MapEnts, exact original angles.
								const std::array<iw8x::gsc::value,3> args{
									iw8x::gsc::value(g_islandWorldPickupName),
									iw8x::gsc::value(iw8x::gsc::vector{-8319.0f,-2676.0f,535.0f}),
									iw8x::gsc::value(iw8x::gsc::vector{0.0f,179.90000915527344f,0.0f})};
								status=iw8x::gsc::internal::spawn_island_native_scriptable(args,g_islandWorldPickup);
								if (status==IW8X_BUSY) return;
								if (status!=IW8X_OK || g_islandWorldPickup.entity_class!=8)
									throw std::runtime_error("Replay1.20 world pickup native ScriptableSv spawn rejected or wrong entity class");
								g_islandWorldPickupPhase.store(3,std::memory_order_release);
								OP_INFO("Island genuine Replay1.20 world pickup ScriptableSv placed source_index=0 item=%s native_instance=%u native_class=%u; use/inventory NOT VERIFIED",
									g_islandWorldPickupName.c_str(),g_islandWorldPickup.instance_index,g_islandWorldPickup.entity_class);
								return;
							}
							if (pickup_phase==3)
							{
								iw8x::gsc::value result;
								status=iw8x::gsc::internal::query_island_native_scriptable(
									g_islandWorldPickup,"getscriptablepartcount",result);
								if (status==IW8X_BUSY) return;
								g_islandWorldPickupPhase.store(0,std::memory_order_release);
								OP_INFO("Island native Replay1.20 world item ScriptableSv part audit item=%s instance=%u status=%u result_type=%u; opening, pickup callback and player inventory NOT VERIFIED",
									g_islandWorldPickupName.c_str(),g_islandWorldPickup.instance_index,
									unsigned(status),unsigned(result.type()));
								return;
							}
						}
						catch (const std::exception& e)
						{
							g_islandWorldPickupPhase.store(0,std::memory_order_release);
							OP_WARN("Island native Replay1.20 world loot ScriptableSv probe aborted: %s; no inventory claimed",e.what());
						}
						return;
					}
				if (g_islandNativeLootDrawQueued.load(std::memory_order_acquire))
				{
					if (!IslandCrateProbePermitted() || !iw8x::gsc::internal::in_game_scope() ||
						g_islandSeedCreated!=islandCachePoints.size() ||
						g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
						g_islandOpeningProbePhase.load(std::memory_order_acquire)!=0)
					{
						g_islandNativeLootDrawQueued.store(false,std::memory_order_release);
						OP_WARN("Island native Replay1.20 loot draw canceled: cache or VM not ready");
						return;
					}
					try
					{
						const auto& binding=iw8x::gsc::internal::bindings();
						iw8x::gsc::value result;
						const auto status=binding.call_builtin(false,{},"pickscriptablelootitem",{},result);
						if (status==IW8X_BUSY) return;
						g_islandNativeLootDrawQueued.store(false,std::memory_order_release);
						if (status==IW8X_OK && result.type()==iw8x::gsc::value_type::string)
						{
							const auto& item=result.get<std::string>();
							OP_INFO("Island Replay1.20 genuine native pickscriptablelootitem() selected_item=%s; world spawnpickup, player use and inventory NOT VERIFIED",item.c_str());
						}
						else OP_WARN("Island genuine Replay1.20 loot native returned status=%u type=%u; no item created",
							unsigned(status),unsigned(result.type()));
					}
					catch (...)
					{
						g_islandNativeLootDrawQueued.store(false,std::memory_order_release);
						OP_WARN("Island real Replay1.20 loot builtin raised an exception; no selection claimed");
					}
					return;
				}
				if (const auto phase=g_islandOpeningProbePhase.load(std::memory_order_acquire);phase!=0)
				{
					if (!IslandCrateProbePermitted() || !iw8x::gsc::internal::in_game_scope() ||
							island_seed_capacity::work_active(g_islandSeedPhase.load(std::memory_order_acquire)) ||
							g_islandSeedCreated==0 ||
						g_islandCacheInstances[0].entity_class!=8)
					{
						g_islandOpeningProbePhase.store(0,std::memory_order_release);
						OP_WARN("Island native cache opening probe canceled: server/map/scriptable epoch changed");
						return;
					}
					const auto& instance=g_islandCacheInstances[0];
					try
					{
						iw8x::gsc::value output;
						iw8x_status status=IW8X_BUSY;
						if (phase==1 || phase==2 || phase==5)
						{
							const std::array<iw8x::gsc::value,1> arguments{iw8x::gsc::value("body")};
							status=iw8x::gsc::internal::query_island_native_scriptable(
								instance,"getscriptablepartstate",output,arguments);
							if (status==IW8X_BUSY) return;
							if (status!=IW8X_OK || output.type()!=iw8x::gsc::value_type::string)
								throw std::runtime_error("real native part-state read failed");
							const auto state=output.get<std::string>();
							if (phase!=5)
							{
								if (state!="closed")
								{
									OP_WARN("Island native cache open test refused: original source2676 part body is %s, not closed",state.c_str());
									g_islandOpeningProbePhase.store(0,std::memory_order_release);
									return;
								}
								OP_INFO("Island native source cache2676 real pre-open part=body state=closed");
								g_islandOpeningProbePhase.store(3,std::memory_order_release);
							}
							else
							{
								OP_INFO("Island native source cache2676 post-transition part=body state=%s; physical open %s; GSC callback, item pickup and inventory NOT VERIFIED",
									state.c_str(),state=="opening" || state=="open"?"observed":"not observed");
								g_islandOpeningProbePhase.store(0,std::memory_order_release);
							}
						}
						else if (phase==3)
						{
							const std::array<iw8x::gsc::value,2> arguments{
								iw8x::gsc::value("body"),iw8x::gsc::value("opening")};
							status=iw8x::gsc::internal::query_island_native_scriptable(
								instance,"getscriptableparthasstate",output,arguments);
							if (status==IW8X_BUSY) return;
							if (status!=IW8X_OK ||
								(output.type()!=iw8x::gsc::value_type::boolean && output.type()!=iw8x::gsc::value_type::integer))
								throw std::runtime_error("native opening state-existence ABI mismatch");
							const bool present=output.type()==iw8x::gsc::value_type::boolean
								?output.get<bool>():output.get<int32_t>()!=0;
							if (!present)
							{
								OP_WARN("Island Replay1.20 source cache2676 has no native body/opening state: original ScriptableDef state transition not ported");
								g_islandOpeningProbePhase.store(0,std::memory_order_release);
								return;
							}
							g_islandOpeningProbePhase.store(4,std::memory_order_release);
							OP_INFO("Island native source cache2676 genuine ScriptableDef body/opening state exists; next frame will test native transition");
						}
						else if (phase==4)
						{
							status=iw8x::gsc::internal::begin_island_native_cache_opening(instance,"body");
							if (status==IW8X_BUSY) return;
							if (status!=IW8X_OK)
								throw std::runtime_error("native Replay1.20 ScriptableSv opening transition failed");
							g_islandOpeningProbePhase.store(5,std::memory_order_release);
							OP_INFO("Island Replay1.20 native body/opening state transition dispatched on source2676; verifying actual engine state next frame");
						}
					}
					catch (const std::exception& e)
					{
						g_islandOpeningProbePhase.store(0,std::memory_order_release);
						OP_WARN("Island genuine ScriptableSv transition probe aborted: %s",e.what());
					}
					return;
				}
			if (g_islandPartAuditPhase.load(std::memory_order_acquire)==2)
			{
				if (!IslandCrateProbePermitted() || !iw8x::gsc::internal::in_game_scope() ||
					g_islandSeedPhase.load(std::memory_order_acquire)!=0)
				{
					g_islandPartAuditPhase.store(0,std::memory_order_release);
					return;
				}
				if (g_islandPartAuditPosition>=islandAuditPositions.size())
				{
					g_islandPartAuditPhase.store(0,std::memory_order_release);
					OP_INFO("Island native Replay1.20 cache part/state audit completed; no opening or inventory claimed");
					return;
				}
				const auto sample=islandAuditPositions[g_islandPartAuditPosition];
				if (sample>=g_islandSeedCreated || g_islandCacheInstances[sample].entity_class!=8)
				{
					g_islandPartAuditPhase.store(0,std::memory_order_release);
					OP_WARN("Island native cache part/state audit requires retained ScriptableSv instance=%u",sample);
					return;
				}
				try
				{
					iw8x::gsc::value answer{};
					const bool part_name=g_islandPartAuditStep==0;
					const std::array<iw8x::gsc::value,1> arg{
						part_name ? iw8x::gsc::value(int32_t(0)) :
							iw8x::gsc::value(g_islandPartNames[g_islandPartAuditPosition])};
					const auto status=iw8x::gsc::internal::query_island_native_scriptable(
						g_islandCacheInstances[sample],
						part_name ? "getscriptablepartnameatindex" : "getscriptablepartstate", answer, arg);
					if (status==IW8X_BUSY) return;
					if (status!=IW8X_OK || answer.type()!=iw8x::gsc::value_type::string)
					{
						g_islandPartAuditPhase.store(0,std::memory_order_release);
						OP_WARN("Island native cache part/state read failed source_index=%u method=%s status=%u result_type=%u",
							islandCachePoints[sample].source_index,part_name ? "name" : "state",
							unsigned(status),unsigned(answer.type()));
						return;
					}
					const auto& text=answer.get<std::string>();
					if (text.empty() || text.size()>96)
					{
						g_islandPartAuditPhase.store(0,std::memory_order_release);
						OP_WARN("Island native Scriptable part/state name invalid; refusing follow-up");
						return;
					}
					if (part_name)
					{
						g_islandPartNames[g_islandPartAuditPosition]=text;
						g_islandPartAuditStep=1;
						OP_INFO("Island native Replay1.20 ScriptableSv source_cache=%u instance=%u part[0]=%s",
							islandCachePoints[sample].source_index,g_islandCacheInstances[sample].instance_index,text.c_str());
					}
					else
					{
							// The read-only audit must never claim the crate is closed:
							// another explicit, real native event may have opened it.
							OP_INFO("Island native Replay1.20 ScriptableSv source_cache=%u instance=%u part=%s current_native_state=%s; audit_read_only=true",
							islandCachePoints[sample].source_index,g_islandCacheInstances[sample].instance_index,
							g_islandPartNames[g_islandPartAuditPosition].c_str(),text.c_str());
						g_islandPartAuditStep=0;
						++g_islandPartAuditPosition;
					}
				}
				catch (...)
				{
					g_islandPartAuditPhase.store(0,std::memory_order_release);
					OP_WARN("Island native Replay1.20 cache part/state read threw; no mutation");
				}
				return;
			}
			if (g_islandCacheAuditPhase.load(std::memory_order_acquire)!=2) return;
			if (!IslandCrateProbePermitted() || !iw8x::gsc::internal::in_game_scope())
			{
				g_islandCacheAuditPhase.store(0,std::memory_order_release);
				return;
			}
			if (g_islandSeedPhase.load(std::memory_order_acquire)!=0) return;
			if (g_islandCacheAuditPosition>=islandAuditPositions.size())
			{
				g_islandCacheAuditPhase.store(0,std::memory_order_release);
				OP_INFO("Island Replay1.20 native ScriptableSv read-only audit complete; opening/inventory NOT VERIFIED");
				return;
			}
			const auto sample=islandAuditPositions[g_islandCacheAuditPosition];
			if (sample>=g_islandSeedCreated || g_islandCacheInstances[sample].entity_class!=8)
			{
				g_islandCacheAuditPhase.store(0,std::memory_order_release);
				OP_WARN("Island native ScriptableSv audit requires all 866 source caches in the current map; available=%u sample=%u",
					g_islandSeedCreated,sample);
				return;
			}
			const auto method=islandAuditMethods[g_islandCacheAuditMethod];
			try
			{
				iw8x::gsc::value answer{};
				const auto status=iw8x::gsc::internal::query_island_native_scriptable(
					g_islandCacheInstances[sample],method,answer);
				if (status==IW8X_BUSY) return;
				if (status!=IW8X_OK)
				{
					g_islandCacheAuditPhase.store(0,std::memory_order_release);
					OP_WARN("Island native ScriptableSv audit stopped status=%u method=%.*s original_source_index=%u instance=%u",
						unsigned(status),int(method.size()),method.data(),islandCachePoints[sample].source_index,
						g_islandCacheInstances[sample].instance_index);
					return;
				}
				if (answer.type()==iw8x::gsc::value_type::vector)
				{
					// Never coerce a native pose/vector result to boolean
					// if a Scriptable method registry mapping changes.
					const auto v=answer.get<iw8x::gsc::vector>();
					OP_WARN("Island native ScriptableSv catalog semantic mismatch original_cache=%u instance=%u method=%.*s native_return_vector=(%.2f,%.2f,%.2f); not interpreted as loot flag",
						islandCachePoints[sample].source_index,g_islandCacheInstances[sample].instance_index,
						int(method.size()),method.data(),double(v.x),double(v.y),double(v.z));
				}
				else if (answer.type()==iw8x::gsc::value_type::integer || answer.type()==iw8x::gsc::value_type::boolean)
				{
					const auto number=answer.type()==iw8x::gsc::value_type::boolean
						? int(answer.get<bool>()) : int(answer.get<int32_t>());
					OP_INFO("Island native ScriptableSv audit original_cache=%u instance=%u method=%.*s result=%d",
						islandCachePoints[sample].source_index,g_islandCacheInstances[sample].instance_index,
						int(method.size()),method.data(),number);
				}
				else
				{
					g_islandCacheAuditPhase.store(0,std::memory_order_release);
					OP_WARN("Island native ScriptableSv audit unexpected return method=%.*s type=%u",
						int(method.size()),method.data(),unsigned(answer.type()));
					return;
				}
				if (++g_islandCacheAuditMethod==islandAuditMethods.size())
				{
					g_islandCacheAuditMethod=0;
					++g_islandCacheAuditPosition;
				}
			}
			catch (...)
			{
				g_islandCacheAuditPhase.store(0,std::memory_order_release);
				OP_WARN("Island native ScriptableSv audit exception during read-only native VM query");
			}
#endif
		}

		void IslandCacheAuditCommand()
		{
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
			if (!IslandCrateProbePermitted() || g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
				g_islandSeedCreated!=islandCachePoints.size())
			{
				OP_WARN("island_crate_audit requires 866 successful current-map native caches, no seed in progress, and opt-in Island BR");
				return;
			}
			uint32_t expected=0;
			if (!g_islandCacheAuditPhase.compare_exchange_strong(expected,1,std::memory_order_acq_rel))
			{
				OP_WARN("island_crate_audit already queued");
				return;
			}
			g_islandCacheAuditPosition=g_islandCacheAuditMethod=0;
			g_islandCacheAuditPhase.store(2,std::memory_order_release);
			OP_INFO("Island native ScriptableSv read-only audit queued for four retained cache handles, three actual engine methods");
#else
			OP_WARN("island_crate_audit requires verified Replay native Island Scriptable bridge");
#endif
		}

		void IslandCachePartAuditCommand()
		{
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
			if (!IslandCrateProbePermitted() || g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
				g_islandSeedCreated!=islandCachePoints.size() ||
				g_islandCacheAuditPhase.load(std::memory_order_acquire)!=0)
			{
				OP_WARN("island_crate_part_audit requires 866 verified current-map caches and idle native diagnostic VM");
				return;
			}

			uint32_t expected=0;
			if (!g_islandPartAuditPhase.compare_exchange_strong(expected,1,std::memory_order_acq_rel))
			{
				OP_WARN("island_crate_part_audit already active");
				return;
			}
			g_islandPartAuditPosition=g_islandPartAuditStep=0;
			g_islandPartNames={};
			g_islandPartAuditPhase.store(2,std::memory_order_release);
			OP_INFO("Island native Replay1.20 crate parts/states read-only audit queued; no state-changing native calls");
#else
			OP_WARN("island_crate_part_audit needs isolated native Island ScriptableSv bridge");
#endif
		}

			void IslandCacheOpeningProbeCommand()
			{
	#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
				char enabled[8]{};
				if (!IslandCrateProbePermitted() ||
					GetEnvironmentVariableA("IW8X_ISLAND_TEST_OPEN_TRANSITION",enabled,DWORD(std::size(enabled)))!=1 ||
						enabled[0]!='1' || island_seed_capacity::work_active(g_islandSeedPhase.load(std::memory_order_acquire)) ||
						g_islandSeedCreated==0 || g_islandCacheInstances[0].entity_class!=8 ||
					g_islandPartAuditPhase.load(std::memory_order_acquire)!=0 ||
					g_islandCacheAuditPhase.load(std::memory_order_acquire)!=0)
				{
						OP_WARN("island_crate_open_probe requires at least one real Replay native cache, idle/capacity-paused VM and explicit IW8X_ISLAND_TEST_OPEN_TRANSITION=1");
					return;
				}
				uint32_t expected=0;
				if (!g_islandOpeningProbePhase.compare_exchange_strong(expected,1,std::memory_order_acq_rel))
				{
					OP_WARN("island_crate_open_probe already active; never issue two opening transitions");
					return;
				}
				OP_INFO("Island source cache2676 authentic native opening state-machine probe queued (NOT a client interaction)");
	#else
				OP_WARN("island_crate_open_probe unavailable without Island-only Scriptable bridge");
	#endif
			}

		void IslandWorldPickupProbeCommand()
		{
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
			// Restricted native Replay 1.20 world-loot ScriptableSv experiment.
			// Never call a client-given ScriptableDef name or fabricate an item.
			char enabled[8]{};
			if (!IslandCrateProbePermitted() ||
				GetEnvironmentVariableA("IW8X_ISLAND_TEST_NATIVE_WORLD_PICKUP",enabled,DWORD(std::size(enabled)))!=1 ||
					enabled[0]!='1' || g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
					g_islandOpeningProbePhase.load(std::memory_order_acquire)!=0 ||
					g_islandWorldSeedPhase.load(std::memory_order_acquire)!=0)
			{
				OP_WARN("island_world_item_probe requires Island BR ready, no active cache batch/opening, IW8X_ISLAND_TEST_NATIVE_WORLD_PICKUP=1");
				return;
			}
			uint32_t expected=0;
			if (!g_islandWorldPickupPhase.compare_exchange_strong(expected,1,std::memory_order_acq_rel))
			{
				OP_WARN("island_world_item_probe already in progress");
				return;
			}
			OP_INFO("Island native Replay1.20 original world point0 item test queued; loot/ScriptableDef selected by real engine, no pickup or inventory assumed");
#else
			OP_WARN("island_world_item_probe requires isolated Replay native Island Scriptable bridge");
#endif
		}

			void IslandNativeWorldSeedCommand()
			{
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
					if (g_islandWorldSeedPhase.load(std::memory_order_acquire)==4)
					{
						if (!IslandCrateProbePermitted() ||
							g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
							g_islandOpeningProbePhase.load(std::memory_order_acquire)!=0 ||
							g_islandWorldPickupPhase.load(std::memory_order_acquire)!=0)
						{
							OP_WARN("Island world resource-paused batch may only resume in the same ready Island BR VM/map epoch");
							return;
						}
						uint32_t paused=4;
						if (g_islandWorldSeedPhase.compare_exchange_strong(paused,
							island_seed_capacity::resume_world_phase(!g_islandWorldSeedItem.empty()),
							std::memory_order_acq_rel))
							OP_INFO("Island Replay1.20 world placement resume requested: created=%u/%u next_original_point=%u item_already_drawn=%u; native capacity will be rechecked without duplicating loot draw",
								g_islandWorldSeedCreated,g_islandWorldSeedTarget,g_islandWorldSeedPosition,
								unsigned(!g_islandWorldSeedItem.empty()));
						return;
					}
				char flag[8]{};
				if (!IslandCrateProbePermitted() ||
					GetEnvironmentVariableA("IW8X_ISLAND_TEST_NATIVE_WORLD_PICKUP",flag,DWORD(std::size(flag)))!=1 ||
					flag[0]!='1' || g_islandWorldSeedCreated!=0 ||
					g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
					g_islandWorldPickupPhase.load(std::memory_order_acquire)!=0 ||
					g_islandOpeningProbePhase.load(std::memory_order_acquire)!=0 ||
					g_islandNativeLootDrawQueued.load(std::memory_order_acquire))
				{
					OP_WARN("island_world_seed requires idle genuine Replay1.20 Island BR VM, explicit world-item opt-in and no earlier batch in this map epoch");
					return;
				}
				char limit_text[32]{};
				uint32_t target=5;
				const auto len=GetEnvironmentVariableA("IW8X_ISLAND_WORLD_BATCH_LIMIT",limit_text,DWORD(std::size(limit_text)));
				if (len)
				{
					if (len>=std::size(limit_text))
					{
						OP_WARN("island_world_seed requires a valid bounded 1..2676 limit");
						return;
					}
					char* end=nullptr;
					const auto parsed=std::strtoul(limit_text,&end,10);
					if (!end || *end!='\0' || parsed<1 || parsed>islandWorldPoints.size())
					{
						OP_WARN("island_world_seed limit must be 1..2676");
						return;
					}
					target=uint32_t(parsed);
				}
				uint32_t expected=0;
				if (!g_islandWorldSeedPhase.compare_exchange_strong(expected,1,std::memory_order_acq_rel))
				{
					OP_WARN("Island original world native generation already queued");
					return;
				}
				g_islandWorldSeedTarget=target;
				g_islandWorldSeedPosition=g_islandWorldSeedCreated=0;
				g_islandWorldSeedItem.clear();
				g_islandWorldSeedPhase.store(2,std::memory_order_release);
				OP_INFO("Island original world loot: genuine Replay1.20 native item drawing and ScriptableSv creation queued=%u/2676 at authenticated source transforms/zone/cluster; pickup/inventory NOT VERIFIED",target);
#else
				OP_WARN("island_world_seed unavailable without isolated Replay1.20 ScriptableSv bridge");
#endif
			}

			void IslandNativeLootDrawProbeCommand()
			{
	#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
				char flag[8]{};
				if (!IslandCrateProbePermitted() ||
					GetEnvironmentVariableA("IW8X_ISLAND_TEST_NATIVE_LOOT_DRAW",flag,DWORD(std::size(flag)))!=1 ||
					flag[0]!='1' || g_islandSeedCreated!=islandCachePoints.size() ||
					g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
						g_islandOpeningProbePhase.load(std::memory_order_acquire)!=0 ||
						g_islandWorldSeedPhase.load(std::memory_order_acquire)!=0)
				{
					OP_WARN("island_native_loot_draw requires original Island BR + 866 caches + IW8X_ISLAND_TEST_NATIVE_LOOT_DRAW=1");
					return;
				}
				bool expected=false;
				if (!g_islandNativeLootDrawQueued.compare_exchange_strong(expected,true,std::memory_order_acq_rel))
					OP_WARN("island_native_loot_draw already queued");
				else OP_INFO("Island Replay1.20 genuine native pickscriptablelootitem() probe queued; NO pickups or inventory side-effects claimed");
	#else
				OP_WARN("island_native_loot_draw requires isolated Replay1.20 native Island bridge");
	#endif
			}

			void IslandCrateSeedCommand()
			{
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
						if (g_islandSeedPhase.load(std::memory_order_acquire)==4)
						{
							if (!IslandCrateProbePermitted() ||
								island_seed_capacity::work_active(g_islandWorldSeedPhase.load(std::memory_order_acquire)) ||
								g_islandOpeningProbePhase.load(std::memory_order_acquire)!=0)
							{
								OP_WARN("island_crate_seed cannot resume a capacity-paused batch during another active VM operation");
								return;
							}
							uint32_t expected_paused=4;
							if (g_islandSeedPhase.compare_exchange_strong(expected_paused,2,std::memory_order_acq_rel))
								OP_INFO("Island native Replay1.20 original cache batch resume requested: created=%u/%u next_source_cache_position=%u; actual engine slots will be rechecked before any spawn",
									g_islandSeedCreated,g_islandSeedTarget,g_islandSeedPosition);
							return;
						}
					if (!IslandCrateProbePermitted() || g_islandCrateProbeQueued.load(std::memory_order_acquire) ||
							island_seed_capacity::work_active(g_islandWorldSeedPhase.load(std::memory_order_acquire)))
				{
					OP_WARN("island_crate_seed requires exact ready Island BR and opt-in native crate probe");
					return;
				}
				char limit_text[32]{};
				uint32_t target=5;
				const auto len=GetEnvironmentVariableA("IW8X_ISLAND_CACHE_BATCH_LIMIT",limit_text,DWORD(std::size(limit_text)));
				if (len && len<std::size(limit_text))
				{
					char* end=nullptr;
					const auto parsed=std::strtoul(limit_text,&end,10);
					if (!end || *end!='\0' || parsed<1 || parsed>islandCachePoints.size())
					{
						OP_WARN("island_crate_seed limit must be 1..866");
						return;
					}
					target=uint32_t(parsed);
				}
					uint32_t expected=0;
					if (!g_islandSeedPhase.compare_exchange_strong(expected,1,std::memory_order_acq_rel))
				{
					OP_WARN("Island native original crate batch already queued");
					return;
				}
				g_islandSeedTarget=target;
				g_islandSeedPosition=g_islandSeedCreated=0;
					g_islandSeedPhase.store(2,std::memory_order_release);
				OP_INFO("Island source-authenticated Replay1.20 native ScriptableSv batch queued count=%u/%u; loot generation/pickup UNVERIFIED",target,unsigned(islandCachePoints.size()));
#else
				OP_WARN("island_crate_seed is unavailable without Island-only native GSC build");
#endif
		}

		void IslandCrateProbeCommand()
		{
			if (!IslandCrateProbePermitted())
			{
				OP_WARN("island_crate_probe requires ready mp_sm_island_1/br and IW8X_ISLAND_TEST_REAL_CRATE=1");
				return;
			}
			bool expected=false;
	#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
				if (g_islandSeedPhase.load(std::memory_order_acquire)!=0 ||
					g_islandWorldSeedPhase.load(std::memory_order_acquire)!=0)
			{
				OP_WARN("island_crate_probe refused while multi-cache spawn is active");
				return;
			}
	#endif
			if (!g_islandCrateProbeQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
				OP_WARN("island_crate_probe already queued");
			else
				OP_INFO("Island native Replay 1.20 crate probe queued for verified server GSC VM, source cache index2676");
		}

	void PumpMapRestartVm() noexcept
	{
		// Called only inside iw8x::gsc::pump_game's game_scope(true), after
		// the native RunFrame tail has verified the server VM is idle.
		if (!g_mapRestart.Pending()) return;
		const auto& binding = iw8x::gsc::internal::bindings();
		if (!iw8x::gsc::internal::in_game_scope() || !(binding.capabilities & IW8X_CAP_GSC_CALL) || !binding.call_builtin)
		{
			g_mapRestart.Finish();
			OP_WARN("map_restart unavailable: server GSC VM bridge is not ready");
			return;
		}
		try
		{
			const std::array<iw8x::gsc::value, 1> args{ iw8x::gsc::value(1) };
			iw8x::gsc::value output;
			const auto status = binding.call_builtin(false, {}, "map_restart", args, output);
			if (status == IW8X_BUSY) return; // Retry only while the VM is transiently busy.
			g_mapRestart.Finish();
			if (status == IW8X_OK)
				OP_INFO("map_restart: stock GSC builtin invoked (argument=1)");
			else
				OP_WARN("map_restart: GSC invocation returned status %u", unsigned(status));
		}
		catch (...)
		{
			g_mapRestart.Finish();
			OP_WARN("map_restart: GSC invocation failed");
		}
	}

	bool CommandEquals(std::string_view value, std::string_view expected)
	{
		if (value.size() != expected.size()) return false;
		for (size_t i = 0; i < value.size(); ++i)
		{
			char a = value[i], b = expected[i];
			if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
			if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
			if (a != b) return false;
		}
		return true;
	}

	void PrintOperatorStatus()
	{
		const bool running = g_base && reinterpret_cast<bool (*)()>(g_base + game::kIsAnyLocalServerRunningRVA)();
		const int netState = g_base ? ReadNetValue(game::kDWNetStateRVA) : -1;
		int sendGameStates = -1;
		uintptr_t globals = 0;
		if (g_base) ReadSendGameStates(sendGameStates, globals);
		char ip[32] = "unknown";
		console::GetLocalIPv4(ip, sizeof(ip));
		OP_INFO("Status | %s | map=%s | mode=%s | ip=%s | net=%s | gameStates=%s", running ? "running" : StageName(g_stage), g_mapName,
			g_gameType, ip, netState == 2 ? "ready" : "starting", sendGameStates == 1 ? "on" : "off");
	}

	bool HandleConsoleCommand(std::string_view command)
	{

		auto remaining = command;
		const auto token = [&remaining]()
		{
			const auto first = remaining.find_first_not_of(" \t");
			if (first == std::string_view::npos)
			{
				remaining = {};
				return std::string_view{};
			}
			remaining.remove_prefix(first);
			const auto end = remaining.find_first_of(" \t");
			const auto result = remaining.substr(0, end);
			remaining = end == std::string_view::npos ? std::string_view{} : remaining.substr(end);
			return result;
		};
		auto name = token();
		if (CommandEquals(name, "map_restart"))
		{
			if (!token().empty())
				OP_WARN("Usage: map_restart");
			else
			{
				switch (g_mapRestart.Queue())
				{
				case MapRestartRequest::Result::Queued: OP_INFO("map_restart queued for server GSC VM"); break;
				case MapRestartRequest::Result::AlreadyQueued: OP_WARN("map_restart already queued"); break;
				case MapRestartRequest::Result::NotReady: OP_WARN("map_restart requires a running gameplay server"); break;
				}
			}

			return true;
		}
		if (CommandEquals(name, "set")) name = token();
		if (CommandEquals(name, "live_lobby_minplayers_start"))
		{
			const auto argument = token();
			if (argument.empty())
			{
				OP_WARN("Usage: live_lobby_minplayers_start <1..200> | updates live, BR and party minimums");
				return true;
			}
			int minimum = 0;
			const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), minimum);
			if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size() || !token().empty() || minimum < 1 ||
				minimum > 200)
			{
				OP_WARN("Usage: live_lobby_minplayers_start <1..200> | updates live, BR and party minimums");
				return true;
			}
			const auto value = std::to_string(minimum);
			const auto batch =
				"set live_lobby_minplayers_start " + value + "\nset br_minplayers " + value + "\nset LLNLRTQRPO " + value + "\n";
			if (commands::Queue(g_base, batch.c_str()))
				OP_INFO("Match start minimum queued: %d players | live lobby, BR and party", minimum);
			else
				OP_WARN("Could not queue match start minimum; try again when the server is ready");
			return true;
		}
		if (CommandEquals(command, "end_server"))
		{
			if (!g_endRequested.exchange(true, std::memory_order_acq_rel))
			{
				OP_INFO("end_server requested | returning host to playlist waiting");
			}
			return true;
		}
		if (online_services::HandleCommand(command)) return true;
		if (CommandEquals(command, "status"))
		{
			PrintOperatorStatus();
			return true;
		}
		if (command == "noclip" || command.rfind("noclip ", 0) == 0 || command == "mw_noclip" || command.rfind("mw_noclip ", 0) == 0)
		{
			int clientNum = 0;
			std::string sub;
			const size_t spacePos = command.find(' ');
			if (spacePos != std::string_view::npos)
			{
				sub = std::string(command.substr(spacePos + 1));
				while (!sub.empty() && (sub.front() == ' ' || sub.front() == '\t'))
					sub.erase(sub.begin());
				while (!sub.empty() && (sub.back() == ' ' || sub.back() == '\t'))
					sub.pop_back();
			}

			if (sub.empty())
			{
				bool newState = false;
				if (noclip::Toggle(g_base, 0, &newState))
				{
					OP_INFO("Noclip | client 0 | %s", newState ? "ON" : "OFF");
				}
				else
				{
					OP_WARN("Noclip | client 0 not spawned or unavailable");
				}
			}
			else if (sub == "on" || sub == "1")
			{
				if (noclip::SetState(g_base, 0, true))
				{
					OP_INFO("Noclip | client 0 | ON");
				}
				else
				{
					OP_WARN("Noclip | client 0 not spawned or unavailable");
				}
			}
			else if (sub == "off" || sub == "0")
			{
				if (noclip::SetState(g_base, 0, false))
				{
					OP_INFO("Noclip | client 0 | OFF");
				}
				else
				{
					OP_WARN("Noclip | client 0 not spawned or unavailable");
				}
			}
			else
			{
				char* end = nullptr;
				long val = std::strtol(sub.c_str(), &end, 10);
				if (end && (*end == '\0' || *end == ' '))
				{
					clientNum = static_cast<int>(val);
					bool newState = false;
					if (noclip::Toggle(g_base, clientNum, &newState))
					{
						OP_INFO("Noclip | client %d | %s", clientNum, newState ? "ON" : "OFF");
					}
					else
					{
						OP_WARN("Noclip | client %d not spawned or unavailable", clientNum);
					}
				}
				else
				{
					OP_WARN("Usage: noclip [clientNum] [on|off|1|0]");
				}
			}
			return true;
		}
		return false;
	}

	void NoclipDedicatedCommand()
	{
		char clientText[32]{}, stateText[32]{};
		ReadSVCmdArg(1, clientText, sizeof(clientText));
		ReadSVCmdArg(2, stateText, sizeof(stateText));

		int clientNum = 0;
		if (clientText[0] != '\0')
		{
			char* end = nullptr;
			const long val = std::strtol(clientText, &end, 10);
			if (end && *end == '\0')
			{
				clientNum = static_cast<int>(val);
			}
			else if (_stricmp(clientText, "on") == 0 || strcmp(clientText, "1") == 0)
			{
				strcpy_s(stateText, clientText);
				clientNum = 0;
			}
			else if (_stricmp(clientText, "off") == 0 || strcmp(clientText, "0") == 0)
			{
				strcpy_s(stateText, clientText);
				clientNum = 0;
			}
		}

		if (stateText[0] == '\0')
		{
			bool newState = false;
			if (noclip::Toggle(g_base, clientNum, &newState))
			{
				OP_INFO("Noclip | client %d | %s", clientNum, newState ? "ON" : "OFF");
			}
			else
			{
				OP_WARN("Noclip | client %d not spawned or unavailable", clientNum);
			}
		}
		else if (_stricmp(stateText, "on") == 0 || strcmp(stateText, "1") == 0)
		{
			if (noclip::SetState(g_base, clientNum, true))
			{
				OP_INFO("Noclip | client %d | ON", clientNum);
			}
			else
			{
				OP_WARN("Noclip | client %d not spawned or unavailable", clientNum);
			}
		}
		else if (_stricmp(stateText, "off") == 0 || strcmp(stateText, "0") == 0)
		{
			if (noclip::SetState(g_base, clientNum, false))
			{
				OP_INFO("Noclip | client %d | OFF", clientNum);
			}
			else
			{
				OP_WARN("Noclip | client %d not spawned or unavailable", clientNum);
			}
		}
	}

	static ServerCommand g_commands[] = {
		{ "devmap", &DevmapCommand, {}, {} },
		{ "iw8_ds_config_ready", &ConfigProcessed, {}, {} },
		{ "addbot", &AddBotCommand, {}, {} },
		{ "addbots", &AddBotsCommand, {}, {} },
		{ "removebot", &RemoveBotCommand, {}, {} },
		{ "removebots", &RemoveBotsCommand, {}, {} },
		{ "bot_status", &BotStatusCommand, {}, {} },
#ifdef IW8X_ISLAND_PORT_PROBE
			{ "island_crate_probe", &IslandCrateProbeCommand, {}, {} },
			{ "island_crate_seed", &IslandCrateSeedCommand, {}, {} },
				{ "island_crate_audit", &IslandCacheAuditCommand, {}, {} },
				{ "island_crate_part_audit", &IslandCachePartAuditCommand, {}, {} },
					{ "island_crate_open_probe", &IslandCacheOpeningProbeCommand, {}, {} },
					{ "island_native_loot_draw", &IslandNativeLootDrawProbeCommand, {}, {} },
					{ "island_world_item_probe", &IslandWorldPickupProbeCommand, {}, {} },
						{ "island_world_seed", &IslandNativeWorldSeedCommand, {}, {} },
#endif
		{ "bot_think_interval", &BotThinkIntervalCommand, {}, {} },
		{ "bot_goal_interval", &BotGoalIntervalCommand, {}, {} },
		{ "bot_profile", &BotProfileCommand, {}, {} },
		{ "bot_movement_workers", &BotMovementWorkersCommand, {}, {} },
		{ "bot_gpu_nav", &BotGpuNavCommand, {}, {} },
		{ "iw8_ds_bot_setup", &CompleteBotClassCommands, {}, {} },
		{ "noclip", &NoclipDedicatedCommand, {}, {} },
		{ "mw_noclip", &NoclipDedicatedCommand, {}, {} },
	};

	bool CommandsAvailable(uintptr_t headRva)
	{
		CommandNode* node = nullptr;
		if (!safemem::ReadBytes(reinterpret_cast<void*>(g_base + headRva), &node, sizeof(node))) return false;
		for (unsigned i = 0; node && i < 8192; ++i)
		{
			CommandNode copy{};
			char name[96]{};
			if (!safemem::ReadBytes(node, &copy, sizeof(copy)) || !safemem::ReadString(copy.name, name, sizeof(name))) return false;
			for (const auto& command : g_commands)
			{
				if (_stricmp(name, command.name) == 0)
				{
					LOG_ERR("Dedicated", "command already registered: %s", name);
					return false;
				}
			}
			node = copy.next;
		}
		return !node;
	}

	bool RegisterCommands()
	{
		if (!CommandsAvailable(game::kPriorityClientCommandsRVA) || !CommandsAvailable(game::kClientCommandsRVA) ||
			!CommandsAvailable(game::kServerCommandsRVA))
			return false;
		using AddFn = void(__fastcall*)(ServerCommand*, unsigned);
		reinterpret_cast<AddFn>(g_base + game::kCmdAddServerCommandListRVA)(g_commands, std::size(g_commands));
		for (const auto& command : g_commands)
		{
			if (command.server.function != command.function ||
				reinterpret_cast<uintptr_t>(command.client.function) != g_base + game::kServerCommandForwarderRVA)
				return false;
		}
		LOG_INFO("Dedicated", "registered devmap bridge and cfg barrier through Cmd_AddServerCommandList");
		return true;
	}
}
