// Included once inside the existing instrumented test fixture.
void dvars()
{
	owner = core::create_owner("typed dvar tests");
	iw8x::api api(&host, owner);
	status(IW8X_WRONG_THREAD,
		[&]
		{
			api.dvars().get_bool("cl_textChatEnabled");
		});
	core::game_scope scope;
	expect(std::size(iw8x::dvar_names::replay) == 6724, "all readable dvars imported");
	expect(iw8x::dvar_names::resolve("cl_textChatEnabled") == "NLPSQMQRTP", "readable mapping");
	expect(iw8x::dvar_names::resolve("NLPSQMQRTP") == "NLPSQMQRTP", "known hashed fallback");
	expect(iw8x::dvar_names::resolve("NLPSQMQRTP", false).empty(), "fallback can be disabled");
	expect(api.dvars().get_string("cl_textChatEnabled") == "1" && requested_name == "NLPSQMQRTP", "typed header maps before engine call");
	dvar[core::replay::layout::dvar_type] = 0;
	expect(api.dvars().get_bool("NLPSQMQRTP"), "native bool accessor");
	dvar[core::replay::layout::dvar_type] = 5;
	expect(api.dvars().get_int("cl_textChatEnabled") == -72, "native int accessor");
	dvar[core::replay::layout::dvar_type] = 8;
	expect(api.dvars().get_int("cl_textChatEnabled") == -72, "native enum accessor");
	dvar[core::replay::layout::dvar_type] = 1;
	expect(api.dvars().get_float("cl_textChatEnabled") == 1.25f, "native float accessor XMM0 ABI");
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.dvars().get_bool("cl_textChatEnabled");
		});
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.dvars().get_int("cl_textChatEnabled");
		});
	dvar[core::replay::layout::dvar_type] = 0;
	status(IW8X_TYPE_MISMATCH,
		[&]
		{
			api.dvars().get_float("cl_textChatEnabled");
		});
	status(IW8X_NOT_FOUND,
		[&]
		{
			api.dvars().get_string("unknown_dvar");
		});
	dvar_exists = false;
	status(IW8X_NOT_FOUND,
		[&]
		{
			api.dvars().get_string("cl_textChatEnabled");
		});
	dvar_exists = true;
	auto legacy = host;
	legacy.size = 256;
	iw8x::api old_host(&legacy, owner);
	status(IW8X_UNSUPPORTED,
		[&]
		{
			old_host.gsc().call("getdvar");
		});
	status(IW8X_UNSUPPORTED,
		[&]
		{
			old_host.dvars().get_string("cl_textChatEnabled");
		});
	expect(core::unload_owner(owner) == IW8X_OK, "typed dvar owner unload");
}
