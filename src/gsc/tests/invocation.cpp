#include "gsc/internal/replay.hpp"
#include "gsc/internal/chat.hpp"
#include "gsc/internal/port_tables.hpp"
#include "diagnostics/night/night_builtin_tables.h"
// Production VM/dvar adapters with instrumented engine entry points. These
// tests prove host behavior; the separately recorded IDA evidence proves ABI.
#include "gsc/api.hpp"
#include "gsc/internal/runtime.hpp"
#include "gsc/internal/state.hpp"
#include "gsc/internal/stock_builtins.hpp"
#include "dedicated/runtime/commands.hpp"
#include "plugin/sdk/gsc.hpp"

#include "engine/image.hpp"

#include "gsc/internal/bindings.cpp" // Exercise the real native callback wrappers.
#include <Windows.h>

#include <array>
#include <atomic>
#include <csetjmp>
#include <cstring>
#include <future>
#include <fstream>

#include "thirdparty/json.hpp"

#include <iostream>
#include <map>
#include <thread>
namespace
{
	std::atomic<unsigned> dedicated_restart_vm_pumps{};
	std::atomic<bool> dedicated_restart_scope_valid{true};
}

// The real native dedicated restart bridge is linked into the production
// DLL. Its test-only boundary is observed here without faking a GSC builtin.
namespace dedicated::detail
{
	void PumpMapRestartVm() noexcept
	{
		dedicated_restart_vm_pumps.fetch_add(1, std::memory_order_relaxed);
		if (!iw8x::gsc::internal::in_game_scope())
			dedicated_restart_scope_valid.store(false, std::memory_order_relaxed);
	}
}

namespace
{
	namespace core = iw8x::gsc::internal;
	namespace old = core::replay;
	namespace mapped = core::replay;
	using iw8x::value;

	struct raw
	{
		uint64_t payload{};
		uint8_t type{};
		uint8_t pad[7]{};
	};

	uintptr_t image{};
	unsigned checks{}, native_calls{}, releases{}, string_creations{};
	unsigned script_calls{};
	unsigned script_lookups{}, script_loads{};
	uint32_t script_offset{};
	bool export_window{}, missing_weapon{}, invalidate_during_lookup{};
	bool unsupported_script_result{};
	uint32_t next_string = 1, next_object = 2, next_child = 1;

	struct string_entry
	{
		std::string text;
		int refs{};
	};

	struct object_entry
	{
		uint8_t type{};
		int refs{ 1 };
		uint64_t self{};
		std::map<uint32_t, raw> children;
	};

	std::map<uint32_t, string_entry> strings;
	std::map<uint32_t, object_entry> objects;

	struct child_entry
	{
		uint32_t parent, key;
	};

	std::map<uint32_t, child_entry> children;
	std::map<uint64_t, uint32_t> child_ids;
	std::map<uint64_t, int> vectors;
	std::map<uint32_t, std::array<uint8_t, 62>> weapons;
	bool night_module_loaded{}, missing_night_export{};
	std::vector<std::string> module_attempts;
	std::vector<std::string> resolution_logs;

	void capture_resolution_log(const char*, const char* message)
	{
		if (std::string_view(message).find("[PluginPerf]") != 0) resolution_logs.emplace_back(message);
	}

	constexpr iw8x::weapon fixture_weapon{ { 0x7fc00001u, 0xdeadbeefu, 0x12345678u, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0xaabbccddu,
		0x00010000u } };
	bool null_weapon{}, unsupported_entity_class{};
	std::array<uint8_t, 2048> in_use{};
	std::array<uint8_t, 24> object_allocator{};
	std::jmp_buf script_jump;
	bool throw_push{}, throw_cpp{}, fail_script{}, fail_after_return{}, sparse_result{}, cycle_result{};
	unsigned fail_field{};
	iw8x_status nested_status{};
	iw8x::plugin_id owner{};
	const auto& host = iw8x::gsc::host_api();

	template <class T>
	T& field(void* ctx, size_t offset)
	{
		return *reinterpret_cast<T*>(static_cast<uint8_t*>(ctx) + offset);
	}

	void expect(bool condition, const char* name)
	{
		++checks;
		if (!condition) throw std::runtime_error(name);
	}

