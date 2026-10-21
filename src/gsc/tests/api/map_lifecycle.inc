// Included once inside the existing instrumented test fixture.
void server_map_lifecycle()
{
	const auto owner = core::create_owner("server lifecycle");
	api api(&host, owner);
	std::vector<iw8x_event> events;
	bool ready_on_server = true, shutdown_vm_denied = true;
	const uint32_t types[]{ IW8X_SERVER_STARTING, IW8X_MAP_STARTING, IW8X_MAP_READY, IW8X_SERVER_READY, IW8X_MAP_RESTART,
		IW8X_SERVER_STOPPING, IW8X_MAP_SHUTDOWN, IW8X_SERVER_STOPPED };
	uint64_t expected_mask = (1ull << IW8X_SCRIPTS_BEGIN_LOAD) | (1ull << IW8X_SCRIPTS_LOADED) | (1ull << IW8X_SCRIPTS_SHUTDOWN);
	for (auto type : types)
	{
		expected_mask |= 1ull << type;
		api.subscribe(type,
			[&](const iw8x_event& e)
			{
				events.push_back(e);
				if (e.type == IW8X_MAP_READY || e.type == IW8X_SERVER_READY)
					ready_on_server = ready_on_server && core::in_game_scope();
				else
					shutdown_vm_denied = shutdown_vm_denied && !core::in_game_scope();
			});
	}
	for (auto type : { IW8X_PLAYER_CONNECTING, IW8X_PLAYER_CONNECTED, IW8X_PLAYER_DISCONNECTING, IW8X_PLAYER_DISCONNECTED,
			 IW8X_PLAYER_SPAWN, IW8X_PLAYER_DEATH, IW8X_PLAYER_KILL })
		expected_mask |= 1ull << type;
	expect(host.supported_events() == expected_mask, "supported event mask contains precisely proven boundaries");
	scripts_begin_load();
	scripts_loaded();
	scripts_shutdown();
	expect(events.empty(), "script callbacks do not invent server/map lifecycle");
	server_starting();
	map_starting();
	scripts_begin_load();
	scripts_loaded();
	const auto epoch = events.front().epoch;
	auto signal = std::async(std::launch::async,
		[]
		{
			map_ready();
			server_ready();
		});
	signal.get();
	expect(events.size() == 2, "readiness observer only queues, never executes on worker/main poll thread");
		const auto restart_pumps_before = dedicated_restart_vm_pumps.load(std::memory_order_relaxed);
	pump_game();
	pump_game();
		expect(dedicated_restart_vm_pumps.load(std::memory_order_relaxed) == restart_pumps_before + 2 &&
			dedicated_restart_scope_valid.load(std::memory_order_relaxed),
			"dedicated map_restart integration is pumped from verified server VM scope");
	expect(events.size() == 4 && events[2].type == IW8X_MAP_READY && events[3].type == IW8X_SERVER_READY && events[1].epoch == epoch &&
			events[2].epoch == epoch && events[3].epoch == epoch,
		"readiness delivered once in map then server order with independent lifecycle epoch");
	map_restart();
	server_stopping();
	map_shutdown();
	scripts_shutdown();
	server_stopped();
	expect(events.size() == 8 && events[4].type == IW8X_MAP_RESTART && events[5].type == IW8X_SERVER_STOPPING &&
			events[6].type == IW8X_MAP_SHUTDOWN && events[7].type == IW8X_SERVER_STOPPED,
		"server/map shutdown ordering comes from separate engine boundaries");
	expect(ready_on_server && shutdown_vm_denied, "only server-delivered readiness permits VM access");
	server_starting();
	map_starting();
	expect(events.back().epoch > epoch, "new native start advances lifecycle epoch independently of script reloads");
	map_ready();
	server_ready();
	map_shutdown();
	const auto before = events.size();
	pump_game();
	expect(events.size() == before, "shutdown discards queued readiness for ended map");
	// A concurrent VM scope must not drop engine-free shutdown invalidation.
	core::prepare_player(3, "retired");
	const auto live = core::activate_player(3).player;
	{
		core::game_scope vm;
		auto stopping = std::async(std::launch::async,
			[]
			{
				server_stopped();
			});
		stopping.get();
	}
	expect(host.player_valid(live) == IW8X_STALE, "server teardown invalidates players even while VM scope is busy elsewhere");
	core::unload_owner(owner);
	scripts_begin_load();
}

