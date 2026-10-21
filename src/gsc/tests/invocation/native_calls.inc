// Included once inside the existing instrumented test fixture.
void global(void* ctx)
{
	++native_calls;
	if (fail_script) std::longjmp(script_jump, 1);
	if (throw_cpp) throw std::runtime_error("injected native C++ exception");
	if (fail_after_return)
	{
		add_string(ctx, "partial result");
		std::longjmp(script_jump, 1);
	}
	if (cycle_result)
	{
		make_array(ctx);
		auto id = uint32_t(top(ctx)->payload);
		auto& data = objects.at(id);
		data.children[0x800000] = { id, 1 };
		++data.refs;
		return;
	}
	if (sparse_result)
	{
		make_array(ctx);
		add_int(ctx, 7);
		add_child(ctx, 0x800005);
		return;
	}
	if (!argc(ctx))
	{
		add_int(ctx, 42);
		return;
	}
	if (argc(ctx) == 2)
	{
		const auto a = arg(ctx, 0), b = arg(ctx, 1);
		expect(a.type == 6 && b.type == 6, "multiple argument order/types");
		add_int(ctx, int(a.payload * 10 + b.payload));
		return;
	}
	return_raw(ctx, arg(ctx, 0));
}

void array_keys_like(void* ctx)
{
	++native_calls;
	const auto source = arg(ctx, 0);
	expect(source.type == 1, "array key source is pointer");
	const auto source_id = uint32_t(source.payload);
	const auto count = uint32_t(objects.at(source_id).children.size());
	// Creating a return array clears the input params.  The production bridge
	// must hold source_id independently so the temporary C++ array survives
	// this clear just like a normal script caller-owned array would.
	make_array(ctx);
	expect(objects.contains(source_id), "collection source retained across outparam clear");
	expect(objects.at(source_id).children.size() == count, "collection children retained across outparam clear");
	for (uint32_t i = 0; i < count; ++i)
	{
		add_int(ctx, int(i));
		add_array(ctx);
	}
}

void method(void* ctx, uint64_t self)
{
	expect(self == 5, "method receiver uses packed Win64 entity ABI");
	global(ctx);
}

void current_weapon(void* ctx, uint64_t self)
{
	expect(self == 5 && argc(ctx) == 0, "getcurrentweapon has only a world-entity receiver");
	++native_calls;
	// Replay GScr_Weapon_AddParam -> Scr_AddEntityNum(weapon ID, class 10).
	// The weapon ID is not a live gentity index.
	if (unsupported_entity_class)
	{
		add_entity(ctx, 1, 7);
		return;
	}
	const iw8x::weapon empty{};
	add_weapon(ctx, null_weapon ? empty.words : fixture_weapon.words, !null_weapon);
	// scr_entref_t has three uninitialized padding bytes above the class.
	objects.at(uint32_t(top(ctx)->payload)).self |= 0xabcdef0000000000ull;
}

void no_return(void*)
{
	++native_calls;
}

void nested(void* ctx)
{
	value output;
	nested_status = core::call_builtin_native(false, {}, "getdvar", {}, output);
	add_int(ctx, 1);
}

