// Included once inside the existing instrumented test fixture.
void compiled_module_resolution()
{
	const auto id = core::create_owner("compiled module tests");
	iw8x::api api(&host, id);
	const auto night = api.gsc().script("scripts/cp_mp/utility/game_utility", "isnightmap", iw8x::gsc::value_type::boolean);
	expect(core::script_module_token("scripts/cp_mp/utility/game_utility") == 0x08C4 &&
			core::script_module_token("scripts/mp/class") == 0x0723 &&
			core::script_function_token(iw8x::gsc::string_view("isnightmap")) == 0x72B6,
		"Replay canonical module IDs and function IDs remain distinct");
	expect(api.gsc().script("SCRIPTS\\CP_MP\\UTILITY\\GAME_UTILITY.GSC", "IsNightMap", iw8x::gsc::value_type::boolean).registration() ==
			night.registration(),
		"isnightmap path/case aliases share one registration");
	core::game_scope scope;
	core::set_log_sink(&capture_resolution_log);
	resolution_logs.clear();
	iw8x::gsc::scripts_begin_load();
	module_attempts.clear();
	night_module_loaded = false;
	resolve_exports();
	expect(module_attempts == std::vector<std::string>{ "2244" }, "stock compiled module is tried first without redundant textual loads");
	expect(night().as<bool>(), "readable isnightmap resolves numeric stock module 2244");
	clean();
	const auto lookups = script_lookups, loads = script_loads;
	for (int i = 0; i < 3; ++i)
		expect(night().as<bool>(), "cached isnightmap call");
	const auto untyped = api.gsc().script("scripts/cp_mp/utility/game_utility", "isnightmap");
	expect(untyped().as<int>() == 1, "new return contract shares the resolved module export");
	resolve_exports();
	expect(script_lookups == lookups && script_loads == loads && resolution_logs.empty(),
		"repeated calls and resolutions reuse the successful handle without log spam");
	clean();

	missing_night_export = true;
	night_module_loaded = false;
	iw8x::gsc::scripts_begin_load();
	status(IW8X_STALE,
		[&]
		{
			night();
		});
	module_attempts.clear();
	resolve_exports();
	const std::vector<std::string> aliases{ "2244", "scripts/cp_mp/utility/game_utility", "scripts/cp_mp/utility/game_utility.gsc" };
	expect(module_attempts == aliases, "missing export records every canonical path attempted");
	status(IW8X_NOT_FOUND,
		[&]
		{
			night();
		});
	core::drain_logs();
	expect(resolution_logs.size() == 1, "one failure diagnostic per shared export per generation");
	const auto& diagnostic = resolution_logs.front();
	for (const auto part : { "requested=\"scripts/cp_mp/utility/game_utility\"", "function=\"isnightmap\"", "function_token=0x000072B6",
			 "module_token=0x000008C4", "module=\"2244\"", "module=\"scripts/cp_mp/utility/game_utility\"",
			 "module=\"scripts/cp_mp/utility/game_utility.gsc\"", "load=", "final_handle=0x00000000" })
		expect(diagnostic.find(part) != std::string::npos, "failed resolution diagnostic includes request and full trace");
	resolve_exports();
	status(IW8X_NOT_FOUND,
		[&]
		{
			night();
		});
	expect(module_attempts == aliases && resolution_logs.size() == 1, "negative cache avoids repeated resolution/logging");

	missing_night_export = false;
	iw8x::gsc::scripts_begin_load();
	night_module_loaded = false;
	script_offset += 0x100;
	module_attempts.clear();
	resolve_exports();
	expect(night().as<bool>() && module_attempts == std::vector<std::string>{ "2244" },
		"same readable registration refreshes a formerly missing export with the new map handle");
	clean();

	// This literal-name fixture has a far call to 2244::isnightmap. Its native
	// loader links that dependency; the host only selects the caller's export.
	expect(core::unload_owner(id) == IW8X_OK, "compiled module owner unload");
	// Use a separate owner so the target module itself is not pre-registered.
	const auto other_id = core::create_owner("far call module tests");
	iw8x::api other(&host, other_id);
	const auto caller = other.gsc().script("scripts/tests/night_forward", "main", iw8x::gsc::value_type::boolean);
	const auto absent = other.gsc().script("scripts/tests/night_forward", "isnightmap");
	iw8x::gsc::scripts_begin_load();
	night_module_loaded = false;
	module_attempts.clear();
	resolve_exports();
	expect(caller().as<bool>() && night_module_loaded,
		"native script loader handles the cross-module dependency before the exported caller runs");
	clean();
	expect(std::find(module_attempts.begin(), module_attempts.end(), "2244") == module_attempts.end(),
		"host does not need to manually resolve far-call dependencies");
	status(IW8X_NOT_FOUND,
		[&]
		{
			absent();
		});
	expect(core::unload_owner(other_id) == IW8X_OK, "far call owner unload");
	core::drain_logs();
	core::set_log_sink(nullptr);
	iw8x::gsc::scripts_shutdown();
}
