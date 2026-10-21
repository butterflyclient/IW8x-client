// Included once inside the existing instrumented test fixture.
void registries_and_values()
{
	const auto owner = core::create_owner("test values");
	api api(&host, owner);
	expect(sdk_c_version() == api_version, "C header version");
	auto wrong_host = host;
	wrong_host.version = 999;
	bool rejected = false;
	try
	{
		iw8x::api invalid(&wrong_host, owner);
	}
	catch (const api_error& e)
	{
		rejected = e.code() == IW8X_VERSION_MISMATCH;
	}
	expect(rejected, "SDK rejects API mismatch");
	int destroyed{};
	expect(host.register_native(owner, string_view("raw"), 0, empty_native, &destroyed, count_destroy) == IW8X_OK, "register function");
	expect(host.register_native(owner, string_view("RAW"), 0, empty_native, &destroyed, count_destroy) == IW8X_DUPLICATE,
		"case insensitive duplicate");
	expect(destroyed == 0, "failure must not consume userdata");
	api.function("add",
		[](context& c)
		{
			c.return_value(c.argument<int>(0) + c.argument<int>(1));
		});
	auto result = call("add", { 10, 20 });
	expect(result.status == IW8X_OK && result.result.as<int>() == 30, "typed integer return");
	result = call("add", { "wrong", 2 });
	expect(result.status == IW8X_CALLBACK_ERROR && result.result.type() == value_type::undefined, "wrong type fails predictably");
	api.function("types",
		[](context& c)
		{
			expect(c.argument_count() == 5, "argument count");
			const auto text = c.argument<std::string_view>(0);
			expect(c.argument<float>(1) == 1.5f && c.argument<bool>(2), "float/bool arguments");
			expect(c.argument<vector>(3).z == 3 && c.argument<entity>(4).valid(), "vector/entity arguments");
			expect(text == "owned", "argument string lifetime across other argument reads");
			c.return_value(value::object{ { "text", value(text) }, { "array", value::array{ 1, true, 2.0f } } });
		});
	result = call("types", { "owned", 1.5f, true, vector{ 1, 2, 3 }, iw8x_entity{ 5, 0, entity_generation } });
	expect(result.status == IW8X_OK && result.result.get<value::object>().at("array").get<value::array>().size() == 3,
		"owned array/struct values cross host ABI");
	const encoded_value encoded(result.result);
	const auto copy = value::copy(encoded.view());
	expect(copy.get<value::object>().at("text").as<std::string>() == "owned", "recursive value copy");
	const iw8x::weapon weapon{ { 0x7fc00001u, 0xdeadbeefu, 0x12345678u } };
	const value weapon_value(weapon);
	const encoded_value encoded_weapon(weapon_value);
	const auto weapon_copy = value::copy(encoded_weapon.view()).as<iw8x::weapon>();
	expect(weapon_copy.words[0] == weapon.words[0] && weapon_copy.words[1] == weapon.words[1] && weapon_copy.words[2] == weapon.words[2],
		"opaque weapon bits survive public ABI without float conversion");
	iw8x_value invalid_weapon{};
	invalid_weapon.type = IW8X_WEAPON_SNAPSHOT;
	rejected = false;
	try
	{
		value::copy(invalid_weapon);
	}
	catch (const std::invalid_argument&)
	{
		rejected = true;
	}
	expect(rejected, "null Weapon view rejected");
	invalid_weapon.type = IW8X_WEAPON;
	rejected = false;
	try
	{
		value::copy(invalid_weapon);
	}
	catch (const std::invalid_argument&)
	{
		rejected = true;
	}
	expect(rejected, "legacy vector-shaped Weapon tag cannot masquerade as snapshot");
	iw8x_value invalid{};
	invalid.type = IW8X_BOOLEAN;
	invalid.as.boolean = 2;
	rejected = false;
	try
	{
		value::copy(invalid);
	}
	catch (const std::invalid_argument&)
	{
		rejected = true;
	}
	expect(rejected, "invalid ABI boolean rejected");
	api.namespace_("sample").function("echo",
		[](context& c)
		{
			c.return_value(c.argument<value>(0));
		});
	expect(call("sample_echo", { 42 }).result.as<int>() == 42, "namespace flattening");
	api.method("ping",
		[](context& c, entity self)
		{
			self.notify("ping", { "payload", 4 });
			c.return_value(true);
		});
	result = call("ping", {}, true, { 5, 0, entity_generation });
	expect(result.status == IW8X_OK && result.result.as<bool>() && notified_args[1].as<int>() == 4, "method receiver/notification");
	const auto old = iw8x_entity{ 5, 0, entity_generation++ };
	expect(call("ping", {}, true, old).status == IW8X_STALE, "stale entity generation rejected before callback");
	expect(host.notify({}, string_view("event"), nullptr, 0) == IW8X_WRONG_THREAD, "notify enforces server scope");
	gsc::value_receiver dvar_out;
	expect(host.get_dvar_string(string_view("ui_mapname"), &gsc::value_receiver::receive, &dvar_out) == IW8X_WRONG_THREAD,
		"dvar read enforces server scope");
	expect(host.broadcast(string_view("hello")) == IW8X_WRONG_THREAD, "chat broadcast enforces server scope");
	expect(host.tell({}, string_view("hello")) == IW8X_WRONG_THREAD, "chat tell enforces server scope");
	{
		core::game_scope scope;
		expect(host.get_field(
				   { 5, 0, entity_generation }, string_view("health"),
				   [](void*, const iw8x_value*)
				   {
				   },
				   nullptr) == IW8X_UNSUPPORTED,
			"unverified fields fail closed");
		expect(api.dvar_string("ui_mapname") == "mp_test", "read-only dvar string crosses public ABI");
	}
	uint64_t saved_call{};
	host.register_native(
		owner, string_view("capture_call"), 0,
		[](void* data, uint64_t id, iw8x_entity)
		{
			*static_cast<uint64_t*>(data) = id;
		},
		&saved_call, nullptr);
	call("capture_call");
	uint32_t count{};
	expect(host.argument_count(saved_call, &count) == IW8X_STALE, "invocation cannot escape callback");
	expect(core::unload_owner(owner) == IW8X_OK && destroyed == 1, "owner cleanup destroys once");
	expect(call("add", { 1, 2 }).status == IW8X_NOT_FOUND, "owner unregisters functions");
	expect(call("ping", {}, true, { 5, 0, entity_generation }).status == IW8X_NOT_FOUND, "owner unregisters methods");
}