void execute_script(void* ctx, uint32_t handle, uint32_t count)
{
	++script_calls;
	expect(!export_window, "script calls happen after the export lookup window");
	if (handle == 0x7100 + script_offset || handle == 0x7200 + script_offset)
	{
		expect(count == 0 && night_module_loaded, "isnightmap and its stock far-call dependency are loaded");
		*++top(ctx) = { 1, 6 };
		inputs(ctx) = 0;
		outputs(ctx) = 1;
		return;
	}
	if (handle == 0x4000 + script_offset || handle == 0x5000 + script_offset)
	{
		expect(count <= 1 && inputs(ctx) == count && !outputs(ctx), "echo script inparams");
		auto result = count ? *top(ctx) : raw{};
		add_ref(result);
		auto* floor = top(ctx) - count;
		while (top(ctx) > floor)
		{
			remove_ref(ctx, top(ctx)->type, top(ctx)->payload);
			*top(ctx)-- = {};
		}
		*++top(ctx) = result;
		inputs(ctx) = 0;
		outputs(ctx) = 1;
		return;
	}
	expect(handle == 0x1234 + script_offset, "current generation script handle forwarded");
	auto* current = top(ctx);
	if (count == 4)
	{
		expect(inputs(ctx) == 4 && !outputs(ctx), "script arguments published as inparams");
		expect(current[0].type == 2 && strings.at(uint32_t(current[0].payload)).text == "iw8_sn_alpha50_mp", "script first argument order");
		expect(current[-1].type == 1 && objects.at(uint32_t(current[-1].payload)).type == 23, "script array argument");
		expect(current[-2].type == 2 && current[-3].type == 2, "script trailing string arguments");
	}
	else
	{
		expect(count == 1 && inputs(ctx) == 1 && current->type == 1, "Weapon re-marshalled as VAR_POINTER");
		const auto ref = objects.at(uint32_t(current->payload)).self;
		expect(uint8_t(ref >> 32) == 10 && std::memcmp(get_weapon(ctx, uint32_t(ref)), fixture_weapon.words, 60) == 0 &&
				weapon_alternate(ctx, uint32_t(ref)),
			"all Weapon bytes and alternate mode preserved into VM");
	}

	auto* floor = current - count;
	while (current > floor)
	{
		remove_ref(ctx, current->type, current->payload);
		*current-- = {};
	}
	top(ctx) = floor;
	inputs(ctx) = 0;
	outputs(ctx) = 0;
	if (count == 4)
	{
		add_weapon(ctx, fixture_weapon.words, true);
		inputs(ctx) = 0;
		outputs(ctx) = 1;
	}
	else
	{
		*++top(ctx) = { 77, 6 };
		outputs(ctx) = 1;
	}
}

uint32_t load_script(void* ctx, const char* path)
{
	expect(export_window && ctx == context(), "script assets loaded only at safe edge");
	++script_loads;
	// Retail far-call module token 0x08C4 is formatted as decimal "2244".
	// The engine loader also links referenced modules for literal-name assets.
	if (std::string_view(path) == "2244" || std::string_view(path) == "scripts/tests/night_forward") night_module_loaded = true;
	return 1;
}

uint32_t execute_script_on(
	void* ctx, uint32_t handle, uint32_t self, uint32_t count, void (*receive)(void*, const void*, void*), const void* input, void* output)
{
	expect(objects.at(self).type == 22 && objects.at(self).self == 5, "script receiver is the player object, not a positional argument");
	expect(objects.at(self).refs == 1, "host owns receiver throughout invocation");
	add_object_ref(ctx, self);
	const auto thread = alloc_thread(ctx, self);
	++field<uint32_t>(ctx, 0x3518);
	execute_script(ctx, handle, count);
	--field<uint32_t>(ctx, 0x3518);
	// Scr_ExecThreadInternal exposes one inparam until its return callback ends.
	inputs(ctx) = 1;
	outputs(ctx) = 0;
	if (unsupported_script_result)
	{
		remove_ref(ctx, top(ctx)->type, top(ctx)->payload);
		*top(ctx) = { 0, 7 };
	}
	receive(ctx, input, output);
	remove_ref(ctx, top(ctx)->type, top(ctx)->payload);
	*top(ctx)-- = {};
	--inputs(ctx);
	return thread;
}

uint32_t get_script(void* ctx, const char* path, uint32_t token)
{
	expect(export_window && ctx == context(), "script exports looked up only at safe edge");
	++script_lookups;
	if (invalidate_during_lookup)
	{
		invalidate_during_lookup = false;
		core::invalidate_script_exports();
	}
	const std::string_view script(path);
	module_attempts.emplace_back(script);
	if (script == "2244" && token == 0x72B6 && night_module_loaded && !missing_night_export) return 0x7100 + script_offset;
	if (script == "scripts/tests/night_forward" && token == 0x254) return 0x7200 + script_offset;
	if (script == "scripts/cp/cp_weapon" && token == 0x24C5 && !missing_weapon) return 0x1234 + script_offset;
	if (script == "scripts/tests/echo" && token == 0x254) return 0x4000 + script_offset;
	if (script == "scripts/tests/extension.gsc" && token == 0x254) return 0x5000 + script_offset;
	return 0;
}

void resolve_exports()
{
	export_window = true;
	core::resolve_script_exports(context());
	export_window = false;
}