void exceptions_and_unload_wait()
{
	const auto throwing = core::create_owner("throws");
	api bad(&host, throwing);
	bad.function("throwing",
		[](context&)
		{
			throw std::runtime_error("expected test failure");
		});
	for (int i = 0; i < 3; ++i)
		expect(call("throwing").status == IW8X_CALLBACK_ERROR, "caught native exception");
	expect(call("throwing").status == IW8X_STOPPING, "repeated exceptions disable owner");
	expect(core::unload_owner(throwing) == IW8X_OK, "disabled owner still unloads");
	const auto async_owner = core::create_owner("throwing worker");
	api async(&host, async_owner);
	int complete{};
	async.worker(
		[]() -> int
		{
			throw std::runtime_error("worker failure");
		},
		[&](int)
		{
			++complete;
		});
	pump_worker();
	pump_game();
	expect(complete == 0, "worker exception skips completion");
	core::unload_owner(async_owner);
	const auto owner = core::create_owner("in flight");
	api api(&host, owner);
	std::promise<void> entered, release;
	auto released = release.get_future().share();
	api.command("blocking_command",
		[&](const iw8x_string*, uint32_t)
		{
			expect(core::unload_owner(owner) == IW8X_BUSY, "callback self-unload cannot deadlock");
			entered.set_value();
			released.wait();
		});
	auto callback = std::async(std::launch::async,
		[]
		{
			(void)command::dispatch("blocking_command");
		});
	entered.get_future().wait();
	auto unload = std::async(std::launch::async,
		[=]
		{
			return core::unload_owner(owner);
		});
	const auto waiting = unload.wait_for(std::chrono::milliseconds(30));
	release.set_value();
	callback.get();
	const auto status = unload.get();
	expect(waiting == std::future_status::timeout && status == IW8X_OK, "unload waits for in-flight callback lease");
	expect(!command::dispatch("blocking_command"), "no dangling dispatch after unload");
}

void map_shutdown_during_worker()
{
	const auto owner = core::create_owner("map worker");
	scripts_begin_load();

	struct work_state
	{
		std::promise<void> entered, release;
		int completed{}, destroyed{};
	} work;

	iw8x_registration token{};
	expect(host.async(
			   owner, IW8X_SCOPE_MAP,
			   [](void* p)
			   {
				   auto& w = *static_cast<work_state*>(p);
				   w.entered.set_value();
				   w.release.get_future().wait();
			   },
			   [](void* p)
			   {
				   ++static_cast<work_state*>(p)->completed;
			   },
			   &work,
			   [](void* p)
			   {
				   ++static_cast<work_state*>(p)->destroyed;
			   },
			   &token) == IW8X_OK,
		"start map worker");
	auto running = std::async(std::launch::async,
		[]
		{
			pump_worker();
		});
	work.entered.get_future().wait();
	expect(host.cancel_task(owner, token) == IW8X_BUSY, "running work cannot be forcibly canceled");
	scripts_shutdown();
	work.release.set_value();
	running.get();
	pump_game();
	expect(work.completed == 0 && work.destroyed == 1, "map change discards running worker result and destroys state once");
	scripts_begin_load();
	iw8x_native_handle handle{};
	work_state borrow;
	host.create_handle(
		owner, 17, IW8X_SCOPE_MAP, &borrow,
		[](void* p)
		{
			++static_cast<work_state*>(p)->destroyed;
		},
		&handle);
	auto borrowing = std::async(std::launch::async,
		[&]
		{
			return host.with_handle(
				owner, handle, 17,
				[](void*, void* p)
				{
					auto& b = *static_cast<work_state*>(p);
					b.entered.set_value();
					b.release.get_future().wait();
				},
				nullptr);
		});
	borrow.entered.get_future().wait();
	scripts_shutdown();
	expect(borrow.destroyed == 0, "map teardown waits for borrowed resource");
	borrow.release.set_value();
	expect(borrowing.get() == IW8X_OK && borrow.destroyed == 1, "borrow destruction deferred until callback returns");
	expect(core::unload_owner(owner) == IW8X_OK, "worker/borrow owner cleanup");
	scripts_begin_load();
}
