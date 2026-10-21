#include "runtime.hpp"
#include "diagnostics.hpp"
#include "frame_metrics.hpp"
#ifdef IW8X_ISLAND_PORT_PROBE
#include "vm_profiler.hpp"
#include "gsc/compiled/gsc.hpp"
#include "engine/safemem.h"
#endif
#include "replay.hpp"
#include "dedicated/dedicated.h"
#ifdef IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE
#include "dedicated/runtime/commands.hpp"
#include "dedicated/dedicated.h"
#include "auto_server.h"
#include "dedicated/runtime/lifecycle.hpp"
#include "game.h"
#endif
#include "thirdparty/minhook/include/MinHook.h"

#include <intrin.h>

#include "chat.hpp"
#include "../api.hpp"
#include "../ownership.hpp"

#include "engine/image.hpp"

#include <atomic>
#include <array>
#include <vector>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
namespace iw8x::gsc::internal
{
	namespace
	{
		std::atomic<bool> ready{};
		std::mutex install_mutex;
		std::array<void*, 14> targets{};
		std::array<void*, 14> trampolines{};
		using frame_fn = void (*)(int32_t);
		using free_fn = void (*)(void*);
		using begin_fn = void (*)(void*, uint8_t, uint32_t);
		using response_fn = void (*)(void*, const void*, int);
		using free_client_fn = void (*)(void*, const char*);
		using shutdown_fn = void (*)(void*, const char*);
		using drop_client_fn = void (*)(void*, const char*, bool);
		using client_spawn_fn = void (*)(void*, const float*, const float*);
		using player_die_fn = void (*)(void*, const void*, void*, int, int, int, const void*, bool, const float*, int, int);
		using end_scripts_fn = void (*)(void*);
		frame_fn original_frame{};
#ifdef IW8X_ISLAND_PORT_PROBE
			using vm_execute_fn = uint32_t (*)(void*, uint32_t, const uint8_t*, uint32_t);
			vm_execute_fn original_vm_execute{};
			bool vm_profile_enabled{};
			bool island_frame_heartbeat_enabled{};
			thread_local bool vm_profile_in_frame{};
			thread_local vm_profiler vm_samples;
			struct vm_program_location
			{
				char name[96]{};
				uint32_t offset{};
				uint32_t slot{};
			};
			vm_program_location locate_vm_program(std::uintptr_t address)
			{
				vm_program_location result{};
				if (!address) return result;
					// The genuine Island Source ScriptFiles are passed through a
					// private, native block-6 compiler view. Runtime VM addresses
					// refer to its compacted copies, NOT the stock DB type-52 pool.
					// The source loader already tracks those exact linked ranges;
					// try that authoritative ownership first. This is invoked only
					// when a one-second performance sample is actually published.
					std::string compiler_script;
					std::size_t compiler_offset{};
					if (::gsc::DescribeIslandCodeSite(address, compiler_script, compiler_offset))
					{
						std::snprintf(result.name, sizeof(result.name), "%s", compiler_script.c_str());
						result.offset = static_cast<std::uint32_t>(compiler_offset);
						result.slot = UINT32_MAX; // private native compiler view, not DB slot
						return result;
					}
				// Independently documented Replay ScriptFile pool layout: one
				// 40-byte ScriptFile entry per type-52 slot.
				constexpr size_t type = 52;
				constexpr uintptr_t count_rva = 0x23FBBA0 + type * 4;
				constexpr uintptr_t stride_rva = 0x2458160 + type * 4;
				constexpr uintptr_t pool_rva = 0xB8F5A80 + type * 8;
				uint32_t count{}, stride{};
				const ::gsc::ScriptFile* pool{};
				if (!safemem::ReadBytes(game::at(count_rva), &count, sizeof(count)) ||
					!safemem::ReadBytes(game::at(stride_rva), &stride, sizeof(stride)) ||
					!safemem::ReadBytes(game::at(pool_rva), &pool, sizeof(pool)) ||
					!pool || !count || count > 4096 || stride != sizeof(::gsc::ScriptFile))
					return result;
				std::vector<::gsc::ScriptFile> scripts(count);
				if (!safemem::ReadBytes(pool, scripts.data(), scripts.size() * sizeof(::gsc::ScriptFile)))
					return result;
				for (uint32_t slot = 0; slot < count; ++slot)
				{
					const auto& file = scripts[slot];
					const auto begin = reinterpret_cast<uintptr_t>(file.bytecode_);
					if (!begin || file.bytecode_len_ <= 0 || file.bytecode_len_ > 4 * 1024 * 1024 ||
						address < begin || address - begin >= static_cast<size_t>(file.bytecode_len_))
						continue;
					result.offset = static_cast<uint32_t>(address - begin);
					result.slot = slot;
					safemem::ReadString(file.name_, result.name, sizeof(result.name));
					return result;
				}
				return result;
			}
			// Verified against shipping Replay 1.20.4.7623265, RVA 0x132BA60.
			constexpr uint8_t vm_execute_entry[]{
				0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
				0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57 };
			uint32_t vm_execute(void* ctx, uint32_t thread, const uint8_t* program, uint32_t mode)
			{
				if (!vm_profile_in_frame)
					return original_vm_execute(ctx, thread, program, mode);
				const auto start = std::chrono::steady_clock::now();
				const auto result = original_vm_execute(ctx, thread, program, mode);
				const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - start).count();
				vm_samples.record(reinterpret_cast<uintptr_t>(program), static_cast<uint64_t>((std::max)(elapsed, 0ll)));
				return result;
			}
#endif
		free_fn original_free{};
		begin_fn original_begin{};
		response_fn original_response{};
		free_client_fn original_free_client{};
		shutdown_fn original_shutdown{};
		free_fn original_shutdown_memory{};
		drop_client_fn original_drop_client{};
		client_spawn_fn original_client_spawn{};
		player_die_fn original_player_die{};
		using player_killed_fn = void (*)(void*, const void*, void*, int, int, int, const void*, bool, const float*, int, int, int);
		player_killed_fn original_player_killed{};
		end_scripts_fn original_end_scripts{};
		client_connect_fn original_connect{};
		free_fn original_finalize{};

