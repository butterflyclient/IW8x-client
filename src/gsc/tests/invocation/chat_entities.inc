// Included once inside the existing instrumented test fixture.
void send_chat_fixture(void*, int reliability, const char* command)
{
	expect(reliability == game::addresses::gsc_api::reliable_server_command, "Replay chat uses reliable commands");
	chat_commands.emplace_back(command);
}

void production_chat()
{
	namespace native = game::addresses::gsc_api;
	core::game_scope scope;
	std::array<uint8_t, 0x580> client{};
	std::array<void*, 2> vtable{ nullptr, reinterpret_cast<void*>(&send_chat_fixture) };
	*reinterpret_cast<void***>(client.data()) = vtable.data();
	*game::at<uint32_t>(native::client_count) = 6;
	game::at<void*>(native::clients)[5] = client.data();
	auto player = core::prepare_player(5, "chat");
	core::activate_player(5);
	chat_commands.clear();
	client[native::client_state] = 3;
	expect(core::tell_chat(player.player, "hello") == IW8X_BUSY && chat_commands.empty(), "loading client cannot receive chat");
	client[native::client_state] = 4;
	expect(core::tell_chat(player.player, "hello") == IW8X_OK, "Replay active client accepts native chat");
	expect(chat_commands.back() == std::string("T \"\x1fServer^7: hello\" 5"), "Replay stock T command preserves chat payload and sender");
	expect(core::broadcast_chat("all") == IW8X_OK && chat_commands.size() == 2, "broadcast sends once to each active identity");
	expect(core::tell_chat(player.player, "") == IW8X_INVALID_ARGUMENT, "empty chat rejected");
	expect(core::tell_chat(player.player, "bad\ncommand") == IW8X_INVALID_ARGUMENT, "chat cannot inject another command");
	expect(core::tell_chat(player.player, "bad\"command") == IW8X_INVALID_ARGUMENT, "chat quotes rejected");
	expect(core::tell_chat(player.player, std::string(121, 'x')) == IW8X_LIMIT, "chat wire limit preserved");
	core::disconnect_player(5);
	expect(core::tell_chat(player.player, "late") == IW8X_STALE, "disconnected chat identity stays stale");
	game::at<void*>(native::clients)[5] = nullptr;
	*game::at<uint32_t>(native::client_count) = 0;
}

void entity_lifetime()
{
	core::game_scope scope;
	const auto ref = core::capture_entity(5);
	expect(ref.generation && core::validate_entity(ref) == IW8X_OK, "production entity registry reads Replay in-use pointer");
	expect(core::capture_entity(5 | 0xabcdef0000000000ull).generation == ref.generation, "scr_entref padding ignored");
	expect(!core::capture_entity(5 | (uint64_t(10) << 32)).generation, "Weapon class does not become a live world entity");
	expect(!core::capture_entity(2048).generation, "Replay entity bound is 2048");
	in_use[5] = 0;
	expect(core::validate_entity(ref) == IW8X_STALE, "freed native entity is stale");
	core::invalidate_entity(5);
	in_use[5] = 1;
	const auto next = core::capture_entity(5);
	expect(next.generation != ref.generation && core::validate_entity(ref) == IW8X_STALE, "entity reuse cannot revive an old identity");
	core::invalidate_entities();
	expect(core::validate_entity(next) == IW8X_STALE, "map teardown retires all entity identities");
}