	void* context()
	{
		return game::at(game::addresses::gsc::kServerContextRva);
	}

	raw*& top(void* ctx)
	{
		return field<raw*>(ctx, core::replay::layout::stack_top);
	}

	uint32_t& inputs(void* ctx)
	{
		return field<uint32_t>(ctx, core::replay::layout::inparam_count);
	}

	uint32_t& outputs(void* ctx)
	{
		return field<uint32_t>(ctx, core::replay::layout::outparam_count);
	}

	void add_ref(raw v)
	{
		if (v.type == 1) ++objects.at(uint32_t(v.payload)).refs;
		if (v.type == 2) ++strings.at(uint32_t(v.payload)).refs;
		if (v.type == 4) ++vectors.at(v.payload);
	}

	void remove_ref(void*, uint32_t type, uint64_t payload)
	{
		++releases;
		if (type == 2)
		{
			auto it = strings.find(uint32_t(payload));
			expect(it != strings.end() && it->second.refs > 0, "string reference valid");
			if (!--it->second.refs) strings.erase(it);
		}
		else if (type == 4)
		{
			auto it = vectors.find(payload);
			expect(it != vectors.end() && it->second > 0, "vector reference valid");
			if (!--it->second)
			{
				delete reinterpret_cast<iw8x::vec3*>(payload);
				vectors.erase(it);
			}
		}
		else if (type == 1)
		{
			auto it = objects.find(uint32_t(payload));
			expect(it != objects.end() && it->second.refs > 0, "object reference valid");
			if (!--it->second.refs)
			{
				const auto data = std::move(it->second);
				objects.erase(it);
				for (auto [key, v] : data.children)
				{
					remove_ref(nullptr, v.type, v.payload);
					if (data.type == 23 && key < 0x100000) remove_ref(nullptr, 2, key);
				}
				if (data.type == 16) remove_ref(nullptr, 1, data.self);
				if (data.type == 22 && uint8_t(data.self >> 32) == 10 && uint32_t(data.self))
					expect(weapons.erase(uint32_t(data.self)) == 1, "script Weapon released with its VM object");
			}
		}
	}

	void clear(void* ctx)
	{
		const auto count = outputs(ctx);
		outputs(ctx) = 0;
		auto* value = top(ctx);
		top(ctx) -= count;
		for (uint32_t i = 0; i < count; ++i)
		{
			remove_ref(ctx, value->type, value->payload);
			*value-- = {};
		}
	}

	raw* increment(void* ctx)
	{
		clear(ctx);
		++inputs(ctx);
		return ++top(ctx);
	}

	void add_undefined(void* ctx)
	{
		*increment(ctx) = {};
	}

	void add_int(void* ctx, int v)
	{
		*increment(ctx) = { uint32_t(v), 6 };
	}

	void add_float(void* ctx, float v)
	{
		uint32_t bits;
		std::memcpy(&bits, &v, 4);
		*increment(ctx) = { bits, 5 };
	}

	uint32_t intern(const char* s, uint32_t)
	{
		++string_creations;
		for (auto& [id, entry] : strings)
			if (entry.text == s)
			{
				++entry.refs;
				return id;
			}
		const auto id = next_string++;
		strings.emplace(id, string_entry{ s, 1 });
		return id;
	}

	const char* convert(uint32_t token)
	{
		return strings.at(token).text.c_str();
	}

	void add_string(void* ctx, const char* s)
	{
		if (throw_push) throw std::runtime_error("injected push failure");
		auto* v = increment(ctx);
		*v = { intern(s, 0), 2 };
	}

	void add_vector(void* ctx, const float* v)
	{
		auto* output = increment(ctx);
		const auto p = reinterpret_cast<uintptr_t>(new iw8x::vec3{ v[0], v[1], v[2] });
		vectors[p] = 1;
		*output = { p, 4 };
	}

	uint32_t object(uint8_t type)
	{
		const auto id = next_object++;
		objects.emplace(id, object_entry{ type });
		return id;
	}

	void make_array(void* ctx)
	{
		*increment(ctx) = { object(23), 1 };
	}

	void make_struct(void* ctx)
	{
		*increment(ctx) = { object(20), 1 };
	}

