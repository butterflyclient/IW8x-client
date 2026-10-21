// Included once inside the existing instrumented test fixture.
void player_lifecycle_and_accessors()
{
	core::invalidate_players();
	const auto owner = core::create_owner("player lifecycle");
	api api(&host, owner);
	std::vector<iw8x_event> events;
	std::vector<iw8x_status> validity;
	bool vm_denied = true;
	const uint32_t types[]{ IW8X_PLAYER_CONNECTING, IW8X_PLAYER_CONNECTED, IW8X_PLAYER_SPAWN, IW8X_PLAYER_DEATH, IW8X_PLAYER_KILL,
		IW8X_PLAYER_DISCONNECTING, IW8X_PLAYER_DISCONNECTED };
	for (const auto type : types)
	{
		expect((host.supported_events() & (1ull << type)) != 0, "proven player event advertised");
		api.subscribe(type,
			[&](const iw8x_event& event)
			{
				events.push_back(event);
				validity.push_back(host.player_valid(event.player));
				vm_denied =
					vm_denied && !core::in_game_scope() && host.notify({}, string_view("player_event"), nullptr, 0) == IW8X_WRONG_THREAD;
			});
	}
	expect(!(host.supported_events() & (1ull << IW8X_GAME_INITIALIZED)), "unproven lifecycle remains unsupported");
	const auto candidate = core::connecting_player(3, "Alice");
	const auto player = api.player_from(candidate.player);
	expect(candidate && player.valid() && player.name() == "Alice" && api.players().empty(),
		"connecting candidate is readable but not enumerated as connected");
	expect(!player.entity(), "candidate has no entity");
	expect(!core::connecting_player(3, "duplicate"), "duplicate candidate cannot replace identity");
	core::connected_player(3, "Alice");
	core::connected_player(3, "Alice");
	expect(events.size() == 2 && api.players().size() == 1 && player.client_num() == 3,
		"accepted connection emitted and enumerated exactly once");
	broadcast_messages.clear();
	tell_messages.clear();
	{
		core::game_scope scope;
		api.broadcast("server hello");
		api.tell(player, "private hello");
		expect(broadcast_messages.size() == 1 && broadcast_messages[0] == "server hello", "SDK broadcast crosses public chat ABI");
		expect(tell_messages.size() == 1 && tell_messages[0].first.generation == candidate.player.generation &&
				tell_messages[0].second == "private hello",
			"SDK tell preserves validated player identity");
		expect(host.broadcast(string_view("bad\nline")) == IW8X_INVALID_ARGUMENT, "chat rejects control characters");
		expect(host.broadcast(string_view("bad\"quote")) == IW8X_INVALID_ARGUMENT, "chat rejects command-breaking quotes");
		const std::string too_long(121, 'x');
		expect(host.broadcast(string_view(too_long)) == IW8X_LIMIT, "chat enforces message length limit");
	}
	uint64_t xuid = 99;
	expect(host.player_xuid(candidate.player, &xuid) == IW8X_UNSUPPORTED && xuid == 0, "unmapped live XUID accessor fails explicitly");
	bool unsupported{};
	try
	{
		(void)player.xuid();
	}
	catch (const api_error& error)
	{
		unsupported = error.code() == IW8X_UNSUPPORTED;
	}
	expect(unsupported, "SDK preserves unsupported XUID status");
	auto forged = candidate.player;
	forged.reserved = 1;
	expect(host.player_valid(forged) == IW8X_STALE, "reserved player bits rejected");
	forged = candidate.player;
	++forged.xuid;
	expect(host.player_valid(forged) == IW8X_STALE, "identity fields cannot be forged");

	core::spawned_player(3, { 5, 0, 901 });
	const auto first_entity = player.entity()->ref();
	core::spawned_player(3, { 5, 0, 902 });
	expect(events.size() == 4 && events[2].player.generation == candidate.player.generation &&
			events[3].player.generation == candidate.player.generation && first_entity.generation == 901 &&
			player.entity()->ref().generation == 902,
		"respawn preserves connection generation and replaces current entity");
	core::clear_player_entity(5);
	expect(!player.entity() && player.valid(), "entity free clears association without invalidating player");
	core::associate_player_entity(3, { 5, 0, 903 });
	core::invalidate_player_entities();
	expect(!player.entity() && player.valid(), "map entity invalidation preserves connection identity");
	core::associate_player_entity(3, { 5, 0, 904 });
	auto reader = std::async(std::launch::async,
		[api, player]
		{
			return !core::in_game_scope() && player.valid() && player.client_num() == 3 && player.name() == "Alice" &&
				player.entity()->ref().generation == 904 && api.players().size() == 1;
		});
	expect(reader.get(), "worker may read copied player state without gaining VM access");

	core::prepare_player(4, "Bob");
	core::activate_player(4);
	core::associate_player_entity(4, { 6, 0, 905 });
	const auto victim = core::find_player(3), killer = core::find_player(4);
	core::died_player(victim, killer, 17);
	expect(events.size() == 6 && events[4].type == IW8X_PLAYER_DEATH && events[5].type == IW8X_PLAYER_KILL &&
			events[4].player.client_num == 3 && events[4].other_player.client_num == 4 && events[5].player.client_num == 4 &&
			events[5].other_player.client_num == 3 && events[4].entity.generation == 904 && events[5].entity.generation == 905 &&
			events[4].reason == 17 && events[5].reason == 17 && events[4].flags == IW8X_EVENT_HAS_REASON &&
			events[5].flags == IW8X_EVENT_HAS_REASON,
		"death precedes kill with correctly oriented identities, entity and proven reason");
	core::died_player(victim, victim, 18);
	core::died_player(victim, {}, 19);
	expect(events.size() == 8 && events[6].type == IW8X_PLAYER_DEATH && events[7].type == IW8X_PLAYER_DEATH &&
			!events[6].other_player.generation && !events[7].other_player.generation,
		"suicide and world death never fabricate a killer");
	const auto before = core::disconnecting_player(3);
	expect(before.connected && player.valid() && player.name() == "Alice" && !core::disconnecting_player(3),
		"disconnecting preserves readable identity and rejects duplicate teardown");
	core::disconnected_player(before);
	expect(events.size() == 10 && events[8].type == IW8X_PLAYER_DISCONNECTING && validity[8] == IW8X_OK &&
			events[9].type == IW8X_PLAYER_DISCONNECTED && validity[9] == IW8X_STALE && !events[9].entity.generation,
		"disconnect invalidates identity before disconnected notification");
	{
		core::game_scope scope;
		expect(host.tell(candidate.player, string_view("after disconnect")) == IW8X_STALE, "chat tell rejects stale player identity");
	}
	const uint32_t expected[]{ IW8X_PLAYER_CONNECTING, IW8X_PLAYER_CONNECTED, IW8X_PLAYER_SPAWN, IW8X_PLAYER_SPAWN, IW8X_PLAYER_DEATH,
		IW8X_PLAYER_KILL, IW8X_PLAYER_DEATH, IW8X_PLAYER_DEATH, IW8X_PLAYER_DISCONNECTING, IW8X_PLAYER_DISCONNECTED };
	for (size_t i = 0; i < std::size(expected); ++i)
		expect(events[i].type == expected[i] && events[i].epoch == events.front().epoch, "player event order and epoch preserved");
	iw8x_entity stale_entity{ 5, 0, 999 };
	uint32_t slot = 3;
	expect(!player.valid() && host.player_client_num(candidate.player, &slot) == IW8X_STALE && slot == UINT32_MAX &&
			host.player_xuid(candidate.player, &xuid) == IW8X_STALE && host.player_entity(candidate.player, &stale_entity) == IW8X_STALE &&
			!stale_entity.generation &&
			host.player_name(
				candidate.player,
				[](void*, const iw8x_value*)
				{
				},
				nullptr) == IW8X_STALE,
		"all stale player accessors reject disconnected identities");
	bool stale_sdk{};
	try
	{
		(void)player.name();
	}
	catch (const api_error& error)
	{
		stale_sdk = error.code() == IW8X_STALE;
	}
	expect(stale_sdk, "SDK exposes stale player status");
	const auto replacement = core::connecting_player(3, "Carol");
	core::disconnected_player(before); // Delayed completion from the prior occupant.
	core::connected_player(3, "Carol");
	expect(replacement.player.generation != candidate.player.generation && !player.valid() && api.player_from(replacement.player).valid() &&
			api.players().size() == 2,
		"slot reuse receives new generation and ignores prior teardown completion");
	const auto rejected = core::connecting_player(7, "Rejected");
	const auto count = events.size();
	core::disconnected_player(core::disconnecting_player(7));
	core::connected_player(7, "Rejected");
	expect(events.size() == count && host.player_valid(rejected.player) == IW8X_STALE && api.players().size() == 2,
		"rejected candidate never emits connected or disconnected and is not enumerated");
	expect(vm_denied, "player lifecycle callbacks cannot mutate the VM");
	core::invalidate_players();
	expect(api.players().empty() && !api.player_from(replacement.player).valid(), "server reset invalidates remaining players");
	expect(core::unload_owner(owner) == IW8X_OK, "player lifecycle owner cleanup");
}