		struct death_context
		{
			void* self;
			bool committed{};
			player_snapshot victim{}, killer{};
			uint32_t reason{};
		};

		thread_local death_context* current_death{};
		thread_local bool gameplay_shutdown{};

#ifdef IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE
			// Island BR join tracing is opt-in, observational, and compiled only
			// in the native prematch bridge variant. This is *not* a spawn fix:
			// it identifies the last genuine Replay script/client transition.
			bool island_loadout_trace_enabled() noexcept
			{
				char setting[8]{};
				return dedicated::Enabled() &&
					std::strcmp(dedicated::detail::g_mapName, "mp_sm_island_1") == 0 &&
					std::strcmp(dedicated::detail::g_gameType, "br") == 0 &&
					GetEnvironmentVariableA("IW8X_ISLAND_LOADOUT_TRACE", setting, sizeof(setting)) == 1 &&
					setting[0] == '1';
			}

			// Tokens 56344/25090 were already verified by the Replay-native bot
			// class pump. Read only the actual script object and native variable
			// pool; do not emit fake loadout notifications or force ClientSpawn.
			struct island_script_field
			{
				uint32_t value{};
				uint8_t type{};
			};
			island_script_field island_read_script_field(void* ctx, uint32_t object, uint32_t token) noexcept
			{
				if (!ctx || !object) return {};
				const auto lookup = reinterpret_cast<uint32_t (*)(void*,uint32_t,uint32_t)>(
					game::at(game::kScrFindVariableRVA));
				const uint32_t child = lookup(ctx, object, token);
				if (!child) return {};
				uintptr_t children{};
				if (!safemem::ReadBytes(static_cast<const uint8_t*>(ctx) + 13504, &children, sizeof(children)) ||
					!children || child > 1000000) return {};
				island_script_field field{};
				const auto* data = reinterpret_cast<const uint8_t*>(children + 32ull * child);
				uint8_t type{};
				if (!safemem::ReadBytes(data, &field.value, sizeof(field.value)) ||
					!safemem::ReadBytes(data + 12, &type, sizeof(type))) return {};
				field.type = type & 63;
				return field;
			}

				// The original DMZ/Blood Money script enters an extended pre-infil
				// selection path and does NOT call native ClientSpawn until its
				// state machine permits it. Observe actual Replay script variables
				// at the existing idle-VM boundary. SL_GetString interns tokens, so
				// intern each literal only once (not once per sampled frame).
					uint32_t island_trace_token(const char* name) noexcept
				{
					if (!name) return 0;
						// Script object property keys are *canonical field hashes*,
						// not SL_GetString's global string-pool identifiers. The former
						// are resolved by this exact verified Replay routine, also used
						// by the source-GSC linker. The Island-only hook adds its source
						// canonical namespace delta when the restored source scripts
						// are selected. Merely interning strings returned IDs ~398k and
						// incorrectly made all extended BR fields look undefined.
						return reinterpret_cast<uint32_t (*)(const char*)>(
							game::at(0x1316BD0))(name);
				}

				struct island_trace_descriptor
				{
					const char* name;
					uint32_t token;
						uint32_t sl_string_token;
				};

