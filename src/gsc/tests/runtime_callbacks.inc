// Included by the instrumented VM fixture; all callbacks are production code.
namespace
{
	unsigned endload_observations{}, runtime_frames{}, runtime_frees{}, native_deaths{};
	unsigned begin_precache{};
	bool reject_connect{}, commit_death{}, release_victim{};
	std::vector<iw8x_event> runtime_events;
	std::vector<std::string> runtime_order;
	int16_t runtime_victim = 5, runtime_killer = 2;
	std::array<float, 3> runtime_point{ 1, 2, 3 };
	int runtime_weapon{};
	uint32_t runtime_handle{};
	iw8x_entity cleanup_capture{};
	iw8x_registration runtime_script{};
	iw8x::plugin_id runtime_owner{};

	void fake_begin(void* ctx, uint8_t user, uint32_t precache)
	{
		expect(ctx == context() && user == 2 && precache == 37, "BeginLoadScripts preserves all three Replay arguments");
		++begin_precache;
		namespace n = game::addresses::gsc_api;
		field<void*>(ctx, n::layout::functions) = game::at(n::native_functions);
		field<void*>(ctx, n::layout::methods) = game::at(n::native_methods);
		field<uint32_t>(ctx, n::layout::function_count) = n::stock_function_count;
		field<uint32_t>(ctx, n::layout::function_end) = n::stock_function_base + n::stock_function_count;
		field<uint32_t>(ctx, n::layout::method_count) = n::stock_method_count;
		field<uint32_t>(ctx, n::layout::method_end) = n::stock_method_base + n::stock_method_count;
	}

	void fake_end(void* ctx)
	{
		uint32_t type{};
		expect(ctx == context() && endload_observations == 1 &&
				core::registered_script_handle(runtime_owner, runtime_script, runtime_handle, type, nullptr) == IW8X_OK && runtime_handle,
			"bot observer and plugin export resolution precede native EndLoadScripts cleanup");
	}

	const char* fake_connect(int index, uint32_t script_id)
	{
		expect(index == 5 && script_id == 101, "native connect keeps index and script ID");
		return reject_connect ? "rejected" : nullptr;
	}

	void fake_response(void* client, const void* address, int migration)
	{
		expect(client == game::at<void*>(game::addresses::gsc_api::clients)[5] && address == &runtime_point && migration == 7,
			"native connection response preserves address and migration arguments");
		static_cast<uint8_t*>(client)[game::addresses::gsc_api::client_state] = 3;
		runtime_order.emplace_back("response");
	}

	void fake_spawn(void* self, const float* origin, const float* angles)
	{
		expect(self == &runtime_victim && origin == runtime_point.data() && angles == runtime_point.data(), "spawn arguments forwarded");
		in_use[5] = 1;
		runtime_order.emplace_back("spawn");
	}

	void fake_free(void* self)
	{
		expect(self == &runtime_victim, "entity free keeps native pointer");
		++runtime_frees;
		if (core::runtime_ready())
		{
			// A capture made by native teardown must be invalidated after it, too.
			cleanup_capture = core::capture_entity(5);
			in_use[5] = 0;
		}
	}

	void fake_killed(void* self, const void* inflictor, void* attacker, int damage, int flags, int reason, const void* weapon,
		bool alternate, const float* point, int hit, int time, int animation)
	{
		expect(self == &runtime_victim && inflictor == &runtime_weapon && attacker == &runtime_killer && damage == 91 && flags == 12 &&
				reason == 13 && weapon == &runtime_weapon && alternate && point == runtime_point.data() && hit == 14 && time == 15 &&
				animation == 16,
			"committed kill preserves twelve native arguments");
		++native_deaths;
		if (release_victim) core::free_entity(self);
	}

	void fake_die(void* self, const void* inflictor, void* attacker, int damage, int flags, int reason, const void* weapon, bool alternate,
		const float* point, int hit, int time)
	{
		expect(self == &runtime_victim && inflictor == &runtime_weapon && attacker == &runtime_victim && damage == 91 && flags == 12 &&
				reason == 13 && weapon == &runtime_weapon && alternate && point == runtime_point.data() && hit == 14 && time == 15,
			"PlayerDie preserves eleven native arguments");
		if (commit_death)
			core::player_killed(self, inflictor, &runtime_killer, damage, flags, reason, weapon, alternate, point, hit, time, 16);
	}

	void fake_free_client(void*, const char* reason)
	{
		expect(std::string_view(reason) == "bye", "free-client reason forwarded");
		runtime_order.emplace_back("free-client");
	}

	void fake_drop(void* client, const char* reason, bool tell)
	{
		expect(tell && std::string_view(reason) == "bye", "drop-client flags forwarded");
		runtime_order.emplace_back("drop");
		core::free_client(client, reason);
	}

	void fake_frame(int32_t time)
	{
		expect(time == 42, "RunFrame time preserved");
		++runtime_frames;
	}