	void add_child(void* ctx, uint32_t key)
	{
		const auto v = *top(ctx);
		--top(ctx);
		--inputs(ctx);
		auto& data = objects.at(uint32_t(top(ctx)->payload));
		expect(!data.children.contains(key), "new child key");
		data.children[key] = v;
		if (data.type == 23 && key < 0x100000) add_ref({ key, 2 });
	}

	void add_array(void* ctx)
	{
		const auto parent = uint32_t((top(ctx) - 1)->payload);
		add_child(ctx, 0x800000 + uint32_t(objects.at(parent).children.size()));
	}

	void add_field(void* ctx, uint32_t name)
	{
		if (fail_field && !--fail_field) std::longjmp(script_jump, 1);
		add_child(ctx, name);
	}

	void add_entity(void* ctx, int index, uint8_t entity_class)
	{
		const auto id = object(22);
		objects.at(id).self = uint32_t(index) | (uint64_t(entity_class) << 32);
		*increment(ctx) = { id, 1 };
	}

	uint64_t entity_ref(void*, uint32_t id)
	{
		return objects.at(id).self;
	}

	void add_weapon(void* ctx, const void* descriptor, bool alternate)
	{
		uint32_t id{};
		if (*static_cast<const uint16_t*>(descriptor))
		{
			id = 1;
			while (weapons.contains(id))
				++id;
			auto& bytes = weapons[id];
			std::memcpy(bytes.data(), descriptor, 60);
			bytes[60] = alternate;
			bytes[61] = 0xA7;
		}
		add_entity(ctx, id, 10);
	}

	const void* get_weapon(void*, uint32_t id)
	{
		static const std::array<uint8_t, 62> empty{};
		return id ? weapons.at(id).data() : empty.data();
	}

	bool weapon_alternate(void*, uint32_t id)
	{
		return id && weapons.at(id)[60];
	}

	uint8_t object_type(void*, uint32_t id)
	{
		return objects.at(id).type;
	}

	uint32_t array_size(void*, uint32_t id)
	{
		return uint32_t(objects.at(id).children.size());
	}

	uint32_t child_id(uint32_t parent, uint32_t key)
	{
		const uint64_t identity = (uint64_t(parent) << 32) | key;
		auto& id = child_ids[identity];
		if (!id)
		{
			id = next_child++;
			children.emplace(id, child_entry{ parent, key });
		}
		return id;
	}

	uint32_t allocate_array(void*)
	{
		return object(23);
	}

	uint32_t allocate_struct(void*)
	{
		return object(20);
	}

	uint32_t variable(void*, uint32_t parent, uint32_t key)
	{
		if (fail_field && !--fail_field) std::longjmp(script_jump, 1);
		auto& entry = objects.at(parent);
		expect(!entry.children.contains(key), "collection field allocated before value transfer");
		entry.children[key] = {};
		return child_id(parent, key);
	}

	uint32_t array_variable(void* ctx, uint32_t parent, uint32_t index)
	{
		return variable(ctx, parent, (index - 0x800000) & 0xffffff);
	}

	void set_new(void* ctx, uint32_t id, const raw* v)
	{
		// Retail Scr_AddArray/Scr_AddStructField remove the source VariableValue
		// from the VM stack before transferring it into the child variable. This
		// catches the subtle ordering bug that only showed up with nested values
		// in the offline Replay ABI fixture.
		expect(top(ctx) + 1 == v, "collection value popped before variable transfer");
		const auto child = children.at(id);
		objects.at(child.parent).children.at(child.key) = *v;
	}

	uint32_t first(void*, uint32_t parent)
	{
		const auto& values = objects.at(parent).children;
		return values.empty() ? 0 : child_id(parent, values.begin()->first);
	}

	uint32_t next(void*, uint32_t id)
	{
		const auto [parent, key] = children.at(id);
		const auto& values = objects.at(parent).children;
		const auto it = values.upper_bound(key);
		return it == values.end() ? 0 : child_id(parent, it->first);
	}

	uint32_t name(void*, uint32_t id)
	{
		return children.at(id).key;
	}

	uint64_t evaluate(void* ctx, uint32_t id, raw* out)
	{
		expect(ctx == context(), "Replay EvalVariable context is the first argument");
		const auto [parent, key] = children.at(id);
		*out = objects.at(parent).children.at(key);
		add_ref(*out);
		return out->payload;
	}