					// Replay stores script-object fields under native 24-bit integer
					// keys. Neither SL_GetString nor the restored source canonical
					// name allocator can be assumed to reproduce precompiled field
					// IDs. Enumerate native child key identities *read only*, using
					// the same sibling/name ABI as the existing snapshot_reader.
					// This establishes whether level was populated before trying to
					// assign GSC names to the entries. No arbitrary field writes.
					void island_trace_native_variable_keys(void* ctx, uint32_t object, const char* label) noexcept
					{
						if (!ctx || !object || !label) return;
						const auto first = reinterpret_cast<uint32_t (*)(void*, uint32_t)>(
							game::at(game::addresses::gsc_api::FindFirstSibling));
						const auto next = reinterpret_cast<uint32_t (*)(void*, uint32_t)>(
							game::at(game::addresses::gsc_api::FindNextSibling));
						const auto get_name = reinterpret_cast<uint32_t (*)(void*, uint32_t)>(
							game::at(game::addresses::gsc_api::GetVariableName));
						char out[960]{};
						int written = std::snprintf(out, sizeof(out),
							"IslandLoadoutTrace native_keys %s=%u", label, object);
						unsigned total{};
						for (uint32_t child=first(ctx,object); child && total<512; child=next(ctx,child))
						{
							if (++total<=22 && written>0 && size_t(written)<sizeof(out))
							{
								const uint32_t key=get_name(ctx,child);
								uint8_t type{};
								uintptr_t children{};
								if (child<=1000000 && safemem::ReadBytes(
									static_cast<const uint8_t*>(ctx)+13504,&children,sizeof(children)) && children)
									(void)safemem::ReadBytes(reinterpret_cast<const void*>(children+32ull*child+12),&type,sizeof(type));
								const int length=std::snprintf(out+written,sizeof(out)-size_t(written),
									" %u:%u",key,unsigned(type&63));
								if (length>0) written+=length;
							}
						}
						if (written>0 && size_t(written)<sizeof(out))
							(void)std::snprintf(out+written,sizeof(out)-size_t(written)," count=%u%s",total,total==512?"+":"");
						report(nullptr,out);
					}

				// Kept as a fixed-size set so unusual/unknown engine VM objects
				// cannot force heap allocations from the native frame hook.
				void island_trace_extended_fields(void* ctx, uint32_t player) noexcept
				{
					static island_trace_descriptor player_fields[]{
						{"br_infilstarted", 0}, {"waitingtospawn", 0},
						{"waitingtospawnamortize", 0}, {"selectedclass", 0},
						{"sessionstate", 0}, {"team", 0}, {"class", 0},
							{"selectedspawnarea", 0}, {"forcespawnorigin", 0},
							{"pers", 0}, {"waitingtoselectclass", 0}, {"hasspawned", 0},
					};
					static island_trace_descriptor level_fields[]{
						{"br_prematchstarted", 0}, {"br_infils_disabled", 0},
						{"ingraceperiod", 0}, {"gameended", 0},
						{"disablespawning", 0}, {"usespawnselection", 0},
						{"stop_visited_once", 0}, {"snatchspawnalltoc130done", 0},
							{"infilstruct", 0}, {"players", 0},
							{"onplayerconnect", 0}, {"onstartgametype", 0},
							{"getspawnpoint", 0}, {"modeonspawnplayer", 0}
					};
					static const bool initialized = []()
					{
							const auto intern = reinterpret_cast<uint32_t (*)(const char*, uint32_t)>(
								game::at(game::addresses::gsc_api::SL_GetString));
							for (auto& field : player_fields)
							{
								field.token = island_trace_token(field.name);
								field.sl_string_token = intern(field.name, 0);
							}
							for (auto& field : level_fields)
							{
								field.token = island_trace_token(field.name);
								field.sl_string_token = intern(field.name, 0);
							}
						return true;
					}();
						if (!initialized) return;
						// The source-GSC namespace cannot be inferred from the
						// Replay string lookup alone. Two fields have ground-truth
						// script-variable keys from Replay's actual player object.
						// When the computed names disagree, no extended field miss
						// can be interpreted as a zero/uninitialized BR state.
						const bool canonical_fields_verified =
							player_fields[10].token == 56344 &&
							player_fields[11].token == 25090;
					uint32_t level{};
					const auto* bytes = static_cast<const uint8_t*>(ctx);
					if (!safemem::ReadBytes(bytes + game::addresses::gsc_api::layout::level_object,
						&level, sizeof(level))) return;
						// Before trusting the dynamic-field diagnostics, verify
						// SL_GetString's IDs against two already-proven direct Replay
						// script-variable tokens. If these disagree, a 0:0 extended
						// field is only a *missed lookup*, not proof that the original
						// BR GSC never initialized the field.
						static bool token_probe_logged{};
						if (!token_probe_logged)
						{
							token_probe_logged = true;
							char token_message[240]{};
							std::snprintf(token_message, sizeof(token_message),
								"IslandLoadoutTrace token_control CANON_waiting=%u SL_waiting=%u verified_waiting=56344 CANON_hasspawned=%u SL_hasspawned=%u verified_hasspawned=25090",
								player_fields[10].token, player_fields[10].sl_string_token,
								player_fields[11].token, player_fields[11].sl_string_token);
							report(nullptr, token_message);
						}
						if (!canonical_fields_verified)
						{
							static bool mismatch_reported{};
							if (!mismatch_reported)
							{
								mismatch_reported = true;
								report(nullptr, "IslandLoadoutTrace extended GSC property keys UNRESOLVED: canonical control differs from verified source tokens; previous type0:value0 lookups are INVALID evidence of BR initialization or missing callbacks; inspecting native level variable keys only");
							}
							return;
						}
						char out[2048]{};
						int written = std::snprintf(out, sizeof(out),
							"IslandLoadoutTrace extended player=%u level=%u", player, level);
						for (const auto& field : player_fields)
					{
							if (!player) break; // independent level-side boot probe
						if (!field.token || written < 0 || size_t(written) >= sizeof(out)) break;
						const auto value = island_read_script_field(ctx, player, field.token);
						const int n = std::snprintf(out + written, sizeof(out) - size_t(written),
							" P.%s=%u:%u", field.name, unsigned(value.type), value.value);
						if (n <= 0) break;
						written += n;
					}
					for (const auto& field : level_fields)
					{
						if (!level || !field.token || written < 0 || size_t(written) >= sizeof(out)) break;
						const auto value = island_read_script_field(ctx, level, field.token);
						const int n = std::snprintf(out + written, sizeof(out) - size_t(written),
							" L.%s=%u:%u", field.name, unsigned(value.type), value.value);
						if (n <= 0) break;
						written += n;
					}
					report(nullptr, out);
				}

