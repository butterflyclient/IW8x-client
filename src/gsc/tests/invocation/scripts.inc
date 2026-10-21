// Included once inside the existing instrumented test fixture.
void script_exports_and_values()
{
	using iw8x::gsc::string_view;
	using iw8x::gsc::value_type;
	const auto id = core::create_owner("readable script tests");
	iw8x::api api(&host, id);
	expect(core::script_function_token(string_view("buildweapon")) == 0x24C5 && core::script_function_token(string_view("MAIN")) == 0x254 &&
			core::script_function_token(string_view("wave_enemies_remaining")) == 0x1184A &&
			core::script_function_token(string_view("aispread")) == 0x1012,
		"canonical names use full 32-bit IDs and correct lowercase spelling");
	const auto build = api.gsc().script("scripts/cp/cp_weapon", "buildweapon", value_type::weapon_);
	const auto echo = api.gsc().script("scripts/tests/echo", "main");
	const auto missing = api.gsc().script("scripts/cp/cp_weapon", "main");
	const auto missing_path = api.gsc().script("scripts/tests/missing", "main");
	const auto extension = api.gsc().script("scripts/tests/extension.gsc", "main");
	expect(api.gsc().script("SCRIPTS\\CP\\CP_WEAPON.GSC", "BuildWeapon", value_type::weapon_).registration() == build.registration() &&
			api.gsc().script("scripts/cp/cp_weapon", 0x24C5, value_type::weapon_).registration() == build.registration(),
		"readable and token calls share path/case/extension-normalized registration");
	std::array<std::future<iw8x_registration>, 4> concurrent;
	for (auto& task : concurrent)
		task = std::async(std::launch::async,
			[api]
			{
				return api.gsc().script("scripts/cp/cp_weapon.gsc", "buildweapon", value_type::weapon_).registration();
			});
	for (auto& task : concurrent)
		expect(task.get() == build.registration(), "concurrent registrations reuse the same ID");
	for (int i = 0; i < 4100; ++i)
		expect(api.gsc().script("scripts/cp/cp_weapon", "buildweapon", value_type::weapon_).registration() == build.registration(),
			"repeated registration never exhausts the registry limit");
	expect(
		core::state().scripts.size() == 5 && script_lookups == 0 && script_loads == 0, "registration does not touch engine lookup objects");
	status(IW8X_NOT_FOUND,
		[&]
		{
			api.gsc().script("scripts/cp/cp_weapon", "iw8x_no_such_function");
		});
	for (auto name : { "", "bad name", "main()", "1main" })
		status(IW8X_INVALID_ARGUMENT,
			[&]
			{
				api.gsc().script("scripts/cp/cp_weapon", name);
			});
	for (auto path : { "", "../cp_weapon", "/scripts/cp/cp_weapon", "scripts//cp_weapon", "scripts/", ".gsc" })
		status(IW8X_INVALID_ARGUMENT,
			[&]
			{
				api.gsc().script(path, "main");
			});
	status(IW8X_INVALID_ARGUMENT,
		[&]
		{
			api.gsc().script(std::string_view("a\0b", 3), "main");
		});
	status(IW8X_INVALID_ARGUMENT,
		[&]
		{
			api.gsc().script("scripts/tests/echo", std::string_view("m\0n", 3));
		});
	iw8x_registration invalid = 99;
	expect(host.register_script_name(id, string_view("scripts/tests/echo"), string_view("main"), 99, &invalid) == IW8X_INVALID_ARGUMENT &&
			invalid == 0,
		"invalid result type clears output registration");
	expect(host.register_script_name(id, string_view("scripts/tests/echo"), string_view("main"), IW8X_INTEGER, nullptr) ==
			IW8X_INVALID_ARGUMENT,
		"null registration output rejected");
	auto legacy = host;
	legacy.size = offsetof(iw8x_plugin_host, register_script_name);
	iw8x::api old_api(&legacy, id);
	status(IW8X_UNSUPPORTED,
		[&]
		{
			old_api.gsc().script("scripts/cp/cp_weapon", "buildweapon");
		});
	expect(old_api.gsc().script("scripts/cp/cp_weapon", 0x24C5, value_type::weapon_).registration() == build.registration(),
		"older host layout still supports token overload");
	status(IW8X_WRONG_THREAD,
		[&]
		{
			echo(7);
		});
	status(IW8X_WRONG_THREAD,
		[&]
		{
			echo.on(iw8x_entity{ 5, 0, 1 }, 7);
		});
	core::game_scope scope;
	status(IW8X_STALE,
		[&]
		{
			echo(7);
		});
	iw8x::gsc::scripts_begin_load();
	status(IW8X_STALE,
		[&]
		{
			echo(7);
		});
	resolve_exports();
	iw8x::gsc::scripts_loaded();
	expect(
		script_lookups == 10 && script_loads == 10, "five exports resolved once, including numeric module aliases and extension fallback");
	resolve_exports();
	expect(script_lookups == 10, "successes and missing exports cached for this script generation");
	status(IW8X_NOT_FOUND,
		[&]
		{
			missing();
		});
	status(IW8X_NOT_FOUND,
		[&]
		{
			missing_path();
		});
	try
	{
		missing();
	}
	catch (const iw8x::api_error& e)
	{
		expect(std::string_view(e.what()).find("scripts/cp/cp_weapon::main") != std::string_view::npos &&
				std::string_view(e.what()).find("IW8X_NOT_FOUND") != std::string_view::npos,
			"SDK error names the requested export and status");
	}
	expect(extension(23).as<int>() == 23, "extension-only script asset works");
	clean();
	const auto weapon =
		api.gsc().call_script<iw8x::weapon>("scripts/cp/cp_weapon", "buildweapon", "iw8_sn_alpha50_mp", iw8x::array{}, "none", "none");
	expect(weapon.words[0] == 0x7fc00001u && weapon.words[1] == 0xdeadbeefu && weapon.words[2] == 0x12345678u,
		"typed readable call returns an owned, lossless Weapon");
	clean();
	const auto inferred =
		api.gsc().script("scripts/cp/cp_weapon", "buildweapon")("iw8_sn_alpha50_mp", iw8x::array{}, "none", "none").as<iw8x::weapon>();
	expect(std::memcmp(&weapon, &inferred, sizeof(weapon)) == 0, "untyped script return infers Weapon without a return hint");
	clean();
	const auto entity = core::capture_entity(5);
	auto global_only = host;
	global_only.size = offsetof(iw8x_plugin_host, call_script_on);
	const auto old_echo = iw8x::api(&global_only, id).gsc().script("scripts/tests/echo", "main");
	status(IW8X_UNSUPPORTED,
		[&]
		{
			old_echo.on(entity, 7);
		});
	expect(old_echo(7).as<int>() == 7, "host without self extension retains global script calls");
	clean();
	const iw8x::struct_value object{ { iw8x::canonical_field(0x2c5), iw8x::array{ 1, "owned", entity } } };
	const std::vector<value> rich{ value{}, -21, 1.5f, true, "text", iw8x::vec3{ 4, 5, 6 }, entity,
		iw8x::array{ 1, "text", iw8x::vec3{ 1, 2, 3 } }, object, weapon };
	for (const auto& input : rich)
	{
		const auto output = api.gsc().script("scripts/tests/echo", "main", input.type())(input);
		expect(output.type() == input.type(), "readable scripts preserve all rich return types");
		clean();
		const auto method_result = api.gsc().script("scripts/tests/echo", "main", input.type()).on(entity, input);
		expect(method_result.type() == input.type(), "entity script calls preserve all rich return types");
		clean();
	}
	expect(echo.on(iw8x::gsc::entity(host, entity), "self").as<std::string>() == "self", "SDK entity overload binds self");
	clean();
	status(IW8X_STALE,
		[&]
		{
			echo.on(iw8x_entity{}, 7);
		});
	clean();
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.gsc().script("scripts/tests/echo", "main", value_type::weapon_).on(entity, 7);
		});
	clean();
	status(IW8X_INVALID_ARGUMENT,
		[&]
		{
			echo.on(entity, std::string("a\0b", 3));
		});
	clean();
	unsupported_script_result = true;
	status(IW8X_UNSUPPORTED,
		[&]
		{
			echo.on(entity, "owned");
		});
	unsupported_script_result = false;
	clean();
	throw_push = true;
	status(IW8X_INTERNAL_ERROR,
		[&]
		{
			echo.on(entity, "throw before native execution");
		});
	throw_push = false;
	clean();
	status(IW8X_NOT_FOUND,
		[&]
		{
			missing.on(entity);
		});
	expect(api.gsc().call_script<int>("scripts/tests/echo", "main", 42) == 42, "typed integer script return");
	expect(api.gsc().call_script<float>("scripts/tests/echo", "main", 1.25f) == 1.25f, "typed number script return");
	expect(
		api.gsc().call_script<bool>("scripts/tests/echo", "main", 1) && !api.gsc().call_script<bool>("scripts/tests/echo", "main", false),
		"typed boolean accepts only VM 0/1");
	expect(api.gsc().call_script<std::string>("scripts/tests/echo", "main", "owned") == "owned", "typed string is owned");
	expect(api.gsc().call_script<iw8x::vec3>("scripts/tests/echo", "main", iw8x::vec3{ 1, 2, 3 }).z == 3, "typed vector return");
	expect(api.gsc().call_script<iw8x_entity>("scripts/tests/echo", "main", entity).generation == entity.generation,
		"typed entity return preserves generation");
	expect(api.gsc().call_script<iw8x::array>("scripts/tests/echo", "main", iw8x::array{ 1, "owned" })[1].as<std::string>() == "owned",
		"typed array return");
	const auto snapshot = api.gsc().call_script<iw8x::struct_value>("scripts/tests/echo", "main", object);
	expect(snapshot.at(iw8x::canonical_field(0x2c5)).as_array()[1].as<std::string>() == "owned", "typed nested struct return");
	expect(api.gsc().call_script("scripts/tests/echo", "main", 17).as<int>() == 17, "untyped convenience returns value");
	api.gsc().call_script<void>("scripts/tests/echo", "main");
	clean();
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.gsc().call_script<int>("scripts/tests/echo", "main", "text");
		});
	clean();
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.gsc().call_script<bool>("scripts/tests/echo", "main", 2);
		});
	clean();
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.gsc().call_script<iw8x::array>("scripts/tests/echo", "main", object);
		});
	clean();
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.gsc().call_script<iw8x::weapon>("scripts/tests/echo", "main", 7);
		});
	clean();
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.gsc().call_script<iw8x::weapon>("scripts/tests/echo", "main", iw8x::vec3{ 1, 2, 3 });
		});
	clean();
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.gsc().call_script<iw8x::weapon>("scripts/tests/echo", "main", entity);
		});
	clean();
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.gsc().call_script<iw8x_entity>("scripts/tests/echo", "main", weapon);
		});
	clean();
	status(IW8X_UNSUPPORTED,
		[&]
		{
			api.gsc().script("scripts/tests/echo", "main", static_cast<value_type>(IW8X_WEAPON));
		});
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.gsc().call_script<int>("scripts/cp/cp_weapon", "buildweapon", "iw8_sn_alpha50_mp", iw8x::array{}, "none", "none");
		});
	clean();
	status(IW8X_INVALID_ARGUMENT,
		[&]
		{
			echo(std::string("a\0b", 3));
		});
	clean();
	const auto other_id = core::create_owner("other script owner");
	iw8x::api other(&host, other_id);
	const auto other_echo = other.gsc().script("scripts/tests/echo", "main");
	expect(other_echo.registration() != echo.registration() && other_echo(7).as<int>() == 7,
		"owners have separate registrations sharing an already resolved export");
	clean();
	const iw8x::gsc::script_function forged(host, other_id, echo.registration());
	status(IW8X_NOT_FOUND,
		[&]
		{
			forged();
		});
	status(IW8X_NOT_FOUND,
		[&]
		{
			forged.on(entity);
		});
	expect(script_lookups == 10 && script_loads == 10, "calls and new return contracts reuse existing handles");
	const auto late = api.gsc().script("scripts/tests/late", "main");
	status(IW8X_STALE,
		[&]
		{
			late();
		});
	expect(script_lookups == 10, "late registration waits for the next safe window");
	const auto calls_before = script_calls;
	iw8x::gsc::scripts_shutdown();
	status(IW8X_STALE,
		[&]
		{
			echo(7);
		});
	status(IW8X_STALE,
		[&]
		{
			echo.on(entity, 7);
		});
	iw8x::gsc::scripts_begin_load();
	status(IW8X_STALE,
		[&]
		{
			echo(7);
		});
	expect(script_calls == calls_before, "stale generations never enter the VM");
	script_offset = 0x100;
	resolve_exports();
	const auto before_stale_weapon = script_calls;
	status(IW8X_STALE,
		[&]
		{
			echo(weapon);
		});
	clean();
	expect(script_calls == before_stale_weapon, "old map Weapon cannot reach a refreshed script handle");
	expect(api.gsc().method(core::capture_entity(5), "getcurrentweapon").as<iw8x::weapon>().map_epoch != weapon.map_epoch,
		"builtin Weapon capture uses the new map generation automatically");
	clean();
	expect(echo(8).as<int>() == 8 && other_echo(9).as<int>() == 9, "same registrations refresh to new program handles");
	clean();
	expect(echo.on(core::capture_entity(5), 8).as<int>() == 8, "entity script calls use refreshed export handles");
	clean();
	expect(api.gsc().script("scripts/tests/echo", "main").registration() == echo.registration(), "registration survives reload");
	status(IW8X_NOT_FOUND,
		[&]
		{
			late();
		});
	// A previously present export disappears, then reappears in a later load.
	missing_weapon = true;
	iw8x::gsc::scripts_begin_load();
	resolve_exports();
	status(IW8X_NOT_FOUND,
		[&]
		{
			build();
		});
	missing_weapon = false;
	iw8x::gsc::scripts_begin_load();
	resolve_exports();
	expect(build("iw8_sn_alpha50_mp", iw8x::array{}, "none", "none").as<iw8x::weapon>().words[1] == weapon.words[1],
		"negative export cache is retried on reload");
	clean();
	iw8x::gsc::map_shutdown();
	status(IW8X_STALE,
		[&]
		{
			echo(7);
		});
	iw8x::gsc::scripts_begin_load();
	invalidate_during_lookup = true;
	resolve_exports();
	status(IW8X_STALE,
		[&]
		{
			echo(7);
		});
	status(IW8X_STALE,
		[&]
		{
			build();
		});
	resolve_exports();
	expect(echo(10).as<int>() == 10, "in-flight resolution cannot publish after invalidation");
	clean();
	core::invalidate_entity(5);
	status(IW8X_STALE,
		[&]
		{
			echo(entity);
		});
	clean();
	status(IW8X_STALE,
		[&]
		{
			echo.on(entity, 7);
		});
	clean();
	expect(core::unload_owner(id) == IW8X_OK, "script registrations released on plugin unload");
	status(IW8X_STOPPING,
		[&]
		{
			echo(1);
		});
	expect(other_echo(11).as<int>() == 11, "unloading an owner preserves another owner's export");
	clean();
	iw8x::gsc::server_stopped();
	status(IW8X_STALE,
		[&]
		{
			other_echo(1);
		});
	expect(core::unload_owner(other_id) == IW8X_OK && core::state().scripts.empty(), "all script registrations cleaned");
	expect(snapshot.at(iw8x::canonical_field(0x2c5)).as_array()[1].as<std::string>() == "owned",
		"owned script result survives reload and owner unload");
	iw8x::gsc::scripts_shutdown();
}