	void add_object_ref(void*, uint32_t id)
	{
		add_ref({ id, 1 });
	}

	void release_object(void* ctx, uint32_t id)
	{
		remove_ref(ctx, 1, id);
	}

	uint32_t alloc_thread(void*, uint32_t self)
	{
		const auto id = object(16);
		objects.at(id).self = self;
		return id;
	}

	uint32_t argc(void* ctx)
	{
		return outputs(ctx);
	}

	raw arg(void* ctx, uint32_t index)
	{
		expect(index < outputs(ctx), "builtin argument exists");
		return top(ctx)[-int(index)];
	}

	void return_raw(void* ctx, raw v)
	{
		add_ref(v);
		*increment(ctx) = v;
	}
#include "invocation/native_calls.inc"
	// This fixture owns its jump destination, as the actual VM interpreter does.
	// No C++ owner exists between setjmp and the production host trampoline.
#include "invocation/protected_execution.inc"
	void commit(uintptr_t rva, size_t size)
	{
		const auto start = rva & ~uintptr_t(4095);
		if (!VirtualAlloc(
				reinterpret_cast<void*>(image + start), (rva - start + size + 4095) & ~size_t(4095), MEM_COMMIT, PAGE_EXECUTE_READWRITE))
			throw std::runtime_error("fixture allocation");
	}

	template <class F>
	void stub(uintptr_t rva, F fn)
	{
		commit(rva, 16);
		uint8_t code[]{ 0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0 };
		const auto address = reinterpret_cast<uintptr_t>(fn);
		std::memcpy(code + 2, &address, 8);
		std::memcpy(game::at(rva), code, sizeof(code));
		FlushInstructionCache(GetCurrentProcess(), game::at(rva), sizeof(code));
	}

	std::array<uint8_t, 64> dvar{};
	bool dvar_exists = true;
	std::string requested_name;

	void* find_dvar(const char* name)
	{
		requested_name = name;
		return dvar_exists ? dvar.data() : nullptr;
	}

	bool dvar_bool(const char*)
	{
		return true;
	}

	int dvar_int(const char*)
	{
		return -72;
	}

	float dvar_float(const char*)
	{
		return 1.25f;
	}

	const char* dvar_string(const char*)
	{
		return "1";
	}

