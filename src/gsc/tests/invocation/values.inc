// Included once inside the existing instrumented test fixture.
void calls_and_values()
{
	iw8x::api api(&host, owner);
	status(IW8X_WRONG_THREAD,
		[&]
		{
			api.gsc().call("getdvar");
		});
	core::game_scope scope;
	const auto entity = core::capture_entity(5);
	expect(core::validate_entity(entity) == IW8X_OK, "getcurrentweapon receiver is valid before dispatch");
	const auto current = api.gsc().method(entity, "getcurrentweapon").as<iw8x::weapon>();
	expect(std::memcmp(current.words, fixture_weapon.words, 64) == 0,
		"getcurrentweapon decodes class 10 automatically, including the full descriptor and alternate flag");
	clean();
	expect(current.words[14] == 0xaabbccddu, "owned Weapon survives native pool release");
	const auto* weapon_bytes = reinterpret_cast<const uint8_t*>(current.words);
	expect(weapon_bytes[60] == 0 && weapon_bytes[61] == 0 && weapon_bytes[62] == 1,
		"Replay 60-byte descriptor excludes native alternate/padding; ABI alternate remains byte 62");
	auto malformed = current;
	reinterpret_cast<uint8_t*>(malformed.words)[60] = 1;
	status(IW8X_INVALID_ARGUMENT,
		[&]
		{
			api.gsc().call("getdvar", malformed);
		});
	clean();
	const auto echoed_weapon = api.gsc().call("getdvar", current).as<iw8x::weapon>();
	expect(std::memcmp(&current, &echoed_weapon, sizeof(current)) == 0, "generic builtin Weapon roundtrip");
	clean();
	null_weapon = true;
	const auto empty_weapon = api.gsc().method(entity, "getcurrentweapon").as<iw8x::weapon>();
	expect(empty_weapon.words[0] == 0 && empty_weapon.words[15] == 0, "no current weapon is a null Weapon, not a stale entity");
	expect(api.gsc().call("getdvar", empty_weapon).as<iw8x::weapon>().words[0] == 0, "null Weapon roundtrip");
	clean();
	null_weapon = false;
	unsupported_entity_class = true;
	status(IW8X_UNSUPPORTED,
		[&]
		{
			api.gsc().method(entity, "getcurrentweapon");
		});
	clean();
	unsupported_entity_class = false;
	expect(api.gsc().call("getdvar").as<int>() == 42, "zero argument global return");
	clean();
	expect(api.gsc().call("getdvar", 3, 7).as<int>() == 37, "reverse argument push order");
	clean();
	{
		std::array<value, 4> script_args{ value("iw8_sn_alpha50_mp"), value(iw8x::array{}), value("none"), value("none") };
		value result;
		expect(core::call_script_native(0x1234, IW8X_WEAPON_SNAPSHOT, script_args, result) == IW8X_OK, "stock script invocation succeeds");
		const auto weapon = result.as<iw8x::weapon>();
		expect(weapon.words[0] == 0x7fc00001u && weapon.words[1] == 0xdeadbeefu && weapon.words[2] == 0x12345678u,
			"script weapon return remains opaque and lossless");
		clean();
		std::array<value, 1> weapon_arg{ value(weapon) };
		expect(core::call_script_native(0x1234, IW8X_INTEGER, weapon_arg, result) == IW8X_OK && result.as<int>() == 77,
			"opaque weapon can be passed back into VM without float conversion");
		clean();
	}
	expect(api.gsc().call("setdvar", "temporary", 1).type() == iw8x::gsc::value_type::undefined, "no return becomes undefined");
	clean();
	expect(api.gsc().method(entity, "setorigin", iw8x::vec3{ 1, 2, 3 }).as<iw8x::vec3>().z == 3, "method vector argument/return");
	clean();
	std::vector<value> values{ value{}, -21, 1.5f, true, std::string("temporary"), iw8x::vec3{ 4, 5, 6 }, entity,
		iw8x::array{ 1, "hello", iw8x::vec3{ 1, 2, 3 }, current },
		iw8x::struct_value{ { iw8x::canonical_field(0x2c5), iw8x::array{ true, "owned", entity, current } } } };
	for (const auto& v : values)
	{
		const auto out = api.gsc().call("getdvar", v);
		expect(out.type() == (v.type() == iw8x::gsc::value_type::boolean ? iw8x::gsc::value_type::integer : v.type()),
			"all supported VM types");
		if (v.type() == iw8x::gsc::value_type::boolean) expect(out.as<bool>(), "VM integer bool conversion");
		clean();
	}
	{
		const auto keys = api.gsc().call("getarraykeys", iw8x::array{ 1, 2, 3 }).as_array();
		expect(keys.size() == 3 && keys[0].as<int>() == 0 && keys[1].as<int>() == 1 && keys[2].as<int>() == 2,
			"collection argument survives callee outparam clear");
		clean();
	}
	auto snapshot = api.gsc().call("getentarray", values.back());
	expect(snapshot.as_struct().at(iw8x::canonical_field(0x2c5)).as_array()[1].as<std::string>() == "owned",
		"nested strings survive engine cleanup");
	clean();
	const auto nested_weapon = snapshot.as_struct().at(iw8x::canonical_field(0x2c5)).as_array()[3].as<iw8x::weapon>();
	expect(std::memcmp(&nested_weapon, &current, sizeof(current)) == 0, "nested Weapon autodetection preserves full snapshot");
	status(IW8X_NOT_FOUND,
		[&]
		{
			api.gsc().call("not_a_function");
		});
	status(IW8X_NOT_FOUND,
		[&]
		{
			api.gsc().method(entity, "not_a_method");
		});
	status(IW8X_STALE,
		[&]
		{
			api.gsc().method(iw8x_entity{}, "setorigin");
		});
	status(IW8X_INVALID_ARGUMENT,
		[&]
		{
			api.gsc().call("bad name");
		});
	status(IW8X_INVALID_ARGUMENT,
		[&]
		{
			api.gsc().call("getdvar", std::string("a\0b", 3));
		});
	auto* saved = top(context());
	outputs(context()) = 2;
	inputs(context()) = 3;
	status(IW8X_BUSY,
		[&]
		{
			api.gsc().call("getdvar");
		});
	expect(top(context()) == saved && outputs(context()) == 2 && inputs(context()) == 3, "busy call preserves pending input/return state");
	inputs(context()) = outputs(context()) = 0;
	api.gsc().call("gettime");
	expect(nested_status == IW8X_BUSY, "nested invocation rejected");
	clean();
	fail_script = true;
	status(IW8X_CALLBACK_ERROR,
		[&]
		{
			api.gsc().call("getdvar", values.back());
		});
	fail_script = false;
	clean();
	throw_cpp = true;
	status(IW8X_INTERNAL_ERROR,
		[&]
		{
			api.gsc().call("getdvar", values.back());
		});
	throw_cpp = false;
	clean();
	fail_after_return = true;
	status(IW8X_CALLBACK_ERROR,
		[&]
		{
			api.gsc().call("getdvar", values.back());
		});
	fail_after_return = false;
	clean();
	throw_push = true;
	status(IW8X_INTERNAL_ERROR,
		[&]
		{
			api.gsc().call("getdvar", iw8x::array{ 1, "throw" });
		});
	throw_push = false;
	clean();
	fail_field = 2;
	status(IW8X_CALLBACK_ERROR,
		[&]
		{
			api.gsc().call("getdvar",
				iw8x::struct_value{ { iw8x::canonical_field(0xd4), "value" }, { iw8x::canonical_field(0x2c5), iw8x::array{ 1, 2 } } });
		});
	clean();
	expect(iw8x::canonical_field(0x2c5) == "@0002c5" && iw8x::canonical_field_id("@0002c5") == 0x2c5,
		"struct canonical IDs are distinct from string tokens");
	status(IW8X_UNSUPPORTED,
		[&]
		{
			api.gsc().call("getdvar", iw8x::struct_value{ { "unmapped_name", 1 } });
		});
	clean();
	expect(core::validate_return(iw8x::array{ 1 }) == IW8X_UNSUPPORTED, "unprotected rich callback return is rejected");
	expect(core::notify_native({}, "event", std::array<value, 1>{ iw8x::array{ 1 } }) == IW8X_UNSUPPORTED,
		"unprotected rich notify is rejected before VM mutation");
	clean();
	value too_deep(1);
	for (int i = 0; i < 34; ++i)
		too_deep = value(iw8x::array{ std::move(too_deep) });
	status(IW8X_LIMIT,
		[&]
		{
			api.gsc().call("getdvar", too_deep);
		});
	clean();
	sparse_result = true;
	status(IW8X_UNSUPPORTED,
		[&]
		{
			api.gsc().call("getdvar");
		});
	sparse_result = false;
	clean();
	cycle_result = true;
	status(IW8X_UNSUPPORTED,
		[&]
		{
			api.gsc().call("getdvar");
		});
	cycle_result = false;
	// The engine owns this deliberate cycle; remove its original cyclic edge.
	for (auto it = objects.begin(); it != objects.end(); ++it)
		if (it->first != 1)
		{
			auto id = it->first;
			it->second.children.clear();
			remove_ref(nullptr, 1, id);
			break;
		}
	clean();
	std::thread wrong(
		[&]
		{
			status(IW8X_WRONG_THREAD,
				[&]
				{
					api.gsc().call("getdvar");
				});
		});
	wrong.join();
	// A script reload can retire Weapon snapshots without retiring the player.
	const auto before_reload = native_calls;
	core::invalidate_script_exports();
	status(IW8X_STALE,
		[&]
		{
			api.gsc().call("getdvar", current);
		});
	clean();
	expect(native_calls == before_reload && core::validate_entity(entity) == IW8X_OK,
		"old Weapon is rejected before entering the VM while its player remains valid");
	const auto refreshed = api.gsc().method(entity, "getcurrentweapon").as<iw8x::weapon>();
	expect(refreshed.script_generation != current.script_generation && std::memcmp(refreshed.words, current.words, 64) == 0,
		"getcurrentweapon refreshes automatically after script reload");
	expect(
		api.gsc().call("getdvar", refreshed).as<iw8x::weapon>().words[14] == 0xaabbccddu, "fresh Weapon can be passed back after reload");
	clean();
	core::invalidate_entity(5);
	status(IW8X_STALE,
		[&]
		{
			api.gsc().method(entity, "setorigin");
		});
	const auto before_stale_receiver = native_calls;
	status(IW8X_STALE,
		[&]
		{
			api.gsc().method(entity, "getcurrentweapon");
		});
	expect(native_calls == before_stale_receiver, "genuinely stale getcurrentweapon receiver never enters VM");
	status(IW8X_STALE,
		[&]
		{
			api.gsc().call("getdvar", entity);
		});
	clean();
	expect(core::unload_owner(owner) == IW8X_OK, "unload with retained owned collection snapshots");
	expect(snapshot.as_struct().at(iw8x::canonical_field(0x2c5)).as_array()[1].as<std::string>() == "owned",
		"owned snapshot survives plugin owner teardown");
	status(IW8X_STOPPING,
		[&]
		{
			api.gsc().call("getdvar");
		});
}
