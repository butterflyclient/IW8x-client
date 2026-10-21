#include "plugin/sdk/gsc.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{

	struct totals
	{
		std::atomic<uint32_t> pass{};
		std::atomic<uint32_t> fail{};
		std::atomic<uint32_t> skip{};
	};

	totals results;
	std::atomic<uint32_t> lifecycle_seen[IW8X_EVENT_COUNT]{};

	void log(const iw8x::api& api, std::string_view text)
	{
		api.raw().log(api.owner(), iw8x::gsc::string_view(text));
	}

	void pass(const iw8x::api& api, std::string_view name)
	{
		++results.pass;
		log(api, "[PASS] " + std::string(name));
	}

	void fail(const iw8x::api& api, std::string_view name, std::string_view why)
	{
		++results.fail;
		log(api, "[FAIL] " + std::string(name) + " -- " + std::string(why));
	}

	void skip(const iw8x::api& api, std::string_view name, std::string_view why)
	{
		++results.skip;
		log(api, "[SKIP] " + std::string(name) + " -- " + std::string(why));
	}

	template <class F>
	void test(const iw8x::api& api, std::string_view name, F&& body)
	{
		log(api, "[BEGIN] " + std::string(name));
		try
		{
			body();
			pass(api, name);
		}
		catch (const std::exception& e)
		{
			fail(api, name, e.what());
		}
		catch (...)
		{
			fail(api, name, "unknown exception");
		}
	}

	void require(bool condition, std::string_view message = "requirement failed")
	{
		if (!condition) throw std::runtime_error(std::string(message));
	}

	void expect_status(iw8x_status expected, const auto& action)
	{
		try
		{
			action();
		}
		catch (const iw8x::api_error& e)
		{
			if (e.code() == expected) return;
			throw;
		}
		throw std::runtime_error("expected API error status " + std::to_string(expected));
	}

	void vm_sanity(const iw8x::api& api)
	{
		require(std::abs(api.gsc().call("sqrt", 16.0f).as<float>() - 4.0f) < 0.001f, "sqrt sanity failed");
		require(api.gsc().call("tolower", "OK").as<std::string>() == "ok", "string sanity failed");
	}

	void summary(const iw8x::api& api, std::string_view phase)
	{
		log(api,
			"[SUMMARY] " + std::string(phase) + " pass=" + std::to_string(results.pass.load()) +
				" fail=" + std::to_string(results.fail.load()) + " skip=" + std::to_string(results.skip.load()));
	}

	constexpr uint64_t acceptance_handle_type = 0x4957385854455354ull; // "IW8XTEST"

	struct handle_resource
	{
		uint64_t magic = 0x1122334455667788ull;
	};

	void destroy_handle(void* resource)
	{
		delete static_cast<handle_resource*>(resource);
	}

	void run_handle_test(const iw8x::api& api)
	{
		const auto& host = api.raw();
		auto* resource = new handle_resource{};
		iw8x_native_handle handle{};
		const auto created = host.create_handle(api.owner(), acceptance_handle_type, IW8X_SCOPE_PLUGIN, resource, &destroy_handle, &handle);
		if (created != IW8X_OK)
		{
			delete resource;
			iw8x::check(created);
		}

		struct borrow_context
		{
			const iw8x::plugin_host* host;
			iw8x::plugin_id owner;
			iw8x_native_handle handle;
			bool saw_resource{};
			bool release_was_busy{};
		} state{ &host, api.owner(), handle };

		iw8x::check(host.with_handle(
			api.owner(), handle, acceptance_handle_type,
			[](void* data, void* raw)
			{
				auto& state = *static_cast<borrow_context*>(data);
				const auto* resource = static_cast<const handle_resource*>(raw);
				state.saw_resource = resource && resource->magic == 0x1122334455667788ull;
				state.release_was_busy = state.host->release_handle(state.owner, state.handle, acceptance_handle_type) == IW8X_BUSY;
			},
			&state));

		require(state.saw_resource, "borrow returned wrong resource");
		require(state.release_was_busy, "borrowed handle was releasable");
		iw8x::check(host.release_handle(api.owner(), handle, acceptance_handle_type));
		require(host.release_handle(api.owner(), handle, acceptance_handle_type) == IW8X_STALE, "released handle did not become stale");
	}

	void run_core(const iw8x::api& api)
	{
		log(api, "PLUGIN_API_TEST BEGIN core");

		test(api, "abi.version",
			[&]
			{
				require(api.raw().version == IW8X_PLUGIN_API_VERSION);
				require(api.raw().size >= sizeof(iw8x_plugin_host));
			});

		test(api, "capabilities.core",
			[&]
			{
				constexpr uint64_t needed =
					IW8X_CAP_GSC_CALL | IW8X_CAP_RICH_VALUES | IW8X_CAP_TYPED_DVARS | IW8X_CAP_COMMANDS | IW8X_CAP_GAME_TASKS;
				require((api.raw().capabilities() & needed) == needed, "missing required capability");
			});

		test(api, "gsc.zero_args",
			[&]
			{
				require(api.gsc().call("gettime").as<int32_t>() >= 0);
			});

		test(api, "gsc.float",
			[&]
			{
				require(std::abs(api.gsc().call("sqrt", 9.0f).as<float>() - 3.0f) < 0.001f);
			});

		test(api, "gsc.string",
			[&]
			{
				require(api.gsc().call("tolower", std::string("TeMpOrArY")).as<std::string>() == "temporary");
			});

		test(api, "gsc.vector",
			[&]
			{
				const auto v = api.gsc().call("vectornormalize", iw8x::vec3{ 3, 0, 0 }).as<iw8x::vec3>();
				require(std::abs(v.x - 1.0f) < 0.001f && std::abs(v.y) < 0.001f && std::abs(v.z) < 0.001f);
			});

		test(api, "gsc.array.engine_to_cpp",
			[&]
			{
				const auto tokens = api.gsc().call("strtok", "one,two,three", ",").as_array();
				require(tokens.size() == 3);
				require(tokens[0].as<std::string>() == "one");
				require(tokens[1].as<std::string>() == "two");
				require(tokens[2].as<std::string>() == "three");
			});

		test(api, "gsc.array.cpp_to_engine",
			[&]
			{
				const auto keys =
					api.gsc().call("getarraykeys", iw8x::array{ 12, "owned", iw8x::array{ true, iw8x::vec3{ 1, 2, 3 } } }).as_array();
				require(keys.size() == 3);
				int mask{};
				for (const auto& key : keys)
				{
					const auto index = key.as<int32_t>();
					require(index >= 0 && index < 3, "unexpected array key");
					mask |= 1 << index;
				}
				require(mask == 7, "missing array key");
			});

		test(api, "gsc.struct.cpp_to_engine",
			[&]
			{
				require(api.gsc()
						.call("isstruct",
							iw8x::struct_value{
								{ iw8x::canonical_field(0x2c5), 123 }, { iw8x::canonical_field(0xd4), iw8x::array{ "nested", 7 } } })
						.as<bool>());
			});

		test(api, "gsc.struct.engine_to_cpp",
			[&]
			{
				require(api.gsc().call("spawnstruct").as_struct().empty());
			});

		test(api, "gsc.method_and_stale_entity",
			[&]
			{
				const auto entity = api.gsc().call("spawn", "script_origin", iw8x::vec3{ 0, 0, 0 }).as<iw8x_entity>();
				const auto origin = api.gsc().method(entity, "getorigin").as<iw8x::vec3>();
				require(std::abs(origin.x) < 0.001f && std::abs(origin.y) < 0.001f && std::abs(origin.z) < 0.001f);
				api.gsc().method(entity, "delete");
				expect_status(IW8X_STALE,
					[&]
					{
						api.gsc().method(entity, "getorigin");
					});
			});

		test(api, "gsc.invalid_function",
			[&]
			{
				expect_status(IW8X_NOT_FOUND,
					[&]
					{
						api.gsc().call("iw8x_function_that_does_not_exist");
					});
				vm_sanity(api);
			});

		test(api, "gsc.script_error_cleanup",
			[&]
			{
				expect_status(IW8X_CALLBACK_ERROR,
					[&]
					{
						api.gsc().call("sqrt");
					});
				vm_sanity(api);
			});

		test(api, "dvar.readable_string_bool",
			[&]
			{
				const auto string_value = api.dvars().get_string("cl_textChatEnabled");
				const auto bool_value = api.dvars().get_bool("cl_textChatEnabled");
				require(string_value == (bool_value ? "1" : "0") || !string_value.empty(), "inconsistent dvar value");
			});

		test(api, "dvar.readable_int",
			[&]
			{
				require(api.dvars().get_int("ui_maxclients") > 0);
			});

		test(api, "dvar.readable_float",
			[&]
			{
				require(std::isfinite(api.dvars().get_float("timescale")));
			});

		test(api, "dvar.hashed_fallback",
			[&]
			{
				require(api.dvars().get_bool("NLPSQMQRTP") == api.dvars().get_bool("cl_textChatEnabled"));
			});

		test(api, "dvar.unknown",
			[&]
			{
				expect_status(IW8X_NOT_FOUND,
					[&]
					{
						(void)api.dvars().get_string("iw8x_missing_dvar_for_acceptance");
					});
			});

		test(api, "players.enumeration",
			[&]
			{
				for (const auto& player : api.players())
					require(player.valid(), "enumeration returned stale player");
			});

		test(api, "handles.create_borrow_release",
			[&]
			{
				run_handle_test(api);
			});

		test(api, "scheduler.cancel_pending",
			[&]
			{
				auto ran = std::make_shared<std::atomic<bool>>(false);
				const auto token = api.game_thread(
					[ran]
					{
						ran->store(true);
					},
					250, IW8X_SCOPE_PLUGIN);
				require(api.raw().cancel_task(api.owner(), token) == IW8X_OK, "cancel failed");
				require(!ran->load(), "task ran before cancellation");
			});

		test(api, "vm.final_sanity",
			[&]
			{
				vm_sanity(api);
			});

		summary(api, "core");

		// Async is inherently deferred. It reports independently when the worker
		// and game-thread completion have both run.
		try
		{
			api.worker(
				[]
				{
					return std::string("worker-result");
				},
				[api](std::string result)
				{
					if (result == "worker-result")
						pass(api, "async.worker_completion");
					else
						fail(api, "async.worker_completion", "wrong result");
					summary(api, "core+async");
				},
				IW8X_SCOPE_PLUGIN);
		}
		catch (const std::exception& e)
		{
			fail(api, "async.worker_schedule", e.what());
		}
	}

	void run_players(const iw8x::api& api)
	{
		log(api, "PLUGIN_API_TEST BEGIN players");
		const auto players = api.players();
		if (players.empty())
		{
			skip(api, "players.connected", "no connected players");
			skip(api, "chat.broadcast", "no connected player for visual confirmation");
			skip(api, "chat.tell", "no connected player for visual confirmation");
			summary(api, "players");
			return;
		}

		for (const auto& player : players)
		{
			const auto prefix = "player[" + std::to_string(player.client_num()) + "].";
			test(api, prefix + "valid",
				[&]
				{
					require(player.valid());
				});
			test(api, prefix + "name",
				[&]
				{
					(void)player.name();
				});
			test(api, prefix + "entity",
				[&]
				{
					const auto entity = player.entity();
					if (!entity) throw std::runtime_error("connected player has no entity");
					(void)api.gsc().method(*entity, "getorigin").as<iw8x::vec3>();
				});
			try
			{
				(void)player.xuid();
				pass(api, prefix + "xuid");
			}
			catch (const iw8x::api_error& e)
			{
				if (e.code() == IW8X_UNSUPPORTED)
					skip(api, prefix + "xuid", "host does not expose persistent XUID yet");
				else
					fail(api, prefix + "xuid", e.what());
			}
		}

		if ((api.raw().capabilities() & IW8X_CAP_CHAT) != 0)
		{
			test(api, "chat.broadcast_transport",
				[&]
				{
					api.broadcast("^2IW8x API test:^7 broadcast transport OK");
				});
			test(api, "chat.tell_transport",
				[&]
				{
					api.tell(players.front(), "^3IW8x API test:^7 private tell transport OK");
				});
			skip(api, "chat.visual_render", "confirm both messages appeared in the connected client's stock text chat");
		}
		else
		{
			skip(api, "chat.broadcast_transport", "IW8X_CAP_CHAT unavailable");
			skip(api, "chat.tell_transport", "IW8X_CAP_CHAT unavailable");
		}

		summary(api, "players");
	}

	struct stress_state
	{
		iw8x::api api;
		uint32_t phase{};
		uint32_t done{};
		uint32_t next_progress{ 25 };
	};

	void run_stress(const iw8x::api& api)
	{
		log(api, "PLUGIN_API_TEST BEGIN stress (chunked; server remains responsive)");

		auto state = std::make_shared<stress_state>(stress_state{ api });
		auto step = std::make_shared<std::function<void()>>();
		const std::weak_ptr<std::function<void()>> weak_step = step;

		*step = [state, weak_step]
		{
			const auto schedule_next = [&]
			{
				if (auto next = weak_step.lock())
					state->api.game_thread(
						[next]
						{
							(*next)();
						},
						0, IW8X_SCOPE_PLUGIN);
			};

			try
			{
				if (state->phase == 0)
				{
					if (!state->done) log(state->api, "[BEGIN] stress.gsc_primitive_2000");

					const auto end = (std::min)(state->done + 100u, 2000u);
					for (; state->done < end; ++state->done)
						require(std::abs(state->api.gsc().call("sqrt", 16.0f).as<float>() - 4.0f) < 0.001f);

					const auto percent = state->done * 100u / 2000u;
					if (percent >= state->next_progress)
					{
						log(state->api, "[PROGRESS] stress.gsc_primitive_2000 " + std::to_string(percent) + "%");
						state->next_progress += 25;
					}
					if (state->done < 2000) return schedule_next();

					pass(state->api, "stress.gsc_primitive_2000");
					state->phase = 1;
					state->done = 0;
					state->next_progress = 25;
					return schedule_next();
				}

				if (state->phase == 1)
				{
					if (!state->done) log(state->api, "[BEGIN] stress.collections_1000");

					const auto end = (std::min)(state->done + 25u, 1000u);
					for (; state->done < end; ++state->done)
					{
						const auto keys = state->api.gsc().call("getarraykeys", iw8x::array{ "repeat", iw8x::array{ 1, 2, 3 } }).as_array();
						require(keys.size() == 2, "collection result changed during stress");
					}

					const auto percent = state->done * 100u / 1000u;
					if (percent >= state->next_progress)
					{
						log(state->api, "[PROGRESS] stress.collections_1000 " + std::to_string(percent) + "%");
						state->next_progress += 25;
					}
					if (state->done < 1000) return schedule_next();

					pass(state->api, "stress.collections_1000");
					state->phase = 2;
					state->done = 0;
					state->next_progress = 25;
					return schedule_next();
				}

				if (state->phase == 2)
				{
					if (!state->done) log(state->api, "[BEGIN] stress.dvars_2000");

					const auto end = (std::min)(state->done + 100u, 2000u);
					for (; state->done < end; ++state->done)
					{
						(void)state->api.dvars().get_int("ui_maxclients");
						(void)state->api.dvars().get_float("timescale");
					}

					const auto percent = state->done * 100u / 2000u;
					if (percent >= state->next_progress)
					{
						log(state->api, "[PROGRESS] stress.dvars_2000 " + std::to_string(percent) + "%");
						state->next_progress += 25;
					}
					if (state->done < 2000) return schedule_next();

					pass(state->api, "stress.dvars_2000");
					state->phase = 3;
					state->done = 0;
					return schedule_next();
				}

				log(state->api, "[BEGIN] stress.final_vm_sanity");
				vm_sanity(state->api);
				pass(state->api, "stress.final_vm_sanity");
				summary(state->api, "stress");
			}
			catch (const std::exception& e)
			{
				const char* names[] = { "stress.gsc_primitive_2000", "stress.collections_1000", "stress.dvars_2000",
					"stress.final_vm_sanity" };
				fail(state->api, names[(std::min)(state->phase, 3u)], e.what());
				summary(state->api, "stress-aborted");
			}
			catch (...)
			{
				fail(state->api, "stress", "unknown exception");
				summary(state->api, "stress-aborted");
			}
		};

		api.game_thread(
			[step]
			{
				(*step)();
			},
			0, IW8X_SCOPE_PLUGIN);
	}

	const char* event_name(uint32_t type)
	{
		switch (type)
		{
		case IW8X_SCRIPTS_BEGIN_LOAD:
			return "SCRIPTS_BEGIN_LOAD";
		case IW8X_SCRIPTS_LOADED:
			return "SCRIPTS_LOADED";
		case IW8X_SCRIPTS_SHUTDOWN:
			return "SCRIPTS_SHUTDOWN";
		case IW8X_MAP_STARTED:
			return "MAP_STARTED";
		case IW8X_MAP_SHUTDOWN:
			return "MAP_SHUTDOWN";
		case IW8X_MAP_RESTART:
			return "MAP_RESTART";
		case IW8X_PLAYER_CONNECTED:
			return "PLAYER_CONNECTED";
		case IW8X_PLAYER_DISCONNECTED:
			return "PLAYER_DISCONNECTED";
		case IW8X_PLAYER_SPAWNED:
			return "PLAYER_SPAWNED";
		case IW8X_PLAYER_KILLED:
			return "PLAYER_KILLED";
		case IW8X_SERVER_STARTED:
			return "SERVER_STARTED";
		case IW8X_SERVER_STOPPING:
			return "SERVER_STOPPING";
		case IW8X_MAP_STARTING:
			return "MAP_STARTING";
		case IW8X_PLAYER_CONNECTING:
			return "PLAYER_CONNECTING";
		case IW8X_PLAYER_DISCONNECTING:
			return "PLAYER_DISCONNECTING";
		case IW8X_PLAYER_DIED:
			return "PLAYER_DIED";
		case IW8X_SERVER_STARTING:
			return "SERVER_STARTING";
		case IW8X_SERVER_STOPPED:
			return "SERVER_STOPPED";
		default:
			return "UNKNOWN";
		}
	}

	void install_lifecycle_observers(const iw8x::api& api)
	{
		const auto supported = api.raw().supported_events();
		for (uint32_t type = 0; type < IW8X_EVENT_COUNT; ++type)
		{
			if ((supported & (1ull << type)) == 0) continue;
			api.subscribe(type,
				[api, type](const iw8x_event& event)
				{
					++lifecycle_seen[type];
					log(api, std::string("[EVENT] ") + event_name(type) + " epoch=" + std::to_string(event.epoch));
				});
		}
	}

	void print_lifecycle(const iw8x::api& api)
	{
		const auto supported = api.raw().supported_events();
		log(api, "PLUGIN_API_TEST lifecycle counters:");
		for (uint32_t type = 0; type < IW8X_EVENT_COUNT; ++type)
		{
			if ((supported & (1ull << type)) == 0) continue;
			log(api, std::string("  ") + event_name(type) + "=" + std::to_string(lifecycle_seen[type].load()));
		}
		summary(api, "current");
	}

	void load_error(const iw8x::plugin_host* host, iw8x::plugin_id owner, std::string_view message) noexcept
	{
		if (!host || host->version != IW8X_PLUGIN_API_VERSION || host->size < offsetof(iw8x::plugin_host, log) + sizeof(host->log) ||
			!host->log)
			return;
		host->log(owner, iw8x::gsc::string_view(message));
	}

} // namespace

extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host, iw8x::plugin_id owner) noexcept
{
	try
	{
		iw8x::api api(host, owner);
		const auto caps = host->capabilities();
		constexpr uint64_t required = IW8X_CAP_COMMANDS | IW8X_CAP_GAME_TASKS | IW8X_CAP_ASYNC | IW8X_CAP_GSC_CALL | IW8X_CAP_RICH_VALUES |
			IW8X_CAP_TYPED_DVARS | IW8X_CAP_PLAYERS;
		if ((caps & required) != required)
		{
			load_error(host, owner,
				"Plugin API acceptance test requires COMMANDS, GAME_TASKS, ASYNC, GSC_CALL, RICH_VALUES, TYPED_DVARS and PLAYERS");
			return 0;
		}

		install_lifecycle_observers(api);

		// Registration itself exercises the append-only native registration
		// surface. Invoke these from GSC when testing script -> native calls.
		api.namespace_("acceptance")
			.function("echo",
				[](iw8x::gsc::context& ctx)
				{
					if (!ctx.argument_count()) return ctx.return_undefined();
					ctx.return_value(ctx.argument<iw8x::value>(0));
				});
		api.namespace_("acceptance")
			.method("self_index",
				[](iw8x::gsc::context& ctx, iw8x::gsc::entity self)
				{
					ctx.return_value(static_cast<int>(self.ref().index));
				});

		api.command("iw8x_test_api",
			[api](const iw8x_string*, uint32_t)
			{
				try
				{
					log(api, "[QUEUED] iw8x_test_api");
					api.game_thread(
						[api]
						{
							run_core(api);
						},
						0, IW8X_SCOPE_PLUGIN);
				}
				catch (const std::exception& e)
				{
					fail(api, "core.schedule", e.what());
				}
			});

		api.command("iw8x_test_players",
			[api](const iw8x_string*, uint32_t)
			{
				try
				{
					log(api, "[QUEUED] iw8x_test_players");
					api.game_thread(
						[api]
						{
							run_players(api);
						},
						0, IW8X_SCOPE_PLUGIN);
				}
				catch (const std::exception& e)
				{
					fail(api, "players.schedule", e.what());
				}
			});

		api.command("iw8x_test_stress",
			[api](const iw8x_string*, uint32_t)
			{
				try
				{
					log(api, "[QUEUED] iw8x_test_stress");
					run_stress(api);
				}
				catch (const std::exception& e)
				{
					fail(api, "stress.schedule", e.what());
				}
			});

		api.command("iw8x_test_results",
			[api](const iw8x_string*, uint32_t)
			{
				log(api, "[RUN] iw8x_test_results");
				print_lifecycle(api);
			});

		log(api, "Plugin API acceptance loaded. Commands: iw8x_test_api, iw8x_test_players, iw8x_test_stress, iw8x_test_results");
		return 1;
	}
	catch (const std::exception& e)
	{
		load_error(host, owner, e.what());
		return 0;
	}
	catch (...)
	{
		load_error(host, owner, "unknown plugin load failure");
		return 0;
	}
}

extern "C" __declspec(dllexport) void IW8X_PluginUnload() noexcept
{
}