	void setup()
	{
		image = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, 0x18000000, MEM_RESERVE, PAGE_NOACCESS));
		expect(image != 0, "reserve fixture image");
		commit(game::addresses::gsc::kServerContextRva, 0x26000);
		commit(game::addresses::gsc_api::entity_in_use_pointer, sizeof(void*));
		*game::at<uint8_t*>(game::addresses::gsc_api::entity_in_use_pointer) = in_use.data();
		in_use[5] = 1;
		auto* ctx = context();
		top(ctx) = reinterpret_cast<raw*>(static_cast<uint8_t*>(ctx) + 0xC3D0);
		field<raw*>(ctx, core::replay::layout::stack_max) = top(ctx) + 1024;
		field<uint32_t>(ctx, 0x34FC) = 1;
		objects.emplace(1, object_entry{ 20 });
		*reinterpret_cast<uint32_t*>(object_allocator.data() + 4) = 1;
		field<void*>(ctx, 0x34A8) = object_allocator.data();
		namespace native = game::addresses::gsc_api;
		commit(native::native_functions, native::stock_function_count * sizeof(uintptr_t));
		commit(native::native_methods, native::stock_method_count * sizeof(uintptr_t));
		auto* functions = game::at<uintptr_t>(native::native_functions);
		auto* methods = game::at<uintptr_t>(native::native_methods);
		field<void*>(ctx, native::layout::functions) = functions;
		field<void*>(ctx, native::layout::methods) = methods;
		field<uint32_t>(ctx, native::layout::function_base) = native::stock_function_base;
		field<uint32_t>(ctx, native::layout::function_count) = native::stock_function_count;
		field<uint32_t>(ctx, native::layout::function_end) = native::stock_function_base + native::stock_function_count;
		field<uint32_t>(ctx, native::layout::method_base) = native::stock_method_base;
		field<uint32_t>(ctx, native::layout::method_count) = native::stock_method_count;
		field<uint32_t>(ctx, native::layout::method_end) = native::stock_method_base + native::stock_method_count;
		for (auto name : { "getdvar", "getentarray", "setdvar", "gettime", "getarraykeys" })
		{
			const auto* entry = core::find_builtin(false, name);
			expect(entry != nullptr, "known stock function name");
			functions[entry->id - 1] = image + entry->rva;
			stub(entry->rva,
				name == std::string_view("setdvar") ? &no_return
					: name == std::string_view("gettime") ? &nested
					: name == std::string_view("getarraykeys") ? &array_keys_like
					: &global);
		}
		const auto* entry = core::find_builtin(true, "setorigin");
		methods[entry->id - 0x8000] = image + entry->rva;
		stub(entry->rva, &method);
		entry = core::find_builtin(true, "getcurrentweapon");
		methods[entry->id - 0x8000] = image + entry->rva;
		stub(entry->rva, &current_weapon);
		stub(old::Scr_AddUndefined.rva, &add_undefined);
		stub(old::Scr_AddInt.rva, &add_int);
		stub(old::Scr_AddFloat.rva, &add_float);
		stub(old::Scr_AddString.rva, &add_string);
		stub(old::Scr_AddVector.rva, &add_vector);
		stub(old::Scr_AddEntityNum.rva, &add_entity);
		stub(old::Scr_ClearOutParams.rva, &clear);
		stub(old::RemoveRefToValue.rva, &remove_ref);
		stub(old::SL_GetString.rva, &intern);
		stub(old::SL_ConvertToString.rva, &convert);
		stub(old::Scr_GetNumParam.rva, &argc);
		stub(old::EntityRef_layout.rva, &entity_ref);
		stub(mapped::Scr_AllocArray.rva, &allocate_array);
		stub(mapped::AllocObject.rva, &allocate_struct);
		stub(mapped::GetNewArrayVariable.rva, &array_variable);
		stub(mapped::GetVariable.rva, &variable);
		stub(mapped::SetNewVariableValue.rva, &set_new);
		stub(mapped::Scr_MakeArray.rva, &make_array);
		stub(mapped::Scr_MakeStruct.rva, &make_struct);
		stub(mapped::Scr_AddArray.rva, &add_array);
		stub(mapped::Scr_AddStructField.rva, &add_field);
		stub(mapped::GetObjectType.rva, &object_type);
		stub(mapped::Scr_GetArraySize.rva, &array_size);
		stub(mapped::FindFirstSibling.rva, &first);
		stub(mapped::FindNextSibling.rva, &next);
		stub(mapped::GetVariableName.rva, &name);
		stub(mapped::Scr_EvalVariable.rva, &evaluate);
		stub(mapped::AddRefToObject.rva, &add_object_ref);
		stub(mapped::RemoveRefToObject.rva, &release_object);
		stub(mapped::AllocThread.rva, &alloc_thread);
		stub(mapped::VM_Execute.rva, &execute);
		stub(core::replay::Scr_ExecThread.rva, &execute_script);
		stub(core::replay::Scr_ExecThreadInternal.rva, &execute_script_on);
		stub(core::replay::Scr_LoadScript.rva, &load_script);
		stub(core::replay::Scr_GetFunctionHandle.rva, &get_script);
		stub(core::replay::weapon::AddParam.rva, &add_weapon);
		stub(core::replay::weapon::GetWeapon.rva, &get_weapon);
		stub(core::replay::weapon::IsAlternate.rva, &weapon_alternate);
		stub(mapped::Dvar_FindVarByName.rva, &find_dvar);
		stub(mapped::Dvar_GetBoolSafe.rva, &dvar_bool);
		stub(mapped::Dvar_GetIntSafe.rva, &dvar_int);
		stub(mapped::Dvar_GetFloatSafe.rva, &dvar_float);
		stub(mapped::Dvar_GetVariantString.rva, &dvar_string);
		commit(native::clients, native::client_capacity * sizeof(void*));
		commit(native::client_count, sizeof(uint32_t));
		commit(native::persistent_globals, sizeof(void*));
		core::publish_tables(ctx);
		expect(field<uint32_t>(ctx, native::layout::function_count) == native::stock_function_count + native::plugin_capacity + 1,
			"Replay native table reserves 256 plugin functions and VM trampoline");
		expect(field<uint32_t>(ctx, native::layout::method_count) == native::stock_method_count + native::plugin_capacity,
			"Replay native table reserves 256 plugin methods");
		expect(
			field<uintptr_t*>(ctx, native::layout::functions)[native::stock_function_count] != 0, "first plugin function thunk published");
		const auto preserved = field<uintptr_t*>(ctx, native::layout::functions);
		expect(preserved[core::invocation_builtin_id - 1] == reinterpret_cast<uintptr_t>(&core::invocation_trampoline),
			"Replay invocation builtin ID matches extended native table");
		expect(preserved[core::find_builtin(false, "getdvar")->id - 1] == image + core::find_builtin(false, "getdvar")->rva,
			"stock callbacks survive native table extension");
		core::publish_tables(ctx);
		expect(field<uintptr_t*>(ctx, native::layout::functions) == preserved, "repeat table publication keeps process-lifetime owner");
		field<uint32_t>(ctx, native::layout::frame_count) = 1;
		bool active_refused{};
		try
		{
			core::publish_tables(ctx);
		}
		catch (const std::exception&)
		{
			active_refused = true;
		}
		expect(active_refused, "table publication refuses active Replay frame count at B9B8");
		field<uint32_t>(ctx, native::layout::frame_count) = 0;
		field<uint32_t>(ctx, native::layout::function_count) = native::stock_function_count;
		bool owner_refused{};
		try
		{
			core::publish_tables(ctx);
		}
		catch (const std::exception&)
		{
			owner_refused = true;
		}
		expect(owner_refused, "table publication rejects inconsistent owner metadata");
		field<uint32_t>(ctx, native::layout::function_count) = native::stock_function_count + native::plugin_capacity + 1;
		core::engine_bindings bindings{};
		bindings.capabilities = IW8X_CAP_GSC_CALL | IW8X_CAP_RICH_VALUES | IW8X_CAP_TYPED_DVARS | IW8X_CAP_ENTITIES | IW8X_CAP_SCRIPT_CALL;
		bindings.call_builtin = &core::call_builtin_native;
		bindings.get_dvar = &core::read_typed_dvar;
		bindings.call_script = &core::call_script_native;
		bindings.resolve_script = &core::resolve_script_native;
		bindings.call_script_on = &core::call_script_on_native;
		bindings.valid = &core::validate_entity;
		core::configure_bindings(bindings);
		owner = core::create_owner("invocation tests");
	}

	template <class F>
	void status(uint32_t expected, F action)
	{
		uint32_t actual = IW8X_OK;
		try
		{
			action();
		}
		catch (const iw8x::api_error& e)
		{
			actual = e.code();
		}
		if (actual != expected)
			throw std::runtime_error("SDK status expected=" + std::to_string(expected) + " actual=" + std::to_string(actual));
		++checks;
	}

	void clean()
	{
		expect(strings.empty() && vectors.empty() && weapons.empty(), "all temporary strings, vectors and Weapons released");
		expect(objects.size() == 1 && objects.at(1).refs == 1, "all temporary arrays, structs, entities and threads released");
		expect(top(context()) == reinterpret_cast<raw*>(static_cast<uint8_t*>(context()) + 0xC3D0) && !inputs(context()) &&
				!outputs(context()) && !field<uint32_t>(context(), 0x3518),
			"VM stack/counts/execution state restored");
	}