			void island_trace_loadout_state() noexcept
			{
				if (!island_loadout_trace_enabled() || !gameplay_server() ||
					!runtime_ready()) return;
				static thread_local ULONGLONG last_sample{};
				const auto now = GetTickCount64();
				if (now - last_sample < 2000) return;
				last_sample = now;
				void* ctx = server_context();
				if (!ctx || !vm_idle(ctx)) return;
				const auto find_entity = reinterpret_cast<uint32_t (*)(void*,uint32_t,uint32_t,uint32_t)>(
					game::at(game::kScrFindEntityIdRVA));
						bool observed_human{};
				for (uint32_t slot = 0; slot < 200; ++slot)
				{
					void* client = client_at(slot);
					if (!client || client_state(client) < 2) continue;
					// The native bot type is 2; never attribute AI loadout
					// notifications or wait states to a human client.
					int native_type{};
					if (!safemem::ReadBytes(static_cast<const uint8_t*>(client) + 214756,
						&native_type, sizeof(native_type)) || native_type == 2) continue;
					const auto name = read_client_name(client);
					if (name.empty()) continue;
						observed_human = true;
					const auto object = find_entity(ctx, slot, 0, 0);
					const auto waiting = island_read_script_field(ctx, object, 56344); // waitingtoselectclass
					const auto spawned = island_read_script_field(ctx, object, 25090); // hasspawned
					char line[320]{};
					std::snprintf(line, sizeof(line),
						"IslandLoadoutTrace native_player slot=%u state=%u script_object=%u waitingtoselectclass=(type%u,value%u) hasspawned=(type%u,value%u)",
						slot, unsigned(client_state(client)), object,
						unsigned(waiting.type), waiting.value, unsigned(spawned.type), spawned.value);
					report(nullptr, line);
							if (slot == 0 && object)
								island_trace_extended_fields(ctx, object);
				}
					// The user-visible "Gearing Up" transition relies on BR GSC
					// registration *before* any client has selected a class. Observe
					// level's genuine initialization callbacks without requiring a
					// human to join or writing synthetic spawn/player flags.
						if (!observed_human)
						{
							island_trace_extended_fields(ctx, 0);
							static unsigned boot_sample_count{};
							if (boot_sample_count++<2)
							{
								uint32_t level{};
								if (safemem::ReadBytes(static_cast<const uint8_t*>(ctx)+
									game::addresses::gsc_api::layout::level_object,&level,sizeof(level)))
									island_trace_native_variable_keys(ctx,level,"level");
							}
						}
			}
#endif

		template <class F>
		void observe(F action) noexcept
		{
			if (!ready.load(std::memory_order_acquire)) return;
			try
			{
				action();
			}
			catch (const std::exception& e)
			{
				report(nullptr, e.what());
			}
			catch (...)
			{
				report(nullptr, "player lifecycle observation failed");
			}
		}

		iw8x_status read_dvar_string(std::string_view name, value& output)
		{
			return read_typed_dvar(name, IW8X_STRING, output);
		}