	void fake_finalize(void* app)
	{
		expect(app == &runtime_weapon, "server finalization retains app pointer");
		runtime_order.emplace_back("finalize");
		*game::at<uint32_t>(game::addresses::gsc_api::server_start_state) = 0;
	}

	void fake_shutdown(void* globals, const char* reason)
	{
		expect(globals && std::string_view(reason) == "shutdown", "server shutdown arguments preserved");
		runtime_order.emplace_back("shutdown");
	}

	void fake_shutdown_memory(void* app)
	{
		expect(app == &runtime_weapon, "server memory shutdown keeps app");
		runtime_order.emplace_back("memory");
	}

	void runtime_callbacks()
	{
		namespace n = game::addresses::gsc_api;
		commit(n::server_start_state, sizeof(uint32_t));
		std::array<uint8_t, 0x580> victim_client{}, killer_client{};
		std::array<uint8_t, 0x80> globals{};
		*game::at<uint8_t*>(n::persistent_globals) = globals.data();
		*game::at<uint32_t>(n::client_count) = 6;
		game::at<void*>(n::clients)[5] = victim_client.data();
		game::at<void*>(n::clients)[2] = killer_client.data();
		auto bindings = core::bindings();
		bindings.capabilities |= IW8X_CAP_GAME_TASKS | IW8X_CAP_PLAYERS;
		bindings.invalidate_entities = &core::invalidate_entities;
		bindings.player_valid = &core::validate_player;
		core::configure_bindings(bindings);
		runtime_owner = core::create_owner("native callbacks");
		iw8x::api api(&host, runtime_owner);
		for (auto event : { IW8X_PLAYER_CONNECTING, IW8X_PLAYER_CONNECTED, IW8X_PLAYER_SPAWN, IW8X_PLAYER_DIED, IW8X_PLAYER_KILLED,
				 IW8X_PLAYER_DISCONNECTING, IW8X_PLAYER_DISCONNECTED, IW8X_MAP_READY, IW8X_SERVER_READY, IW8X_SERVER_STOPPING,
				 IW8X_SERVER_STOPPED })
			api.subscribe(event,
				[](const iw8x_event& e)
				{
					runtime_events.push_back(e);
					runtime_order.emplace_back("event:" + std::to_string(e.type));
				});
		core::ready.store(true);
		core::original_begin = &fake_begin;
		core::original_end_scripts = &fake_end;
		core::original_response = &fake_response;
		core::original_client_spawn = &fake_spawn;
		core::original_player_die = &fake_die;
		core::original_player_killed = &fake_killed;
		core::original_free = &fake_free;
		core::original_drop_client = &fake_drop;
		core::original_free_client = &fake_free_client;
		core::original_frame = &fake_frame;
		core::original_finalize = &fake_finalize;
		core::original_shutdown = &fake_shutdown;
		core::original_shutdown_memory = &fake_shutdown_memory;
		core::begin(context(), 2, 37);
		expect(begin_precache == 1 && field<uint32_t>(context(), n::layout::function_count) > n::stock_function_count,
			"native tables are extended after BeginLoadScripts initializes them");
		const auto script = api.gsc().script("scripts/tests/echo", "main");
		runtime_script = script.registration();
		iw8x::gsc::scripts_begin_load();
		export_window = true;
		core::end_scripts(context());
		export_window = false;
		core::ready.store(false);
		core::original_end_scripts = +[](void*)
		{
		};
		core::end_scripts(context());
		expect(endload_observations == 1, "disarmed script hook is native pass-through");
		core::ready.store(true);

		auto* caller = game::at(n::DirectConnectGameConnectReturn);
		core::observe_client_connect(5, 101, nullptr, &fake_connect);
		expect(runtime_events.empty(), "restart/migration connect caller does not create an identity");
		reject_connect = true;
		core::observe_client_connect(5, 101, caller, &fake_connect);
		expect(runtime_events.size() == 1 && !core::find_player(5, true), "native rejection retires prepared player");
		reject_connect = false;
		runtime_events.clear();
		runtime_order.clear();
		core::observe_client_connect(5, 101, caller, &fake_connect);
		core::observe_client_connect(5, 101, caller, &fake_connect);
		core::connect_response(victim_client.data(), &runtime_point, 7);
		core::connect_response(victim_client.data(), &runtime_point, 7);
		expect(runtime_events.size() == 2 && runtime_events[0].type == IW8X_PLAYER_CONNECTING &&
				runtime_events[1].type == IW8X_PLAYER_CONNECTED,
			"accepted connection emits one prepare and one post-response event");
		expect(runtime_order[1] == "response", "connected event follows native response");
		in_use[2] = 1;
		core::prepare_player(2, "killer");
		core::activate_player(2);
		core::associate_player_entity(2, core::capture_entity(2));
		runtime_events.clear();
		runtime_order.clear();
		core::client_spawn(&runtime_victim, runtime_point.data(), runtime_point.data());
		expect(runtime_events.size() == 1 && runtime_events[0].entity.index == 5 && runtime_order[0] == "spawn",
			"spawn callback observes completed native spawn");
		const auto entity = runtime_events[0].entity;
		runtime_events.clear();
		core::player_die(
			&runtime_victim, &runtime_weapon, &runtime_victim, 91, 12, 13, &runtime_weapon, true, runtime_point.data(), 14, 15);
		expect(runtime_events.empty(), "PlayerDie early return does not invent a kill");
		commit_death = true;
		release_victim = true;
		core::player_die(
			&runtime_victim, &runtime_weapon, &runtime_victim, 91, 12, 13, &runtime_weapon, true, runtime_point.data(), 14, 15);
		expect(runtime_events.size() == 2 && runtime_events[0].type == IW8X_PLAYER_DIED && runtime_events[1].type == IW8X_PLAYER_KILLED &&
				runtime_events[1].player.client_num == 2 && runtime_events[0].reason == 13 && !runtime_events[0].entity.generation,
			"committed death keeps engine-attributed killer and clears freed victim entity");
		{
			core::game_scope scope;
			expect(core::validate_entity(entity) == IW8X_STALE, "native free invalidates prior entity handles");
		}
		in_use[5] = 1;
		{
			core::game_scope scope;
			expect(cleanup_capture.generation && core::validate_entity(cleanup_capture) == IW8X_STALE,
				"post-free invalidation retires handles captured during native cleanup even when the slot is reused");
		}
		in_use[5] = 0;
		expect(native_deaths == 1 && runtime_frees == 1 && !core::current_death, "death context is restored after native call");
		runtime_events.clear();
		core::drop_client(victim_client.data(), "bye", true);
		expect(runtime_events.size() == 2 && runtime_events[0].type == IW8X_PLAYER_DISCONNECTING &&
				runtime_events[1].type == IW8X_PLAYER_DISCONNECTED && !core::find_player(5, true),
			"nested drop/free emits disconnect once");

		runtime_events.clear();
		runtime_order.clear();
		unsigned tasks{};
		api.game_thread(
			[&]
			{
				++tasks;
			});
		*game::at<uint32_t>(n::server_start_state) = 2;
		core::frame(42);
		expect(!tasks && runtime_events.empty(), "startup frames cannot drain plugin work before finalization");
		core::finalize_startup(&runtime_weapon);
		expect(runtime_events.empty() && runtime_order[0] == "finalize", "finalization signals readiness without inline plugin delivery");
		globals[n::persistent_frontend] = 1;
		core::frame(42);
		expect(!tasks && runtime_events.empty(), "frontend frame cannot deliver gameplay readiness or tasks");
		globals[n::persistent_frontend] = 0;
		field<uint32_t>(context(), old::layout::frame_count) = 1;
		core::frame(42);
		expect(!tasks && runtime_events.empty(), "active VM frame defers plugin execution");
		field<uint32_t>(context(), old::layout::frame_count) = 0;
		core::frame(42);
		expect(tasks == 1 && runtime_events.size() == 2 && runtime_events[0].type == IW8X_MAP_READY &&
				runtime_events[1].type == IW8X_SERVER_READY,
			"idle finalized game frame delivers readiness then queued work once");
		core::frame(42);
		expect(tasks == 1 && runtime_events.size() == 2, "readiness does not repeat every frame");
		runtime_events.clear();
		runtime_order.clear();
		core::shutdown_server(globals.data(), "shutdown");
		expect(runtime_events.size() == 1 && runtime_order[0] == "event:" + std::to_string(IW8X_SERVER_STOPPING),
			"stopping event precedes native teardown");
		core::shutdown_memory(&runtime_weapon);
		expect(runtime_events.size() == 2 && runtime_events[1].type == IW8X_SERVER_STOPPED && runtime_order[2] == "memory",
			"stopped event follows native memory shutdown");
		core::ready.store(false);
		in_use[5] = 1;
		const auto before = core::capture_entity(5);
		core::free_entity(&runtime_victim);
		expect(core::capture_entity(5).generation == before.generation, "disarmed entity observer cannot invalidate identities");
		expect(core::unload_owner(runtime_owner) == IW8X_OK, "native callback fixture owner unloads");
		*game::at<uint32_t>(n::client_count) = 0;
		game::at<void*>(n::clients)[5] = nullptr;
		game::at<void*>(n::clients)[2] = nullptr;
		*game::at<uint8_t*>(n::persistent_globals) = nullptr;
		core::invalidate_players();
	}
}

namespace dedicated
{
	void ObserveScriptEndLoad(void*)
	{
		++endload_observations;
	}
}

namespace console
{
	void Print(const char*, ...)
	{
	}
}
