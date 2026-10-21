// Included once inside the existing instrumented test fixture.
void persistent_tables(const std::filesystem::path& root)
{
	std::filesystem::create_directories(root);
	const auto database = root / "native-bindings.json";
	const std::string retail = R"({"api":4,"bindings":[]})";
	{
		std::ofstream out(database);
		out << retail;
	}
	bool rejected{};
	try
	{
		core::initialize_tables(root);
	}
	catch (const std::exception&)
	{
		rejected = true;
	}
	expect(rejected, "Replay rejects an unmarked retail binding database");
	std::ifstream old(database);
	std::string untouched((std::istreambuf_iterator<char>(old)), {});
	old.close();
	expect(untouched == retail, "rejected retail binding data remains unchanged");
	{
		std::ofstream out(database);
		out << nlohmann::json{ { "api", IW8X_PLUGIN_API_VERSION }, { "target", "replay-1.20.4" }, { "bindings", nlohmann::json::array() } };
	}
	core::initialize_tables(root);
	expect(core::reserve_native(false, "test", "replay_table_function") == IW8X_OK, "reserve Replay function identity");
	expect(core::reserve_native(true, "test", "replay_table_method") == IW8X_OK, "reserve Replay method identity");
	expect(core::reserve_native(false, "test", "replay_table_function") == IW8X_OK, "reservation is idempotent");
	expect(core::reserve_native(false, "different", "replay_table_function") == IW8X_DUPLICATE, "durable identity belongs to one plugin");
	expect(core::reserve_native(true, "test", "replay_table_function") == IW8X_DUPLICATE, "function/method identity cannot change");
	std::ifstream h(root / "iw8x_native_bindings.gsh");
	std::string header((std::istreambuf_iterator<char>(h)), {});
	expect(header.find("replay_table_function _func_0419") != std::string::npos, "Replay first custom function ID is 0419");
	expect(header.find("replay_table_method _meth_8787") != std::string::npos, "Replay first custom method ID is 8787");
}

void player_adapter_layout()
{
	namespace native = game::addresses::gsc_api;
	core::game_scope scope;
	std::array<uint8_t, 0x580> client{};
	std::array<uint8_t, 0x80> globals{};
	*game::at<uint8_t*>(native::persistent_globals) = globals.data();
	expect(core::gameplay_server(), "Replay persistent frontend byte zero admits gameplay");
	globals[native::persistent_frontend] = 1;
	expect(!core::gameplay_server(), "Replay frontend server cannot deliver gameplay events");
	globals[native::persistent_frontend] = 0;
	*game::at<uint32_t>(native::client_count) = 6;
	game::at<void*>(native::clients)[5] = client.data();
	client[native::client_state] = 3;
	std::memcpy(client.data() + native::client_name, "Replay fixture", 15);
	expect(core::client_at(5) == client.data() && !core::client_at(6), "Replay client pointer table honors allocated count");
	expect(core::client_state(client.data()) == 3, "Replay SvClient state lives at byte 8");
	expect(core::read_client_name(client.data()) == "Replay fixture", "Replay SvClient name starts at 534, not retail 53C");
	auto prepared = core::prepare_client_player(client.data());
	expect(prepared.player.client_num == 5 && !prepared.connected, "native client copied to prepared plugin identity");
	auto active = core::activate_player(5);
	expect(core::find_client_player(client.data()).player.generation == active.player.generation,
		"native lookup preserves connected identity");
	int16_t index = 5;
	const auto associated = core::associate_entity_player(&index);
	expect(associated.entity.generation && associated.entity.index == 5, "native player entity uses signed number at offset zero");
	value copied_name;
	expect(core::read_player_name(active.player, copied_name) == IW8X_OK && copied_name.as<std::string>() == "Replay fixture",
		"player name survives native storage changes");
	std::memset(client.data() + native::client_name, 'x', native::client_name_capacity);
	expect(core::read_client_name(client.data()).size() == native::client_name_capacity - 1, "native name copy is bounded");
	*game::at<uint32_t>(native::client_count) = native::client_capacity + 1;
	expect(!core::client_at(5) && !core::prepare_client_player(client.data()), "invalid allocation count never walks client table");
	core::invalidate_players();
	*game::at<uint32_t>(native::client_count) = 0;
	game::at<void*>(native::clients)[5] = nullptr;
	*game::at<uint8_t*>(native::persistent_globals) = nullptr;
	expect(!core::gameplay_server(), "absent native server globals do not imply gameplay");
}