		void frame(int32_t time)
		{
				// This trampoline wraps Replay's *native* G_MainMP_RunFrame,
				// not a plugin callback. Report CPU and elapsed time separately:
				// a 100ms stall/sleep used to be misreported as a 100ms plugin
				// CPU bottleneck. Aggregate instead of allocating/logging a line
				// on every slow frame.
				const auto thread_cpu_us = []() noexcept -> std::uint64_t
				{
					FILETIME created{}, exited{}, kernel{}, user{};
					if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return 0;
					const auto ticks = [](const FILETIME& part) -> std::uint64_t
					{
						return (std::uint64_t(part.dwHighDateTime) << 32) | part.dwLowDateTime;
					};
					return (ticks(kernel) + ticks(user)) / 10;
				};
				const auto wall_begin = std::chrono::steady_clock::now();
				const auto cpu_begin = thread_cpu_us();
#ifdef IW8X_ISLAND_PORT_PROBE
				const bool previous_profile = vm_profile_in_frame;
				vm_profile_in_frame = vm_profile_enabled;
#endif
				original_frame(time);
	#ifdef IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE
					island_trace_loadout_state();
	#endif
#ifdef IW8X_ISLAND_PORT_PROBE
				vm_profile_in_frame = previous_profile;
#endif
				const auto cpu_end = thread_cpu_us();
				const auto now = std::chrono::steady_clock::now();
				const auto wall_us = static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::microseconds>(now - wall_begin).count());
				thread_local engine_frame_metrics metrics;
					const auto result = metrics.add(static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()),
						wall_us, cpu_end >= cpu_begin ? cpu_end - cpu_begin : 0,
						40000,
#ifdef IW8X_ISLAND_PORT_PROBE
						island_frame_heartbeat_enabled
#else
						false
#endif
						);
				const auto native_dispatch = result.complete ? take_native_dispatch_stats() : native_dispatch_snapshot{};
#ifdef IW8X_ISLAND_PORT_PROBE
				const auto vm = result.complete && vm_profile_enabled ? vm_samples.take() : vm_profiler::snapshot{};