void events_commands_tasks_handles()
{
	const auto owner = core::create_owner("resources");
	api api(&host, owner);
	const auto outsider = core::create_owner("outsider");
	int events{}, commands{}, tasks{}, destroyed{};
	api.subscribe(IW8X_SCRIPTS_LOADED,
		[&](const iw8x_event&)
		{
			++events;
		});
	expect(host.subscribe(
			   owner, IW8X_PLAYER_SPAWNED,
			   [](void*, const iw8x_event*)
			   {
			   },
			   nullptr, nullptr, nullptr) == IW8X_INVALID_ARGUMENT,
		"subscription validates output");
	iw8x_registration token{};
	expect(host.subscribe(
			   owner, IW8X_GAME_INITIALIZED,
			   [](void*, const iw8x_event*)
			   {
			   },
			   nullptr, nullptr, &token) == IW8X_UNSUPPORTED,
		"unsupported lifecycle not invented");
	scripts_begin_load();
	scripts_loaded();
	expect(events == 1, "proven lifecycle delivered");
	api.command("owned_command",
		[&](const iw8x_string* args, uint32_t n)
		{
			expect(n == 2 && text(args[1]) == "argument", "existing command arguments");
			++commands;
		});
	expect(command::dispatch("owned_command argument") && commands == 1, "existing command dispatch");
	command::add("stock_command",
		[](const command::params&)
		{
		});
	expect(host.add_command(
			   owner, string_view("stock_command"),
			   [](void*, const iw8x_string*, uint32_t)
			   {
			   },
			   nullptr, nullptr) == IW8X_DUPLICATE,
		"native commands cannot replace existing infrastructure");
	command::remove("stock_command");
	token = api.game_thread(
		[&]
		{
			++tasks;
		});
	expect(host.cancel_task(outsider, token) == IW8X_NOT_FOUND, "task cancellation ownership");
	expect(host.cancel_task(owner, token) == IW8X_OK, "cancel pending task");
	pump_game();
	expect(tasks == 0, "canceled task never runs");
	api.game_thread(
		[&]
		{
			++tasks;
			api.game_thread(
				[&]
				{
					++tasks;
				});
		});
	pump_game();
	expect(tasks == 1, "reentrant scheduling waits next pump");
	pump_game();
	expect(tasks == 2, "reentrant task executes later");
	int completed{};
	api.worker(
		[]
		{
			expect(!core::in_game_scope(), "worker does not inherit VM scope");
			return 42;
		},
		[&](int n)
		{
			expect(core::in_game_scope(), "completion on game scope");
			completed = n;
		});
	pump_worker();
	expect(completed == 0, "worker completion waits for game pump");
	pump_game();
	expect(completed == 42, "async result delivery");
	api.game_thread(
		[&]
		{
			++tasks;
		});
	iw8x_native_handle handle{};
	expect(host.create_handle(owner, 9, IW8X_SCOPE_MAP, &destroyed, count_destroy, &handle) == IW8X_OK, "native handle allocation");
	expect(host.release_handle(outsider, handle, 9) == IW8X_STALE, "handle owner isolation");
	expect(host.release_handle(owner, handle, 10) == IW8X_STALE, "handle type isolation");

	struct borrow_data
	{
		iw8x_plugin_id owner;
		iw8x_native_handle handle;
	} borrowing{ owner, handle };

	expect(host.with_handle(
			   owner, handle, 9,
			   [](void* p, void* resource)
			   {
				   auto& b = *static_cast<borrow_data*>(p);
				   expect(resource != nullptr, "resource borrow");
				   expect(host.release_handle(b.owner, b.handle, 9) == IW8X_BUSY, "cannot release borrowed handle");
			   },
			   &borrowing) == IW8X_OK,
		"with_handle callback");
	expect(host.release_handle(owner, handle, 9) == IW8X_OK && destroyed == 1, "native handle release");
	const auto old = handle;
	host.create_handle(owner, 9, IW8X_SCOPE_MAP, &destroyed, count_destroy, &handle);
	expect(old != handle && host.release_handle(owner, old, 9) == IW8X_STALE, "generation prevents alias on slot reuse");
	scripts_shutdown();
	pump_game();
	expect(tasks == 2 && destroyed == 2, "map shutdown cancels tasks and closes map resources");
	expect(host.release_handle(owner, handle, 9) == IW8X_STALE, "map invalidates handles");
	scripts_begin_load();
	host.create_handle(owner, 7, IW8X_SCOPE_PLUGIN, &destroyed, count_destroy, &handle);
	api.game_thread(
		[&]
		{
			++tasks;
		});
	expect(core::unload_owner(owner) == IW8X_OK, "resource owner unload");
	scripts_loaded();
	pump_game();
	expect(events == 1 && tasks == 2 && destroyed == 3, "owner unload subscriptions/task/handle cleanup");
	expect(!command::dispatch("owned_command argument"), "owner command cleanup");
	core::unload_owner(outsider);
}