#include "invocation/values.inc"
#include "invocation/scripts.inc"
#include "invocation/module_resolution.inc"
#include "invocation/dvars.inc"
}

namespace game
{
	uintptr_t base() noexcept
	{
		return image;
	}

	uint32_t image_size() noexcept
	{
		return 0x18000000;
	}

	bool initialized() noexcept
	{
		return true;
	}

	bool validate_bytes(const void*, const void*, size_t) noexcept
	{
		return true;
	}
}

namespace
{
#include "invocation/tables_players.inc"
	std::vector<std::string> chat_commands;

	void port_table_reload()
	{
		namespace native = game::addresses::gsc_api;
		namespace ports = core::port_tables;
		using tables = night_builtins::Tables;
		static tables provider;
		expect(provider.Add(0x41A, reinterpret_cast<uintptr_t>(&global)) &&
			provider.Add(0x879F, reinterpret_cast<uintptr_t>(&method)), "reload port providers configured");
		provider.Seal();
		auto* ctx = context();
		auto* stock_functions = game::at<uintptr_t>(native::native_functions);
		auto* stock_methods = game::at<uintptr_t>(native::native_methods);
		field<void*>(ctx, native::layout::functions) = stock_functions;
		field<void*>(ctx, native::layout::methods) = stock_methods;
		field<uint32_t>(ctx, native::layout::function_count) = tables::NativeCount;
		field<uint32_t>(ctx, native::layout::method_count) = tables::NativeMethodCount;
		field<uint32_t>(ctx, native::layout::function_end) = tables::NativeEnd;
		field<uint32_t>(ctx, native::layout::method_end) = tables::NativeMethodEnd;
		expect(provider.Adopt(ctx, stock_functions, stock_methods, true), "first native port table initialization");
		ports::inspect = [](const void* ctx, ports::view& view)
		{
			if (!provider.Owns(ctx)) return false;
			view = { tables::Read<uintptr_t*>(ctx, 0xC90), tables::Read<uintptr_t*>(ctx, 0xC98),
				tables::Read<uint32_t>(ctx, 0xC7C), tables::Read<uint32_t>(ctx, 0xC88) };
			return true;
		};
		ports::reserved = [](bool method, uint32_t slot)
		{
			return provider.Reserved(method, slot);
		};
		const night_builtins::Registration record{ 1, 0, reinterpret_cast<uintptr_t>(&global), 0 };
		const night_builtins::Descriptor descriptor{ &record, 1 };
		core::publish_tables(ctx);
		auto* merged_functions = field<uintptr_t*>(ctx, native::layout::functions);
		auto* merged_methods = field<uintptr_t*>(ctx, native::layout::methods);
		expect(!provider.Adopt(ctx, stock_functions, stock_methods), "old merged owner reproduces registration rejection");
		for (unsigned reload = 0; reload < 4; ++reload)
		{
			core::restore_port_tables(ctx);
			expect(provider.Owns(ctx), "native registration receives its port table owner");
			// Replay BeginLoadScripts clears both context-sized tables before registration.
			auto* functions = field<uintptr_t*>(ctx, native::layout::functions);
			auto* methods = field<uintptr_t*>(ctx, native::layout::methods);
			std::fill_n(functions, field<uint32_t>(ctx, native::layout::function_count), 0);
			std::fill_n(methods, field<uint32_t>(ctx, native::layout::method_count), 0);
			expect(provider.ValidDescriptor(&descriptor) && provider.Adopt(ctx, stock_functions, stock_methods),
				"registration succeeds after native table clear");
			expect(functions[0] == 0 && functions[0x41A - 1] == reinterpret_cast<uintptr_t>(&global) &&
				methods[0x879F - 0x8000] == reinterpret_cast<uintptr_t>(&method), "reset restores only selected port providers");
			functions[0] = record.callback;
			provider.Mirror(stock_functions, stock_methods);
			core::publish_tables(ctx);
			expect(field<uintptr_t*>(ctx, native::layout::functions) == merged_functions &&
				field<uintptr_t*>(ctx, native::layout::methods) == merged_methods, "merged dispatch owner restored after registration");
			expect(merged_functions[0] == record.callback && merged_functions[0x41A - 1] == record.callback &&
				merged_methods[0x879F - 0x8000] == reinterpret_cast<uintptr_t>(&method) &&
				merged_functions[native::stock_function_count + 2] != 0, "stock, port and plugin callbacks survive reload");
		}
		field<uint32_t>(ctx, native::layout::frame_count) = 1;
		bool rejected{};
		try
		{
			core::restore_port_tables(ctx);
		}
		catch (const std::exception&)
		{
			rejected = true;
		}
		expect(rejected && field<uintptr_t*>(ctx, native::layout::functions) == merged_functions,
			"active VM table handoff rejected without mutation");
		field<uint32_t>(ctx, native::layout::frame_count) = 0;
		field<uint32_t>(ctx, native::layout::function_count) -= 1;
		rejected = false;
		try
		{
			core::restore_port_tables(ctx);
		}
		catch (const std::exception&)
		{
			rejected = true;
		}
		expect(rejected && field<uintptr_t*>(ctx, native::layout::functions) == merged_functions,
			"invalid merged metadata rejected without mutation");
		field<uint32_t>(ctx, native::layout::function_count) += 1;
		ports::inspect = nullptr;
		ports::reserved = nullptr;
		core::restore_port_tables(ctx);
		expect(field<uintptr_t*>(ctx, native::layout::functions) == merged_functions, "ordinary unported table remains unchanged");
	}