#endif
				if (result.publish)
				{
					char message[768]{};
					const auto written = std::snprintf(message, sizeof(message),
							"[PluginPerf] op=frame.engine.native frames=%llu slow=%llu wall_avg_us=%llu wall_max_us=%llu cpu_avg_us=%llu cpu_max_us=%llu native_calls=%llu native_missing=%llu native_dispatch_us=%llu native_peak_us=%llu missing_top_method=%u missing_top_slot=%u missing_top_calls=%llu missing_second_method=%u missing_second_slot=%u missing_second_calls=%llu",
						static_cast<unsigned long long>(result.frames),
						static_cast<unsigned long long>(result.slow_frames),
						static_cast<unsigned long long>(result.wall_total_us / result.frames),
						static_cast<unsigned long long>(result.wall_max_us),
						static_cast<unsigned long long>(result.cpu_total_us / result.frames),
						static_cast<unsigned long long>(result.cpu_max_us),
						static_cast<unsigned long long>(native_dispatch.calls),
						static_cast<unsigned long long>(native_dispatch.missing),
						static_cast<unsigned long long>(native_dispatch.elapsed_us),
						static_cast<unsigned long long>(native_dispatch.peak_us),
						unsigned(native_dispatch.missing_top_method),
						native_dispatch.missing_top_slot,
							static_cast<unsigned long long>(native_dispatch.missing_top_calls),
							unsigned(native_dispatch.missing_second_method),
							native_dispatch.missing_second_slot,
							static_cast<unsigned long long>(native_dispatch.missing_second_calls));
#ifdef IW8X_ISLAND_PORT_PROBE
					if (vm_profile_enabled && written > 0 && static_cast<size_t>(written) < sizeof(message))
					{
						const auto location = locate_vm_program(vm.top_program);
						std::snprintf(message + written, sizeof(message) - static_cast<size_t>(written),
							" vm_exec_calls=%llu vm_exec_us=%llu vm_max_us=%llu vm_hot_program=%p vm_hot_calls=%llu vm_program_overflow=%llu vm_hot_script=%s vm_hot_offset=%u vm_hot_pool=%u",
							static_cast<unsigned long long>(vm.calls),
							static_cast<unsigned long long>(vm.vm_us),
							static_cast<unsigned long long>(vm.maximum_us),
							reinterpret_cast<const void*>(vm.top_program),
							static_cast<unsigned long long>(vm.top_program_calls),
							static_cast<unsigned long long>(vm.overflow),
							location.name[0] ? location.name : "<unresolved>",
							location.offset, location.slot);
					}
#endif
					report(nullptr, message);
				}
			if (!ready.load(std::memory_order_acquire)) return;
			try
			{
				if (gameplay_server() && !*game::at<uint32_t>(game::addresses::gsc_api::server_start_state) && vm_idle(server_context()))
						{
	#ifdef IW8X_ISLAND_PORT_PROBE
							// pump_game executes after original_frame and after this
							// telemetry batch is published. The existing VM sampling
							// interval covered only original_frame, so a pathological
							// post-frame script could generate thousands of native calls
							// with vm_exec_calls=0. Include this distinct execution
							// window when profiling is explicitly enabled; its samples
							// are reported with the *next* batch, just like the native
							// dispatch counters. Do not alter scheduler execution.
							struct vm_sample_scope
							{
								bool& state;
								bool previous;
								vm_sample_scope(bool& target, bool enabled) noexcept
									: state(target), previous(target)
								{
									state = enabled;
								}
								~vm_sample_scope() noexcept { state = previous; }
							} scope{ vm_profile_in_frame, vm_profile_enabled };
	#endif
					pump_game();
						}
			}
			catch (const std::exception& e)
			{
				report(nullptr, e.what());
			}
		}

		void free_entity(void* entity)
		{
			if (!runtime_ready())
			{
				original_free(entity);
				return;
			}
			// Hook argument is the engine's live gentity pointer; retail itself reads
			// the entity number directly from word ptr [entity]. Keep the observer just
			// as cheap instead of VirtualQuery'ing on every entity retirement.
			const auto index = entity ? *static_cast<const int16_t*>(entity) : int16_t{ -1 };
			if (runtime_ready() && index >= 0) invalidate_entity(index);
			original_free(entity);
			if (runtime_ready() && index >= 0) invalidate_entity(index); // Invalidate captures made during native cleanup, too.
		}

		void begin(void* context, uint8_t user, uint32_t precache_count)
		{
			if (!ready.load(std::memory_order_acquire) || context != server_context())
			{
				original_begin(context, user, precache_count);
				return;
			}
			try
			{
				restore_port_tables(context);
				original_begin(context, user, precache_count);
				publish_tables(context);
			}
			catch (const std::exception& e)
			{
				// Continuing could dispatch an extended ID outside another owner's
				// table. Do not throw a C++ exception through retail engine frames.
				report(nullptr, e.what());
				report(nullptr, "fatal native dispatch table mismatch; refusing unsafe VM execution");
				std::terminate();
			}
		}

		void end_scripts(void* context)
		{
			if (!runtime_ready())
			{
				original_end_scripts(context);
				return;
			}
#ifndef IW8X_CLIENT_ONLY
			dedicated::ObserveScriptEndLoad(context);
#endif
			if (context == server_context()) resolve_script_exports(context);
			original_end_scripts(context);
		}

		void finalize_startup(void* app)
		{
			original_finalize(app);
			observe(
				[]
				{
					if (!gameplay_server()) return;
					map_ready();
					server_ready();
				});
		}

		void connect_response(void* client, const void* address, int migration)
		{
			original_response(client, address, migration);
	#ifdef IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE
				if (island_loadout_trace_enabled())
				{
					const auto player = find_client_player(client, true);
					char line[192]{};
					std::snprintf(line, sizeof(line),
						"IslandLoadoutTrace connection_response slot=%u client_state=%u migration=%d",
						player ? player.player.client_num : UINT32_MAX, unsigned(client_state(client)), migration);
					report(nullptr,line);
				}
	#endif
			observe(
				[&]
				{
					const auto candidate = find_client_player(client, true);
					if (candidate && !candidate.connected && client_state(client) >= 3)
						connected_player(candidate.player.client_num, read_client_name(client));
				});
		}

		void drop_client(void* client, const char* reason, bool tell_them)
		{
			player_snapshot before{};
			observe(
				[&]
				{
					const auto player = find_client_player(client, true);
					if (player && client_state(client) >= 2) before = disconnecting_player(player.player.client_num);
				});
			original_drop_client(client, reason, tell_them);
			observe(
				[&]
				{
					if (before.entity.generation) invalidate_entity(before.entity.index);
					disconnected_player(before);
				});
		}

		void free_client(void* client, const char* reason)
		{
			player_snapshot before{};
			observe(
				[&]
				{
					const auto player = find_client_player(client, true);
					if (player) before = disconnecting_player(player.player.client_num);
				});
			original_free_client(client, reason);
			observe(
				[&]
				{
					if (before.entity.generation) invalidate_entity(before.entity.index);
					disconnected_player(before);
				});
		}

		void client_spawn(void* entity, const float* origin, const float* angles)
		{
	#ifdef IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE
				if (island_loadout_trace_enabled())
				{
					char line[230]{};
					std::snprintf(line, sizeof(line),
						"IslandLoadoutTrace actual_native_ClientSpawn_enter slot=%u origin=(%.1f %.1f %.1f)",
						player_entity_index(entity), origin ? origin[0] : 0.0f,
						origin ? origin[1] : 0.0f, origin ? origin[2] : 0.0f);
					report(nullptr,line);
				}
	#endif
	#ifdef IW8X_NATIVE_ISLAND_PREMATCH_SPAWN_BRIDGE
				// This callback receives the selected GSC position *before* the
				// engine commits ClientSpawn. Restore the original Island's
				// high-altitude insertion position only for a genuine bad GSC
				// fallback. No fake spawns on other maps or on valid coordinates.
				float island_position[3]{};
				const auto client = player_entity_index(entity);
				const float* selected = client != UINT32_MAX &&
					dedicated::detail::RestoreIslandOriginalSpawnAtNativeEntry(client, origin, island_position)
					? island_position : origin;
				original_client_spawn(entity, selected, angles);
					if (island_loadout_trace_enabled())
						report(nullptr,"IslandLoadoutTrace actual_native_ClientSpawn_returned");
	#else
				original_client_spawn(entity, origin, angles);
	#endif
			observe(
				[&]
				{
					const auto index = player_entity_index(entity);
					if (index != UINT32_MAX) spawned_player(index, capture_entity(index));
				});
		}

		void player_killed(void* self, const void* inflictor, void* attacker, int damage, int damage_flags, int means_of_death,
			const void* weapon, bool alternate, const float* point, int hit_location, int ps_time_offset, int animation_duration)
		{
			// This sole retail caller is after all PlayerDie early returns and after
			// turret-owner attribution. Capture before script execution can free self.
			observe(
				[&]
				{
					if (!current_death || current_death->self != self || current_death->committed) return;
					current_death->victim = associate_entity_player(self);
					current_death->killer = find_player(player_entity_index(attacker));
					current_death->reason = static_cast<uint32_t>(means_of_death);
					current_death->committed = true;
				});
			original_player_killed(self, inflictor, attacker, damage, damage_flags, means_of_death, weapon, alternate, point, hit_location,
				ps_time_offset, animation_duration);
		}

		void player_die(void* self, const void* inflictor, void* attacker, int damage, int damage_flags, int means_of_death,
			const void* weapon, bool alternate, const float* point, int hit_location, int ps_time_offset)
		{
			death_context death{ self };
			const auto previous = current_death;
			current_death = &death;
			original_player_die(
				self, inflictor, attacker, damage, damage_flags, means_of_death, weapon, alternate, point, hit_location, ps_time_offset);
			current_death = previous;
			if (death.committed)
				observe(
					[&]
					{
						died_player(death.victim, death.killer, death.reason);
					});
		}

		void shutdown_server(void* globals, const char* reason)
		{
			gameplay_shutdown = runtime_ready() && gameplay_server();
			if (gameplay_shutdown) server_stopping();
			original_shutdown(globals, reason);
		}

		void shutdown_memory(void* app)
		{
			original_shutdown_memory(app);
			if (gameplay_shutdown)
			{
				gameplay_shutdown = false;
				server_stopped();
			}
		}

	}

	const char* observe_client_connect(int index, uint32_t script_id, const void* caller, client_connect_fn original)
	{
		player_snapshot candidate{};
		// The plugin runtime owns this Replay entry. Other callers reinitialize
		// retained clients during restart/migration; they do not create a new identity.
		if (caller == game::at(game::addresses::gsc_api::DirectConnectGameConnectReturn) && gameplay_server())
			observe(
				[&]
				{
					if (client_at(index)) candidate = connecting_player(index, {});
				});
		const auto rejection = original(index, script_id);
		if (rejection && candidate)
			observe(
				[&]
				{
					disconnect_player(index);
				});
		return rejection;
	}

	bool runtime_ready() noexcept
	{
		return ready.load(std::memory_order_acquire);
	}

	namespace
	{
		const char* connect(int index, uint32_t script_id)
		{
			return observe_client_connect(index, script_id, _ReturnAddress(), original_connect);
		}
	}

	hook::Status install_runtime()
	{
		std::lock_guard lock(install_mutex);
		if (runtime_ready()) return hook::Status::Installed;
		if (!game::initialized()) return hook::Status::NotReady;
		namespace native = game::addresses::gsc_api;

		struct entry
		{
			uintptr_t rva;
			std::span<const uint8_t> bytes;
			void* callback;
		};
#define REPLAY_HOOK(name, callback) entry{native::name, native::name##_entry, reinterpret_cast<void*>(&callback)}
		const entry entries[]{ REPLAY_HOOK(G_MainMP_RunFrame, frame), REPLAY_HOOK(GScr_FreeEntity, free_entity),
			REPLAY_HOOK(Scr_BeginLoadScripts, begin), REPLAY_HOOK(SV_ClientMP_DropClient, drop_client),
			REPLAY_HOOK(G_ActiveMP_ClientSpawn, client_spawn), REPLAY_HOOK(G_CombatMP_PlayerDie, player_die),
			REPLAY_HOOK(SV_SendConnectResponseToClient, connect_response), REPLAY_HOOK(SV_ClientMP_FreeClient, free_client),
			REPLAY_HOOK(Scr_PlayerKilled, player_killed), REPLAY_HOOK(Persistent_ServerShutdown, shutdown_server),
			REPLAY_HOOK(App_ServerShutdown, shutdown_memory), REPLAY_HOOK(Scr_EndLoadScripts, end_scripts),
			REPLAY_HOOK(G_ClientMP_Connect, connect), REPLAY_HOOK(ServerStart_Finalize, finalize_startup) };
#undef REPLAY_HOOK
		static_assert(std::size(entries) == std::tuple_size_v<decltype(targets)>);
		// Share MinHook's existing installation mutex. Every original is published
		// before one queued enable operation makes these callbacks reachable.
		std::lock_guard hook_lock(hook::detail::Mutex());
		for (size_t i = 0; i < std::size(entries); ++i)
		{
			if (trampolines[i]) continue; // Prepared detours survive a retry.
			const auto& e = entries[i];
			if (e.rva >= game::image_size() || e.bytes.size() > game::image_size() - e.rva) return hook::Status::Failed;
			targets[i] = game::at(e.rva);
			const auto status = hook::detail::Prepare(targets[i], e.callback, e.bytes.data(), e.bytes.size(), &trampolines[i]);
			if (status != hook::Status::Installed) return status;
		}
		original_frame = reinterpret_cast<frame_fn>(trampolines[0]);
		original_free = reinterpret_cast<free_fn>(trampolines[1]);
		original_begin = reinterpret_cast<begin_fn>(trampolines[2]);
		original_drop_client = reinterpret_cast<drop_client_fn>(trampolines[3]);
		original_client_spawn = reinterpret_cast<client_spawn_fn>(trampolines[4]);
		original_player_die = reinterpret_cast<player_die_fn>(trampolines[5]);
		original_response = reinterpret_cast<response_fn>(trampolines[6]);
		original_free_client = reinterpret_cast<free_client_fn>(trampolines[7]);
		original_player_killed = reinterpret_cast<player_killed_fn>(trampolines[8]);
		original_shutdown = reinterpret_cast<shutdown_fn>(trampolines[9]);
		original_shutdown_memory = reinterpret_cast<free_fn>(trampolines[10]);
		original_end_scripts = reinterpret_cast<end_scripts_fn>(trampolines[11]);
		original_connect = reinterpret_cast<client_connect_fn>(trampolines[12]);
		original_finalize = reinterpret_cast<free_fn>(trampolines[13]);
		engine_bindings runtime{};
		runtime.capabilities = IW8X_CAP_FUNCTIONS | IW8X_CAP_METHODS | IW8X_CAP_ENTITIES | IW8X_CAP_NOTIFY | IW8X_CAP_GAME_TASKS |
			IW8X_CAP_ASYNC | IW8X_CAP_DVARS | IW8X_CAP_PLAYERS;
		runtime.valid = &validate_entity;
		runtime.notify = &notify_native;
		runtime.reserve_native = &reserve_native;
		runtime.invalidate_entities = &invalidate_entities;
		runtime.validate_return = &validate_return;
		runtime.get_dvar_string = &read_dvar_string;
		runtime.player_valid = &validate_player;
		runtime.player_name = &read_player_name;
		runtime.player_entity = &read_player_entity;
		if (typed_dvars_available())
		{
			runtime.capabilities |= IW8X_CAP_TYPED_DVARS;
			runtime.get_dvar = &read_typed_dvar;
		}
		if (invocation_available())
		{
			runtime.capabilities |= IW8X_CAP_GSC_CALL | IW8X_CAP_RICH_VALUES;
			runtime.call_builtin = &call_builtin_native;
			if (script_invocation_available())
			{
				runtime.capabilities |= IW8X_CAP_SCRIPT_CALL;
				runtime.call_script = &call_script_native;
				runtime.call_script_on = &call_script_on_native;
				runtime.resolve_script = &resolve_script_native;
			}
		}
		if (chat_available())
		{
			runtime.capabilities |= IW8X_CAP_CHAT;
			runtime.broadcast_chat = &broadcast_chat;
			runtime.tell_chat = &tell_chat;
		}
		configure_bindings(runtime);
		if (!hook::detail::EnableBatch(targets)) return hook::Status::Failed;
#ifdef IW8X_ISLAND_PORT_PROBE
			char enable[2]{};
				char heartbeat[2]{};
				island_frame_heartbeat_enabled =
					GetEnvironmentVariableA("IW8X_GSC_FRAME_HEARTBEAT", heartbeat, sizeof(heartbeat)) == 1 &&
					heartbeat[0] == '1';
				if (island_frame_heartbeat_enabled)
					report(nullptr, "Island GSC frame heartbeat enabled: every engine-frame metrics window will publish, including healthy windows");
			if (GetEnvironmentVariableA("IW8X_GSC_VM_PROFILE", enable, sizeof(enable)) == 1 && enable[0] == '1')
			{
				void* vm_original{};
				auto* vm_target = game::at(native::VM_Execute);
				if (hook::detail::Prepare(vm_target, reinterpret_cast<void*>(&vm_execute),
					vm_execute_entry, sizeof(vm_execute_entry), &vm_original) == hook::Status::Installed)
				{
					original_vm_execute = reinterpret_cast<vm_execute_fn>(vm_original);
					vm_profile_enabled = hook::detail::Enable(vm_target);
				}
				if (!vm_profile_enabled)
					report(nullptr, "GSC VM sampling unavailable: stock VM_Execute signature or hook differs");
			}
#endif
		ready.store(true, std::memory_order_release);
		return hook::Status::Installed;
	}
}
