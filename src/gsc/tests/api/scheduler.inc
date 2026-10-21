// Included once inside the existing instrumented test fixture.
std::atomic<unsigned> worker_notifications{};

void count_worker_wakeup() noexcept
{
	worker_notifications.fetch_add(1, std::memory_order_relaxed);
}

void disabled_worker_owner_sleeps()
{
	core::drain_logs();
	worker_notifications = 0;
	set_worker_wakeup(&count_worker_wakeup);
	const auto owner = core::create_owner("disabled worker wakeup");
	struct payload
	{
		unsigned work{}, complete{}, destroyed{};
	} data;
	iw8x_registration task{};
	expect(host.async(
			   owner, IW8X_SCOPE_PLUGIN,
			   [](void* context)
			   {
				   ++static_cast<payload*>(context)->work;
			   },
			   [](void* context)
			   {
				   ++static_cast<payload*>(context)->complete;
			   },
			   &data,
			   [](void* context)
			   {
				   ++static_cast<payload*>(context)->destroyed;
			   },
			   &task) == IW8X_OK,
		"overdue worker task accepted before owner is disabled");
	expect(worker_notifications.load() != 0, "worker admission wakes the event-driven consumer");
	const auto plugin = core::find_owner(owner);
	for (unsigned fault = 0; fault < 3; ++fault)
		core::fault(plugin, "disabled owner fixture");
	pump_worker();
	expect(!data.work && !data.complete, "disabled worker owner cannot dispatch callbacks");
	expect(core::state().tasks.contains(task), "disabled task retains its data for unload cleanup");
	expect(worker_delay() > std::chrono::milliseconds::zero(), "overdue disabled worker task cannot cause an immediate repeat loop");
	expect(core::unload_owner(owner) == IW8X_OK, "disabled worker owner unloads safely");
	expect(data.destroyed == 1 && !core::state().tasks.contains(task), "disabled task cleanup runs exactly once on unload");
	set_worker_wakeup(nullptr);
}

void queued_commands()
{
	const auto owner = core::create_owner("queued console");
	api plugin(&host, owner);
	unsigned runs{}, nested{}, old_runs{}, replacement_runs{};
	plugin.command("queued_test",
		[&](const iw8x_string* args, uint32_t count)
		{
			expect(core::in_game_scope(), "queued command runs on game boundary");
			expect(count == 2 && std::string_view(args[1].data, args[1].size) == "copied", "queued command owns input text");
			++runs;
		});
	std::thread producer(
		[&]
		{
			std::string input = "QUEUED_TEST copied";
			expect(command::enqueue(input) == command::enqueue_result::queued, "worker can enqueue registered command");
			input.assign("overwritten");
		});
	producer.join();
	expect(runs == 0, "console producer never calls plugin inline");
	pump_game();
	expect(runs == 1, "queued command delivered once");
	expect(command::enqueue("stock_only") == command::enqueue_result::unknown, "unknown command remains native");
	expect(command::enqueue("queued_test " + std::string(1024, 'x')) == command::enqueue_result::rejected,
		"oversized plugin input rejected explicitly");
	plugin.command("queued_nested",
		[&](const iw8x_string*, uint32_t)
		{
			if (++nested == 1)
			{
				expect(command::enqueue("queued_nested") == command::enqueue_result::queued, "callback can enqueue another command");
				pump_game();
			}
		});
	(void)command::enqueue("queued_nested");
	pump_game();
	expect(nested == 1, "nested command waits for following pump");
	pump_game();
	expect(nested == 2, "nested command delivered exactly once");
	for (unsigned i = 0; i < 256; ++i)
		expect(command::enqueue("queued_test copied") == command::enqueue_result::queued, "bounded queue accepts capacity");
	expect(command::enqueue("queued_test copied") == command::enqueue_result::rejected, "queue saturation cannot forward a plugin command");
	pump_game();
	expect(runs == 33, "one pump drains at most 32 commands");
	for (unsigned i = 0; i < 7; ++i)
		pump_game();
	expect(runs == 257, "command burst drains without loss or duplicates");
	pump_game();
	expect(runs == 257, "empty queue stays idle");
	const auto token = command::try_add("replacement_test",
		[&](const command::params&)
		{
			++old_runs;
		});
	(void)command::enqueue("replacement_test");
	command::remove("replacement_test", token);
	const auto replacement = command::try_add("replacement_test",
		[&](const command::params&)
		{
			++replacement_runs;
		});
	pump_game();
	expect(!old_runs && !replacement_runs, "stale queued input cannot enter a replacement handler");
	(void)command::enqueue("replacement_test");
	pump_game();
	expect(replacement_runs == 1, "new registration accepts fresh input");
	command::remove("replacement_test", replacement);
	(void)command::enqueue("queued_test copied");
	expect(core::unload_owner(owner) == IW8X_OK, "queued input does not pin an unloaded plugin");
	pump_game();
	expect(runs == 257, "unloaded callback never receives queued input");
}