void event_reentrancy_and_scope()
{
	const auto owner = core::create_owner("event reentrancy");
	api api(&host, owner);
	std::promise<void> acquired;
	auto acquired_future = acquired.get_future();
	std::thread contender;
	bool unlocked{};
	api.subscribe(IW8X_SCRIPTS_LOADED,
		[&](const iw8x_event&)
		{
			contender = std::thread(
				[&]
				{
					(void)core::find_player(3, true);
					std::lock_guard lock(core::state().mutex);
					acquired.set_value();
				});
			// The contender is joined after dispatch, so a regression reports a
			// timeout instead of deadlocking while the callback still owns a lock.
			unlocked = acquired_future.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
		});
	emit(IW8X_SCRIPTS_LOADED);
	contender.join();
	expect(unlocked, "event callback executes without holding either registry mutex");
	std::vector<int> order;
	bool subscribed{};
	api.subscribe(IW8X_PLAYER_SPAWN,
		[&](const iw8x_event&)
		{
			order.push_back(1);
			if (!subscribed)
			{
				subscribed = true;
				api.subscribe(IW8X_PLAYER_SPAWN,
					[&](const iw8x_event&)
					{
						order.push_back(2);
					});
				emit(IW8X_PLAYER_SPAWN);
			}
		});
	emit(IW8X_PLAYER_SPAWN);
	expect(order == std::vector<int>({ 1, 1, 2 }), "nested emit observes new listener without changing outer snapshot");
	emit(IW8X_PLAYER_SPAWN);
	expect(order == std::vector<int>({ 1, 1, 2, 1, 2 }), "later dispatch includes reentrant subscription once");
	{
		core::game_scope forbidden(false);
		{
			core::game_scope nested(true);
			expect(!core::in_game_scope(), "nested scope cannot escalate lifecycle VM access");
		}
		expect(!core::in_game_scope(), "nested scope preserves outer VM restriction");
	}
	expect(!core::in_game_scope(), "scope exit removes engine authorization");
	expect(core::unload_owner(owner) == IW8X_OK, "reentrant listener cleanup");

	const auto throwing = core::create_owner("raw event exception");
	const auto observing = core::create_owner("event exception observer");
	iw8x::api observer(&host, observing);
	unsigned faults{}, delivered{};
	iw8x_registration token{};
	expect(host.subscribe(
			   throwing, IW8X_PLAYER_DEATH,
			   [](void* data, const iw8x_event*)
			   {
				   ++*static_cast<unsigned*>(data);
				   throw std::runtime_error("raw event failure");
			   },
			   &faults, nullptr, &token) == IW8X_OK,
		"raw event callback registered");
	observer.subscribe(IW8X_PLAYER_DEATH,
		[&](const iw8x_event&)
		{
			++delivered;
		});
	for (int i = 0; i < 4; ++i)
		emit(IW8X_PLAYER_DEATH);
	expect(faults == 3 && delivered == 4, "raw event exceptions contained and failing owner disabled independently");
	expect(core::unload_owner(throwing) == IW8X_OK && core::unload_owner(observing) == IW8X_OK, "faulted event owners unload safely");
}

