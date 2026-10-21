// Included once inside the existing instrumented test fixture.
void dll_and_manifest(const std::filesystem::path& directory)
{
	const auto root = directory / "plugins";
	iw8x::plugins::discover(root);
	auto plugins = iw8x::plugins::loaded();
	expect(plugins.size() == 1 && plugins[0].name == "Native Example", "load separately compiled example DLL");
	expect(!command::dispatch("failed_plugin_command"), "failed DLL load rolls back registrations");
	expect(call("native_add", { 10, 20 }).result.as<int>() == 30, "real DLL callback through public ABI");
	const auto before = notifications;
	expect(call("native_test", {}, true, { 5, 0, entity_generation }).status == IW8X_OK, "real DLL method");
	expect(notifications == before, "example notify scheduled after GSC wait installation");
	pump_game();
	expect(notifications == before + 1, "real DLL scheduled notification");
	expect(iw8x::plugins::unload(plugins[0].id) == IW8X_OK, "FreeLibrary after registry cleanup");
	expect(call("native_add", { 1, 2 }).status == IW8X_NOT_FOUND && !command::dispatch("native_example_status"),
		"DLL native and command cleanup");
	const auto temporary = directory / "manifest-test.json";
	auto reject = [&](const char* input)
	{
		{
			std::ofstream out(temporary);
			out << input;
		}
		bool rejected{};
		try
		{
			iw8x::plugins::read_manifest(temporary);
		}
		catch (...)
		{
			rejected = true;
		}
		expect(rejected, "invalid manifest rejected");
	};
	reject(R"({"name":"a","version":"1","api":2,"entry":"test.dll"})");
	reject(R"({"name":"a","version":"1","api":1,"entry":"../test.dll"})");
	reject(R"({"name":"a","name":"b","version":"1","api":1,"entry":"test.dll"})");
	reject(R"({"name":"a","version":"1","api":1.0,"entry":"test.dll"})");
	std::filesystem::remove(temporary);
}

void failed_unload_is_quarantined()
{
	const auto owner = core::create_owner("quarantined unload");
	expect(core::unload_owner(owner,
			   []
			   {
				   throw std::runtime_error("unload did not join its threads");
			   }) == IW8X_CALLBACK_ERROR,
		"throwing unload export must prevent FreeLibrary");
	expect(core::unload_owner(owner) == IW8X_CALLBACK_ERROR, "unsafe unload cannot be retried into FreeLibrary");
	const auto plugin = core::find_owner(owner);
	expect(plugin && !plugin->accepting && plugin->cleanup_failed, "failed cleanup keeps owner pinned and disabled");
}