void task_bursts()
{
	scripts_begin_load();
	const auto owner = core::create_owner("task burst");
	api api(&host, owner);
	int nested{};
	api.game_thread(
		[&]
		{
			++nested;
			api.game_thread(
				[&]
				{
					++nested;
				});
			pump_game();
		});
	pump_game();
	expect(nested == 1, "recursive plugin pump must defer nested tasks");
	pump_game();
	expect(nested == 2, "nested task runs once on following pump");
	std::vector<unsigned> executions(3200);
	for (size_t i = 0; i < executions.size(); ++i)
		api.game_thread(
			[&, i]
			{
				++executions[i];
			});
	pump_game();
	expect(!core::state().tasks.empty(), "burst must not drain unbounded work in one server frame");
	unsigned pumps = 1;
	while (!core::state().tasks.empty() && pumps++ < 4000)
		pump_game();
	expect(core::state().tasks.empty(), "burst drains without leaking queued tasks");
	for (const auto calls : executions)
		expect(calls == 1, "each task executes exactly once");
	int slow{}, after_slow{};
	api.game_thread(
		[&]
		{
			++slow;
			std::this_thread::sleep_for(std::chrono::milliseconds(6));
		});
	api.game_thread(
		[&]
		{
			++after_slow;
		});
	pump_game();
	expect(slow == 1 && after_slow == 0, "slow task finishes but yields remaining work to another frame");
	pump_game();
	expect(after_slow == 1, "work deferred by time budget is not lost");

	// 200 players, repeated spawn/kill events. Each callback queues work which
	// queues one completion; all 3200 callbacks must drain once, with no loop.
	int delivered{}, tasks{}, completions{};
	const auto listener = [&](const iw8x_event& event)
	{
		expect(event.player.client_num < 200, "event carries player identity");
		++delivered;
		api.game_thread(
			[&]
			{
				++tasks;
				api.game_thread(
					[&]
					{
						++completions;
					});
			});
	};
	api.subscribe(IW8X_PLAYER_SPAWN, listener);
	api.subscribe(IW8X_PLAYER_KILL, listener);
	for (uint32_t i = 0; i < 200; ++i)
	{
		core::prepare_player(i, "LoadBot");
		const auto player = core::activate_player(i).player;
		for (unsigned wave = 0; wave < 4; ++wave)
		{
			emit(IW8X_PLAYER_SPAWN, {}, player);
			emit(IW8X_PLAYER_KILL, {}, player);
		}
	}
	const auto event_start = std::chrono::steady_clock::now();
	pumps = 0;
	while (!core::state().tasks.empty() && pumps++ < 4000)
		pump_game();
	expect(delivered == 1600 && tasks == delivered && completions == tasks && core::state().tasks.empty(),
		"many player events and nested tasks drain exactly once");
	core::invalidate_players();
	const auto event_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - event_start).count();

	// Sleeping timers are not walked on each frame or on the worker pipeline.
	for (unsigned i = 0; i < 4000; ++i)
		api.game_thread(
			[&]
			{
				++tasks;
			},
			60000);
	pump_game();
	pump_worker();
	const auto idle_start = std::chrono::steady_clock::now();
	for (unsigned i = 0; i < 10000; ++i)
	{
		pump_game();
		pump_worker();
	}
	const auto idle_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - idle_start).count();
	expect(tasks == 1600 && core::state().tasks.size() == 4000, "future timers do not run early");
	api.game_thread(
		[&]
		{
			++tasks;
		});
	pump_game();
	expect(tasks == 1601, "new immediate task wakes a sleeping pipeline");
	int completed{};
	api.worker(
		[]
		{
			return 7;
		},
		[&](int result)
		{
			completed = result;
		});
	pump_worker();
	pump_game();
	expect(completed == 7, "worker completion wakes a sleeping game queue");
	scripts_shutdown();
	expect(core::state().tasks.empty(), "map shutdown drains thousands of delayed callbacks");
	std::cout << "Runtime load: 3200 nested tasks=" << event_us << "us in " << pumps
			  << " pumps; 10000 idle game+worker pairs with 4000 timers=" << idle_us << "us\n";
	expect(core::unload_owner(owner) == IW8X_OK, "burst owner cleanup");
	scripts_shutdown();
	disabled_worker_owner_sleeps();
}