	void merged_port_tables()
	{
		namespace native = game::addresses::gsc_api;
		namespace ports = core::port_tables;
		static std::array<uintptr_t, native::stock_function_count + 2> functions{};
		static std::array<uintptr_t, native::stock_method_count + 2> methods{};
		std::copy_n(game::at<uintptr_t>(native::native_functions), native::stock_function_count, functions.data());
		std::copy_n(game::at<uintptr_t>(native::native_methods), native::stock_method_count, methods.data());
		functions.back() = reinterpret_cast<uintptr_t>(&global);
		methods.back() = reinterpret_cast<uintptr_t>(&method);
		auto* ctx = context();
		field<void*>(ctx, native::layout::functions) = functions.data();
		field<void*>(ctx, native::layout::methods) = methods.data();
		field<uint32_t>(ctx, native::layout::function_count) = uint32_t(functions.size());
		field<uint32_t>(ctx, native::layout::function_end) = native::stock_function_base + uint32_t(functions.size());
		field<uint32_t>(ctx, native::layout::method_count) = uint32_t(methods.size());
		field<uint32_t>(ctx, native::layout::method_end) = native::stock_method_base + uint32_t(methods.size());
		ports::inspect = [](const void* context, ports::view& view)
		{
			if (field<void*>(const_cast<void*>(context), native::layout::functions) != functions.data()) return false;
			view = { functions.data(), methods.data(), functions.size(), methods.size() };
			return true;
		};
		ports::reserved = [](bool, uint32_t slot)
		{
			return slot == 1;
		};
		expect(core::reserve_native(false, "test", "after_port_function") == IW8X_OK, "plugin allocation skips port IDs");
		expect(core::native_name(false, 1).empty() && core::native_name(false, 2) == "after_port_function",
			"persistent port ID is never assigned to plugin");
		core::publish_tables(ctx);
		auto* published_functions = field<uintptr_t*>(ctx, native::layout::functions);
		auto* published_methods = field<uintptr_t*>(ctx, native::layout::methods);
		expect(published_functions[native::stock_function_count + 1] == functions.back(), "port function survives merged publication");
		expect(published_methods[native::stock_method_count + 1] == methods.back(), "port method survives merged publication");
		expect(published_functions[native::stock_function_count] != 0 && published_functions[native::stock_function_count + 2] != 0,
			"existing and newly allocated plugin thunks coexist with port builtin");
		core::publish_tables(ctx);
		expect(published_functions[native::stock_function_count + 1] == functions.back(), "repeat publication preserves port callback");
		ports::inspect = nullptr;
		ports::reserved = nullptr;
	}
#include "invocation/chat_entities.inc"
}
#include "runtime_callbacks.inc"
int main()
{
	const auto root = std::filesystem::temp_directory_path() / ("iw8x-replay-tables-" + std::to_string(GetCurrentProcessId()));
	try
	{
		persistent_tables(root);
		setup();
		entity_lifetime();
		player_adapter_layout();
		calls_and_values();
		script_exports_and_values();
		compiled_module_resolution();
		dvars();
		production_chat();
		runtime_callbacks();
			expect(dedicated_restart_scope_valid.load(std::memory_order_relaxed),
				"native dedicated map_restart boundary never runs outside a GSC VM scope");
		merged_port_tables();
		port_table_reload();
		std::filesystem::remove_all(root);
		std::cout << checks << " invocation/script/value/dvar/table/entity checks passed\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::filesystem::remove_all(root);
		std::cerr << "FAILED: " << e.what() << " after " << checks << " checks\n";
		return 1;
	}
}
