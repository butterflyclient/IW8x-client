// Included once inside the existing instrumented test fixture.
void diagnostic_notifications()
{
	core::drain_logs();
	diagnostic_lines.clear();
	worker_notifications = 0;
	core::set_log_sink(&capture_diagnostic);
	set_worker_wakeup(&count_worker_wakeup);
	core::queue_log("event-driven", "queued diagnostic");
	expect(worker_notifications.load() == 1, "diagnostic admission immediately wakes the event-driven consumer");
	expect(worker_delay() == std::chrono::milliseconds::zero(), "queued diagnostics request an immediate worker continuation");
	for (unsigned count = 1; count < 256; ++count)
		core::queue_log("event-driven", "fill bounded diagnostic queue");
	const auto before_drop = worker_notifications.load();
	core::queue_log("event-driven", "explicitly rejected overflow");
	expect(worker_notifications.load() > before_drop, "dropped diagnostic wakes the consumer for its saturation report");
	for (unsigned tick = 0; tick < 20; ++tick)
		core::drain_logs();
	expect(!core::logs_pending(), "notified diagnostics and drop accounting drain completely");
	set_worker_wakeup(nullptr);
	core::set_log_sink(nullptr);
	diagnostic_lines.clear();
}

void runtime_diagnostics()
{
	core::set_log_sink(&capture_diagnostic);
	core::slow_sample("below", "fast", 42, 9, 1999, 2000);
	expect(diagnostic_lines.empty(), "fast operations produce no log");
	core::slow_sample("gsc.method", "getcurrentweapon", 42, 9, 2500, 2000);
	expect(diagnostic_lines.empty(), "slow samples are deferred outside game thread");
	core::drain_logs();
	expect(diagnostic_lines.size() == 1 &&
			diagnostic_lines[0].find("owner=42 id=9 name=getcurrentweapon elapsed_us=2500") != std::string::npos,
		"slow diagnostic identifies operation owner registration name and duration");
	diagnostic_lines.clear();
	scripts_begin_load();
	const auto owner = core::create_owner("timed callbacks");
	api api(&host, owner);
	api.game_thread(
		[]
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(12));
		});
	pump_game();
	expect(diagnostic_lines.empty(), "real slow callback does not write logs on the game thread");
	pump_worker();
	const auto contains = [](std::string_view part)
	{
		for (const auto& line : diagnostic_lines)
			if (line.find(part) != std::string::npos) return true;
		return false;
	};
	expect(contains("op=task.game") && contains("op=plugin.dispatch") && contains("op=frame.plugins"),
		"production timing scopes report a slow scheduled callback and its containing pump");
	expect(core::unload_owner(owner) == IW8X_OK, "timing fixture cleanup");
	scripts_shutdown();
	diagnostic_lines.clear();
	for (unsigned i = 0; i < 2000; ++i)
		core::queue_log("burst", "message");
	core::drain_logs();
	expect(diagnostic_lines.size() <= 17, "log drain is bounded per worker tick");
	for (unsigned i = 0; i < 20; ++i)
		core::drain_logs();
	expect(diagnostic_lines.size() == 257, "log burst has bounded capacity and reports dropped messages");
	diagnostic_lines.clear();
	std::promise<void> entered, release;
	sink_entered = &entered;
	sink_release = release.get_future().share();
	core::set_log_sink(&blocking_sink);
	core::queue_log("host", "blocked console");
	auto draining = std::async(std::launch::async,
		[]
		{
			core::drain_logs();
		});
	entered.get_future().wait();
	auto producer = std::async(std::launch::async,
		[]
		{
			for (unsigned i = 0; i < 1000; ++i)
				core::queue_log("game", "not blocked by I/O");
		});
	const auto ready = producer.wait_for(std::chrono::seconds(1));
	// Remove sink before unblocking so the next queued line cannot signal twice.
	core::set_log_sink(nullptr);
	release.set_value();
	producer.get();
	draining.get();
	expect(ready == std::future_status::ready, "blocked log sink cannot block game-thread producers");
	for (unsigned i = 0; i < 20; ++i)
		core::drain_logs();
	diagnostic_notifications();
}