void event_unload_wait()
{
	const auto owner = core::create_owner("event in flight");

	struct event_state
	{
		std::promise<void> entered, release;
		std::shared_future<void> released = release.get_future().share();
		std::atomic<unsigned> calls{};
		iw8x_plugin_id owner{};
		iw8x_status self_unload{};
		int destroyed{};
	} data;

	data.owner = owner;
	iw8x_registration token{};
	expect(host.subscribe(
			   owner, IW8X_PLAYER_CONNECTED,
			   [](void* pointer, const iw8x_event*)
			   {
				   auto& data = *static_cast<event_state*>(pointer);
				   ++data.calls;
				   data.self_unload = core::unload_owner(data.owner);
				   data.entered.set_value();
				   data.released.wait();
			   },
			   &data,
			   [](void* pointer)
			   {
				   ++static_cast<event_state*>(pointer)->destroyed;
			   },
			   &token) == IW8X_OK,
		"blocking event registration");
	auto entered = data.entered.get_future();
	auto callback = std::async(std::launch::async,
		[]
		{
			emit(IW8X_PLAYER_CONNECTED);
		});
	const auto started = entered.wait_for(std::chrono::seconds(2));
	if (started != std::future_status::ready)
	{
		data.release.set_value();
		callback.get();
		expect(false, "event callback started before unload test");
	}
	auto unload = std::async(std::launch::async,
		[=]
		{
			return core::unload_owner(owner);
		});
	const auto waiting = unload.wait_for(std::chrono::milliseconds(30));
	data.release.set_value();
	callback.get();
	const auto result = unload.get();
	expect(waiting == std::future_status::timeout && result == IW8X_OK && data.destroyed == 1 && data.self_unload == IW8X_BUSY,
		"event lease blocks unload until callback exits and userdata is destroyed once");
	emit(IW8X_PLAYER_CONNECTED);
	expect(data.calls == 1, "event callbacks cannot run after owner unload");
}
