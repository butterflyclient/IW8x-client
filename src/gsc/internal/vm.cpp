#include "replay.hpp"
#include "runtime.hpp"
#include "state.hpp"
#include "stock_builtins.hpp"

#include "engine/image.hpp"

#include <cstring>
#include <algorithm>
#include <array>
#include <exception>
#include <cstdio>
#include <cstdlib>
namespace iw8x::gsc::internal
{
	namespace
	{
		template <class Function>
		Function at(replay::site site)
		{
			return reinterpret_cast<Function>(game::at(site.rva));
		}

		template <class T>
		T read(void* context, size_t offset)
		{
			T value{};
			// This is the fixed Replay 1.20.4 server ScriptContext (or an engine-owned pointer
			// read from it) while we hold the game/VM boundary.  The old hot path used
			// safemem::ReadBytes here, which performs VirtualQuery for every scalar
			// access.  A single plugin GSC call reads this context a few dozen times,
			// making memory-protection queries substantially more expensive than the
			// native builtin/script itself on protected retail builds.
			std::memcpy(&value, static_cast<const uint8_t*>(context) + offset, sizeof(value));
			return value;
		}

		template <class T>
		void put(void* context, size_t offset, T value)
		{
			std::memcpy(static_cast<uint8_t*>(context) + offset, &value, sizeof(value));
		}

		struct vm_failure
		{
			iw8x_status status;
		};

		struct native_value
		{
			uint64_t payload{};
			uint8_t type{};
			uint8_t padding[7]{};
		};

		static_assert(sizeof(native_value) == 16);

		struct value_budget
		{
			size_t nodes{}, bytes{}, depth{};
		};

		void preflight(const value& v, value_budget& budget, size_t depth = 0)
		{
			if (++budget.nodes > 65536 || depth > 32) throw vm_failure{ IW8X_LIMIT };
			budget.depth = (std::max)(budget.depth, depth);
			switch (v.type())
			{
			case value_type::undefined:
			case value_type::integer:
			case value_type::boolean:
			case value_type::number:
			case value_type::vector:
				return;
			case value_type::weapon_: {
				const auto& weapon = v.get<iw8x::gsc::weapon>();
				auto& s = state();
				std::lock_guard lock(s.mutex);
				if (weapon.map_epoch != s.epoch || weapon.script_generation != s.script_generation) throw vm_failure{ IW8X_STALE };
				const auto* bytes = reinterpret_cast<const uint8_t*>(weapon.words);
				if (bytes[replay::weapon::alternate] > 1 || bytes[replay::weapon::reserved] ||
					std::any_of(bytes + game::addresses::gsc_api::weapon_descriptor_size, bytes + replay::weapon::alternate,
						[](uint8_t byte)
						{
							return byte != 0;
						}))
					throw vm_failure{ IW8X_INVALID_ARGUMENT };
				return;
			}
			case value_type::string:
				if (v.get<std::string>().size() > 65535 || v.get<std::string>().find('\0') != std::string::npos)
					throw std::invalid_argument("GSC strings must be NUL-free and at most 65535 bytes");
				budget.bytes += v.get<std::string>().size();
				if (budget.bytes > 16 * 1024 * 1024) throw vm_failure{ IW8X_LIMIT };
				return;
			case value_type::entity:
				if (validate_entity(v.get<entity_ref>()) != IW8X_OK) throw vm_failure{ IW8X_STALE };
				return;
			case value_type::array:
				for (const auto& child : v.get<value::array>())
					preflight(child, budget, depth + 1);
				return;
			case value_type::struct_:
				for (const auto& [name, child] : v.get<value::object>())
				{
					if (!iw8x::canonical_field_id(name)) throw vm_failure{ IW8X_UNSUPPORTED };
					budget.bytes += name.size();
					if (budget.bytes > 16 * 1024 * 1024) throw vm_failure{ IW8X_LIMIT };
					preflight(child, budget, depth + 1);
				}
				return;
			}
		}

		void preflight(const value& v)
		{
			value_budget budget;
			preflight(v, budget);
		}

		struct builtin_job
		{
			void* context{};
			const stock_builtin* target{};
			entity_ref self{};
			std::span<const value> args;
			std::array<uint32_t, 256> held_objects{};
			uint32_t held_object_count{};
			bool complete{};
			iw8x_status status{ IW8X_OK };
			bool marshalling{};
			uint64_t native_scriptable_self{};
		};

		thread_local builtin_job* current_job{};
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
			thread_local native_scriptable_spawn* native_island_scriptable_result{};
			thread_local const native_scriptable_spawn* native_island_scriptable_method_self{};
#endif

		void stack_room(void* context, size_t count)
		{
			const auto top = read<uintptr_t>(context, replay::layout::stack_top);
			const auto maximum = read<uintptr_t>(context, replay::layout::stack_max);
			const auto outgoing = read<uint32_t>(context, replay::layout::outparam_count);
			const auto hard_stop = reinterpret_cast<uintptr_t>(context) + replay::layout::stack_sentinel;
			if (outgoing > 256 || count > 300) throw std::runtime_error("invalid VM stack metadata");

			const auto outgoing_bytes = static_cast<uintptr_t>(outgoing) * 16;
			if (top < outgoing_bytes) throw std::runtime_error("invalid VM stack metadata");

			// Replay Scr_IncInParam first clears nonzero outparam_count (+B9D8),
			// subtracting outgoing*16 from stack_top (+B9C8). It refuses a push at
			// stack_max (+B9B0) or context+stack_sentinel (+B998), then advances
			// stack_top by one 16-byte native value.
			const auto cleared = top - outgoing_bytes;
			if (cleared > maximum) throw std::runtime_error("invalid VM stack metadata");
			if (!count) return;

			const auto distance_to_max = maximum - cleared;
			if (count > distance_to_max / 16) throw std::runtime_error("VM stack has insufficient space");

			const auto last_push = cleared + static_cast<uintptr_t>(count - 1) * 16;
			if (hard_stop >= cleared && hard_stop <= last_push && ((hard_stop - cleared) & 0xF) == 0)
				throw std::runtime_error("VM stack has insufficient space");
		}

		void push(void* ctx, const value& v)
		{
			using namespace replay;
			switch (v.type())
			{
			case value_type::undefined:
				at<void (*)(void*)>(Scr_AddUndefined)(ctx);
				break;
			case value_type::integer:
				at<void (*)(void*, int32_t)>(Scr_AddInt)(ctx, v.get<int32_t>());
				break;
			case value_type::boolean:
				at<void (*)(void*, int32_t)>(Scr_AddInt)(ctx, v.get<bool>() ? 1 : 0);
				break;
			case value_type::number:
				at<void (*)(void*, float)>(Scr_AddFloat)(ctx, v.get<float>());
				break;
			case value_type::string:
				at<void (*)(void*, const char*)>(Scr_AddString)(ctx, v.get<std::string>().c_str());
				break;
			case value_type::vector:
				at<void (*)(void*, const float*)>(Scr_AddVector)(ctx, &v.get<vector>().x);
				break;
			case value_type::weapon_: {
				const auto& weapon = v.get<iw8x::gsc::weapon>();
				at<void (*)(void*, const void*, bool)>(replay::weapon::AddParam)(
					ctx, weapon.words, reinterpret_cast<const uint8_t*>(weapon.words)[replay::weapon::alternate] != 0);
				break;
			}
			case value_type::entity:
				at<void (*)(void*, int32_t, uint8_t)>(Scr_AddEntityNum)(ctx, v.get<entity_ref>().index, 0);
				break;
			case value_type::array: {
				// Use the engine's complete array construction path.  Retail
				// Scr_MakeArray performs IncInParam + VAR_POINTER publication +
				// Scr_AllocArray, and Scr_AddArray performs the native pop,
				// GetArraySize/GetNewArrayVariable and SetNewVariableValue sequence.
				// The Replay bodies confirm this sequence; keep construction inside
				// the engine instead of reconstructing its object/child bookkeeping.
				at<void (*)(void*)>(replay::Scr_MakeArray)(ctx);
				for (const auto& child : v.get<value::array>())
				{
					push(ctx, child);
					at<void (*)(void*)>(replay::Scr_AddArray)(ctx);
				}
				if (!v.get<value::array>().empty())
				{
					const auto id = static_cast<uint32_t>(read<native_value*>(ctx, replay::layout::stack_top)->payload);
					if (at<uint32_t (*)(void*, uint32_t)>(replay::Scr_GetArraySize)(ctx, id) != v.get<value::array>().size())
						throw vm_failure{ IW8X_INTERNAL_ERROR };
				}
				break;
			}
			case value_type::struct_: {
				at<void (*)(void*)>(replay::Scr_MakeStruct)(ctx);
				for (const auto& [name, child] : v.get<value::object>())
				{
					push(ctx, child);
					at<void (*)(void*, uint32_t)>(replay::Scr_AddStructField)(ctx, iw8x::canonical_field_id(name));
				}
				break;
			}
			}
		}

		struct snapshot_reader
		{
			void* ctx;
			value_budget budget;
			std::array<uint32_t, 33> path{};

			std::string string(uint32_t token)
			{
				const auto* text = at<const char* (*)(uint32_t)>(replay::SL_ConvertToString)(token);
				if (!text) throw vm_failure{ IW8X_INTERNAL_ERROR };
				const auto length = strnlen_s(text, 65536);
				budget.bytes += length;
				if (length == 65536 || budget.bytes > 16 * 1024 * 1024) throw vm_failure{ IW8X_LIMIT };
				return { text, length };
			}

			value copy(const native_value& input, size_t depth = 0)
			{
				if (depth > 32 || ++budget.nodes > 65536) throw vm_failure{ IW8X_LIMIT };
				switch (input.type)
				{
				case 0:
					return {};
				case 6:
					return value(static_cast<int32_t>(input.payload));
				case 5: {
					float out;
					std::memcpy(&out, &input.payload, 4);
					return value(out);
				}
				case 2:
					return value(string(static_cast<uint32_t>(input.payload)));
				case 4:
					return value(read<vector>(reinterpret_cast<void*>(input.payload), 0));
				case 1:
					break;
				default:
					throw vm_failure{ IW8X_UNSUPPORTED };
				}
				const auto id = static_cast<uint32_t>(input.payload);
				const auto type = at<uint8_t (*)(void*, uint32_t)>(replay::GetObjectType)(ctx, id);
				if (type == 22)
				{
					const auto ref = at<uint64_t (*)(void*, uint32_t)>(replay::EntityRef_layout)(ctx, id);
					const auto entity_class = static_cast<uint8_t>(ref >> 32);
					if (entity_class == replay::weapon::entity_class)
					{
						const auto weapon_id = static_cast<uint32_t>(ref);
						if (weapon_id > 1000) throw vm_failure{ IW8X_INTERNAL_ERROR };
						iw8x::gsc::weapon weapon{};
						const auto* native = at<const void* (*)(void*, uint32_t)>(replay::weapon::GetWeapon)(ctx, weapon_id);
						// GetWeapon returned engine-owned storage from the live VM.  It
						// is valid for this synchronous snapshot and does not cross the
						// plugin ABI as a pointer.
						std::memcpy(weapon.words, native, replay::weapon::descriptor_size);
						reinterpret_cast<uint8_t*>(weapon.words)[replay::weapon::alternate] =
							at<bool (*)(void*, uint32_t)>(replay::weapon::IsAlternate)(ctx, weapon_id);
						auto& s = state();
						std::lock_guard lock(s.mutex);
						weapon.map_epoch = s.epoch;
						weapon.script_generation = s.script_generation;
						return value(weapon);
					}
					if (entity_class != 0) throw vm_failure{ IW8X_UNSUPPORTED };
					const auto entity = capture_entity(ref);
					if (!entity.generation) throw vm_failure{ IW8X_STALE };
					return value(entity);
				}
				// Retail codes differ from the semantic-reference build (20/23 here).
				if (type != 20 && type != 23) throw vm_failure{ IW8X_UNSUPPORTED };
				if (std::find(path.begin(), path.begin() + depth, id) != path.begin() + depth)
					throw vm_failure{ IW8X_UNSUPPORTED }; // Cycles cannot be represented by the value ABI.
				path[depth] = id;
				value::array array;
				value::object object;
				std::vector<bool> seen;
				if (type == 23)
				{
					const auto count = at<uint32_t (*)(void*, uint32_t)>(replay::Scr_GetArraySize)(ctx, id);
					if (count > 65536 - budget.nodes) throw vm_failure{ IW8X_LIMIT };
					array.resize(count);
					seen.resize(count);
				}
				size_t children = 0;
				auto child = at<uint32_t (*)(void*, uint32_t)>(replay::FindFirstSibling)(ctx, id);
				while (child)
				{
					if (++children > 65536) throw vm_failure{ IW8X_LIMIT };
					const auto key = at<uint32_t (*)(void*, uint32_t)>(replay::GetVariableName)(ctx, child);
					native_value raw{};
					// Replay uses an explicit third output argument, not a hidden aggregate return.
					at<uint64_t (*)(void*, uint32_t, native_value*)>(replay::Scr_EvalVariable)(ctx, child, &raw);

					struct release
					{
						void* ctx;
						native_value& v;

						~release()
						{
							at<void (*)(void*, uint32_t, uint64_t)>(replay::RemoveRefToValue)(ctx, v.type, v.payload);
						}
					} ref{ ctx, raw };

					auto owned = copy(raw, depth + 1);
					if (type == 23)
					{
						// GetNewArrayVariable: (index - 800000h) & FFFFFFh.
						// Never flatten sparse/string/object-keyed arrays silently.
						constexpr auto key_base = game::addresses::gsc_api::array_integer_key_base;
						if (key < key_base || key - key_base >= array.size() || seen[key - key_base]) throw vm_failure{ IW8X_UNSUPPORTED };
						seen[key - key_base] = true;
						array[key - key_base] = std::move(owned);
					}
					else if (!object.emplace(iw8x::canonical_field(key), std::move(owned)).second)
						throw vm_failure{ IW8X_INTERNAL_ERROR };
					child = at<uint32_t (*)(void*, uint32_t)>(replay::FindNextSibling)(ctx, child);
				}
				if (type == 23)
				{
					if (children != array.size()) throw vm_failure{ IW8X_UNSUPPORTED };
					return value(std::move(array));
				}
				return value(std::move(object));
			}
		};
	}

	void* server_context()
	{
		return game::at(game::addresses::gsc::kServerContextRva);
	}

	bool vm_idle(void* ctx)
	{
		return !read<uint32_t>(ctx, replay::layout::frame_count) && !read<uint32_t>(ctx, replay::layout::inparam_count) &&
			!read<uint32_t>(ctx, replay::layout::outparam_count);
	}

	bool invocation_available()
	{
		// Replay 1.20.4 is the only supported target. The invocation bridge uses the
		// fixed retail RVAs in replay; capability publication must not be
		// disabled by brittle prologue/signature checks.
		return true;
	}

	bool script_invocation_available()
	{
		// Replay 1.20.4 is the only supported target. Script invocation uses the fixed,
		// verified retail RVAs in replay; do not make the public capability
		// depend on fragile function-prologue byte signatures. A harmless compiler/
		// dump byte difference here previously hid IW8X_CAP_SCRIPT_CALL entirely and
		// made plugins fail at load even though the script bridge was installed.
		return true;
	}

	void invocation_trampoline(void* ctx)
	{
		auto* job = current_job;
		if (!job || job->context != ctx)
		{
			at<void (*)(void*)>(replay::Scr_AddUndefined)(ctx);
			return;
		}
		// A script error jumps to the engine's interpreter, not through a plugin
		// callback. Keep all host owners in call_builtin_native, above that frame.
		const auto floor = read<native_value*>(ctx, replay::layout::stack_top);
		try
		{
			job->marshalling = true;
			for (auto it = job->args.rbegin(); it != job->args.rend(); ++it)
				push(ctx, *it);
			job->marshalling = false;

			// Script callers normally retain object-valued arguments outside the
			// callee's outparam stack.  A C++ call has no such caller-owned VM
			// reference: collections are created as temporaries directly on the
			// argument stack.  Builtins such as getarraykeys save the source object
			// ID, create their return value (which clears the input params), and
			// only then walk that saved object.  Without this extra hold the clear
			// releases the temporary collection and destroys its child list before
			// the builtin can use it.
			auto* argument_top = read<native_value*>(ctx, replay::layout::stack_top);
			for (size_t i = 0; i < job->args.size(); ++i)
			{
				const auto& native = argument_top[-static_cast<ptrdiff_t>(i)];
				if (native.type != 1) continue;
				const auto id = static_cast<uint32_t>(native.payload);
				at<void (*)(void*, uint32_t)>(replay::AddRefToObject)(ctx, id);
				job->held_objects[job->held_object_count++] = id;
			}
			put<uint32_t>(ctx, replay::layout::outparam_count, static_cast<uint32_t>(job->args.size()));
			put<uint32_t>(ctx, replay::layout::inparam_count, 0);
			if (job->target->method)
					reinterpret_cast<void (*)(void*, uint64_t)>(game::at(job->target->rva))(ctx,
						job->native_scriptable_self ? job->native_scriptable_self : job->self.index);
			else
				reinterpret_cast<void (*)(void*)>(game::at(job->target->rva))(ctx);
			job->complete = true;
		}
		catch (const vm_failure& e)
		{
			job->status = e.status;
		}
		catch (...)
		{
			job->status = IW8X_INTERNAL_ERROR;
		}
		if (job->status != IW8X_OK)
		{
			auto* top = read<native_value*>(ctx, replay::layout::stack_top);
			if (top < floor) std::terminate();
			while (top > floor)
			{
				at<void (*)(void*, uint32_t, uint64_t)>(replay::RemoveRefToValue)(ctx, top->type, top->payload);
				*top-- = {};
			}
			put(ctx, replay::layout::stack_top, floor);
			put<uint32_t>(ctx, replay::layout::inparam_count, 0);
			put<uint32_t>(ctx, replay::layout::outparam_count, 0);
			at<void (*)(void*)>(replay::Scr_AddUndefined)(ctx);
		}
		job->marshalling = false;
	}

	iw8x_status call_builtin_native(bool method, entity_ref self, std::string_view name, std::span<const value> args, value& output)
	{
		if (!in_game_scope()) return IW8X_WRONG_THREAD;
		if (current_job) return IW8X_BUSY;
		auto* ctx = server_context();
		if (!vm_idle(ctx) || read<uint8_t>(ctx, replay::layout::error_flag) || read<uint8_t>(ctx, replay::layout::loading))
			return IW8X_BUSY;
			if (method
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
				&& !native_island_scriptable_method_self
#endif
			)
		{
			const auto status = validate_entity(self);
			if (status != IW8X_OK) return status;
		}
		const auto* target = find_builtin(method, name);
		if (!target) return IW8X_NOT_FOUND;
		if (args.size() > 256) return IW8X_LIMIT;
		const auto table = read<uintptr_t>(ctx, method ? replay::layout::methods : replay::layout::functions);
		const auto base = read<uint32_t>(ctx, method ? replay::layout::method_base : replay::layout::function_base);
		const auto count = read<uint32_t>(ctx, method ? replay::layout::method_count : replay::layout::function_count);
		if (!table || target->id < base || target->id - base >= count) return IW8X_BUSY;
		if (read<uintptr_t>(reinterpret_cast<void*>(table), (target->id - base) * 8) != reinterpret_cast<uintptr_t>(game::at(target->rva)))
			return IW8X_UNSUPPORTED;
		const auto function_table = read<uintptr_t>(ctx, replay::layout::functions);
		if (read<uint32_t>(ctx, replay::layout::function_base) != 1 ||
			read<uint32_t>(ctx, replay::layout::function_count) < invocation_builtin_id || !function_table ||
			read<uintptr_t>(reinterpret_cast<void*>(function_table), (invocation_builtin_id - 1) * 8) !=
				reinterpret_cast<uintptr_t>(&invocation_trampoline))
			return IW8X_BUSY;
		const auto level = read<uint32_t>(ctx, replay::layout::level_object);
		if (!level) return IW8X_BUSY;
		try
		{
			value_budget budget;
			for (const auto& arg : args)
				preflight(arg, budget);
			stack_room(ctx, args.size() + budget.depth + 2);
			const auto objects = read<void*>(ctx, replay::layout::parents);
			if (!objects || !read<uint32_t>(objects, game::addresses::gsc_api::parent_free_head_offset)) return IW8X_LIMIT;
			builtin_job job{ ctx, target, self, args };
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
				if (method && native_island_scriptable_method_self)
				{
					const auto& scriptable=*native_island_scriptable_method_self;
					job.native_scriptable_self=(uint64_t(scriptable.entity_class)<<32)|scriptable.instance_index;
				}
#endif
			const auto* saved_top = read<native_value*>(ctx, replay::layout::stack_top);
			const auto saved_error = read<uintptr_t>(ctx, replay::layout::error_message);
			const auto saved_error_index = read<uint32_t>(ctx, replay::layout::error_index);
			const auto saved_execute = read<uint32_t>(ctx, replay::layout::execute_depth);

			struct cleanup
			{
				void* ctx;
				const native_value* top;
				builtin_job& job;
				uintptr_t error;
				uint32_t error_index, execute, thread{};

				~cleanup()
				{
					auto* current = read<native_value*>(ctx, replay::layout::stack_top);
					if (current < top || reinterpret_cast<uintptr_t>(current) > read<uintptr_t>(ctx, replay::layout::stack_max))
						std::terminate();
					while (current > top)
					{
						at<void (*)(void*, uint32_t, uint64_t)>(replay::RemoveRefToValue)(ctx, current->type, current->payload);
						*current-- = {};
					}
					for (uint32_t i = 0; i < job.held_object_count; ++i)
						at<void (*)(void*, uint32_t)>(replay::RemoveRefToObject)(ctx, job.held_objects[i]);
					job.held_object_count = 0;
					if (thread) at<void (*)(void*, uint32_t)>(replay::RemoveRefToObject)(ctx, thread);
					put(ctx, replay::layout::stack_top, top);
					put<uint32_t>(ctx, replay::layout::inparam_count, 0);
					put<uint32_t>(ctx, replay::layout::outparam_count, 0);
					put(ctx, replay::layout::error_message, error);
					put(ctx, replay::layout::error_index, error_index);
					put(ctx, replay::layout::execute_depth, execute);
					current_job = nullptr;
				}
			} guard{ ctx, saved_top, job, saved_error, saved_error_index, saved_execute };

			// Same owner/thread reference transfer as Scr_ExecThreadInternal.
			at<void (*)(void*, uint32_t)>(replay::AddRefToObject)(ctx, level);
			guard.thread = at<uint32_t (*)(void*, uint32_t)>(replay::AllocThread)(ctx, level);
			current_job = &job;
			put(ctx, replay::layout::execute_depth, saved_execute + 1);
			// checkclearparams; CallBuiltin0 <host trampoline>; Return.
			// These bytes have host/process lifetime, including error diagnostics.
			static constexpr uint8_t program[]{ game::addresses::gsc_api::op_checkclearparams, game::addresses::gsc_api::op_callbuiltin0,
				static_cast<uint8_t>(invocation_builtin_id), static_cast<uint8_t>(invocation_builtin_id >> 8),
				game::addresses::gsc_api::op_return };
			{
				slow_scope timer("vm.builtin", name);
				guard.thread =
					at<uint32_t (*)(void*, uint32_t, const uint8_t*, uint32_t)>(replay::VM_Execute)(ctx, guard.thread, program, 0);
			}
			if (job.status != IW8X_OK) return job.status;
			if (!job.complete) return IW8X_CALLBACK_ERROR;
			if (read<native_value*>(ctx, replay::layout::stack_top) != saved_top + 1 ||
				read<uint32_t>(ctx, replay::layout::inparam_count) != 1 || read<uint32_t>(ctx, replay::layout::outparam_count) ||
				read<uint32_t>(ctx, replay::layout::frame_count))
				return IW8X_INTERNAL_ERROR;
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
			// Diagnostic only. Some native ScriptableSv objects have an engine
			// entity class outside the plugin ABI's class-0 entity handle. A
			// successful spawn may therefore return IW8X_UNSUPPORTED while
			// decoding a real native ScriptableSv reference; do not retry the
			// same spawn or falsely claim it did not execute. Observe the exact
			// live return class before changing ownership/ABI semantics.
			if (name == "spawnscriptable")
			{
				const char* enabled = std::getenv("IW8X_ISLAND_TEST_REAL_CRATE");
				if (enabled && enabled[0] == '1' && enabled[1] == '\0')
				{
					const auto raw = *(saved_top + 1);
					uint32_t object_type = UINT32_MAX;
					uint64_t engine_ref{};
					if (raw.type == 1 && raw.payload)
					{
						object_type = at<uint8_t (*)(void*, uint32_t)>(replay::GetObjectType)(ctx, uint32_t(raw.payload));
						if (object_type == 22)
							engine_ref = at<uint64_t (*)(void*, uint32_t)>(replay::EntityRef_layout)(ctx, uint32_t(raw.payload));
					}
					char message[256]{};
					std::snprintf(message, sizeof(message),
						"Island native Replay spawnscriptable completed: raw_var_type=%u raw_payload=0x%llX native_object_type=%u native_entity_class=%u native_entity_index=%u (return conversion not yet validated)",
						unsigned(raw.type), static_cast<unsigned long long>(raw.payload), object_type,
						unsigned(uint8_t(engine_ref >> 32)), unsigned(uint32_t(engine_ref)));
					report(nullptr, message);
				}
			}
#endif
#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
				if (native_island_scriptable_result && name == "spawnscriptable")
				{
					// ScriptableSv is the native class-8 entity returned by the
					// verified Replay 1.20 builtin. Retain its proper class and
					// engine-assigned index; do not reinterpret it as a player.
					// This snapshot is only valid for the current map epoch.
					const auto raw = *(saved_top + 1);
					if (raw.type != 1 || !raw.payload) return IW8X_TYPE_MISMATCH;
					const auto object_type = at<uint8_t (*)(void*, uint32_t)>(replay::GetObjectType)(ctx, uint32_t(raw.payload));
					if (object_type != 22) return IW8X_TYPE_MISMATCH;
					const auto ref = at<uint64_t (*)(void*, uint32_t)>(replay::EntityRef_layout)(ctx, uint32_t(raw.payload));
					if (uint8_t(ref >> 32) != 8) return IW8X_TYPE_MISMATCH;
					const auto index = uint32_t(ref);
					if (index == UINT32_MAX) return IW8X_INTERNAL_ERROR;
					*native_island_scriptable_result = {index,8};
					return IW8X_OK;
				}
#endif
			snapshot_reader reader{ ctx };
			output = reader.copy(*(saved_top + 1));
			return IW8X_OK;
			}
		catch (const vm_failure& e)
		{
			return e.status;
		}
	}

#ifdef IW8X_NATIVE_ISLAND_CRATE_VM_BRIDGE
		iw8x_status spawn_island_native_scriptable(std::span<const value> args, native_scriptable_spawn& instance)
		{
			instance = {};
			if (!in_game_scope()) return IW8X_WRONG_THREAD;
			if (native_island_scriptable_result) return IW8X_BUSY;
			if (args.size()!=3 || args[0].type()!=value_type::string ||
				args[1].type()!=value_type::vector || args[2].type()!=value_type::vector)
				return IW8X_INVALID_ARGUMENT;
			// Replay 1.20 owns both the BR crate definition and the actual
			// brloot_* ScriptableDefs. A separately opt-in world-item probe may
			// pass ONLY names drawn from Replay's own pickscriptablelootitem()
			// result. This must never turn class-8 scriptables into class-0
			// plugin entities, or accept arbitrary names from a network client.
			const auto& definition=args[0].get<std::string>();
			if (definition!="br_loot_cache")
			{
				const char* enabled=std::getenv("IW8X_ISLAND_TEST_NATIVE_WORLD_PICKUP");
				if (!enabled || enabled[0]!='1' || enabled[1]!='\0' ||
					definition.size()<=7 || definition.size()>100 ||
					definition.rfind("brloot_",0)!=0 ||
					!std::all_of(definition.begin(),definition.end(),[](unsigned char c)
					{
						return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';
					})) return IW8X_INVALID_ARGUMENT;
			}
			struct scope
			{
				~scope(){ native_island_scriptable_result=nullptr; }
			} guard;
			native_island_scriptable_result=&instance;
			value unused;
			const auto status=call_builtin_native(false, {}, "spawnscriptable", args, unused);
			if (status!=IW8X_OK) instance = {};
				else
				{
					auto& s=state();
					std::lock_guard lock(s.mutex);
					instance.map_epoch=s.epoch;
					instance.script_generation=s.script_generation;
				}
			return status;
		}

			iw8x_status query_island_native_scriptable(const native_scriptable_spawn& instance,
				std::string_view method, value& output, std::span<const value> args)
		{
			if (!in_game_scope()) return IW8X_WRONG_THREAD;
			if (native_island_scriptable_method_self || native_island_scriptable_result) return IW8X_BUSY;
			if (instance.entity_class!=8 || instance.instance_index>=32768 ||
				!instance.map_epoch || !instance.script_generation) return IW8X_INVALID_ARGUMENT;
				// Exact 1.20.4 read-only native methods with engine class-8
				// scr_entref_t. Never extend this to a state-changing method until
				// its actual ScriptableDef graph/callback semantics are established.
				const bool scalar = method=="getscriptableisloot" ||
					method=="getscriptablepartcount" || method=="getscriptableisusableonanypart" ||
					method=="getscriptableisreserved" || method=="getscriptableislinked";
				const bool name_at_zero = method=="getscriptablepartnameatindex" && args.size()==1 &&
					args[0].type()==value_type::integer && args[0].get<int32_t>()==0;
				const bool state_for_name = method=="getscriptablepartstate" && args.size()==1 &&
					args[0].type()==value_type::string && !args[0].get<std::string>().empty() &&
					args[0].get<std::string>().size()<=96;
					const bool part_has_state=method=="getscriptableparthasstate" && args.size()==2 &&
						args[0].type()==value_type::string && args[1].type()==value_type::string &&
						!args[0].get<std::string>().empty() && args[0].get<std::string>().size()<=96 &&
						args[1].get<std::string>()=="opening";
					if (!((scalar && args.empty()) || name_at_zero || state_for_name || part_has_state))
					return IW8X_UNSUPPORTED;
			{
				auto& s=state();
				std::lock_guard lock(s.mutex);
				if (!s.map_active || s.epoch!=instance.map_epoch ||
					s.script_generation!=instance.script_generation) return IW8X_STALE;
			}
			struct scope
			{
				~scope(){ native_island_scriptable_method_self=nullptr; }
			} guard;
			native_island_scriptable_method_self=&instance;
				const auto status=call_builtin_native(true, {}, method, args, output);
			if (status!=IW8X_OK) output={};
			return status;
		}

			iw8x_status begin_island_native_cache_opening(const native_scriptable_spawn& instance,
				std::string_view part)
			{
				// Do not expose a generic state mutation API. Replay's own
				// setter is responsible for genuine ScriptableSv state/event
				// dispatch; the source BR pickup callback has separate prerequisites.
				if (!in_game_scope()) return IW8X_WRONG_THREAD;
				if (native_island_scriptable_method_self || native_island_scriptable_result) return IW8X_BUSY;
				if (instance.entity_class!=8 || instance.instance_index>=32768 ||
					!instance.map_epoch || !instance.script_generation || part!="body") return IW8X_INVALID_ARGUMENT;
				const char* enabled=std::getenv("IW8X_ISLAND_TEST_OPEN_TRANSITION");
				if (!enabled || enabled[0]!='1' || enabled[1]!='\0') return IW8X_UNSUPPORTED;
				{
					auto& s=state();
					std::lock_guard lock(s.mutex);
					if (!s.map_active || s.epoch!=instance.map_epoch ||
						s.script_generation!=instance.script_generation) return IW8X_STALE;
				}
				const std::array<value,2> args{value("body"),value("opening")};
				struct scope
				{
					~scope(){native_island_scriptable_method_self=nullptr;}
				} guard;
				native_island_scriptable_method_self=&instance;
				value output;
				return call_builtin_native(true,{},"setscriptablepartstate",args,output);
			}

			iw8x_status release_consumed_island_native_scriptable(
				const native_scriptable_spawn& instance, const bool acquisition_committed)
			{
				// The original 3,543 placements cannot all own simultaneous
				// Replay reserved ScriptableSv slots. The engine itself must release
				// consumed world items: never memset a Scriptable, steal a pool slot,
				// or free a crate merely to satisfy map loading.
				if (!in_game_scope()) return IW8X_WRONG_THREAD;
				if (!acquisition_committed) return IW8X_UNSUPPORTED;
				const char* enabled=std::getenv("IW8X_ISLAND_TEST_CONSUMED_SCRIPTABLE_RECLAIM");
				if (!enabled || enabled[0]!='1' || enabled[1]!='\0') return IW8X_UNSUPPORTED;
				if (instance.entity_class!=8 || instance.instance_index>=32768 ||
					!instance.map_epoch || !instance.script_generation ||
					native_island_scriptable_result || native_island_scriptable_method_self)
					return IW8X_INVALID_ARGUMENT;
				{
					auto& s=state();
					std::lock_guard lock(s.mutex);
					if (!s.map_active || s.epoch!=instance.map_epoch ||
						s.script_generation!=instance.script_generation) return IW8X_STALE;
				}
				// Verify the native ownership class and actual reserved/loot
				// classification, rather than trusting stale source-array entries.
				value reserved, is_loot;
				// Native Replay Scr_AddBool is implemented through Scr_AddInt.
				// The public snapshot therefore exposes integer 0/1, not an
				// arbitrary plugin boolean. Reject other values fail-closed.
				const auto native_true=[](const value& v) noexcept
				{
					return (v.type()==value_type::integer && v.get<int32_t>()==1) ||
						(v.type()==value_type::boolean && v.get<bool>());
				};
				auto status=query_island_native_scriptable(instance,"getscriptableisreserved",reserved);
				if (status!=IW8X_OK) return status;
				if (!native_true(reserved)) return IW8X_NOT_FOUND;
				status=query_island_native_scriptable(instance,"getscriptableisloot",is_loot);
				if (status!=IW8X_OK) return status;
				if (!native_true(is_loot)) return IW8X_UNSUPPORTED;
				struct scope
				{
					~scope(){native_island_scriptable_method_self=nullptr;}
				} guard;
				native_island_scriptable_method_self=&instance;
				value discarded;
				return call_builtin_native(true,{},"freescriptable",{},discarded);
			}
#endif

	uint32_t resolve_script_native(void* context, const char* script, uint32_t token)
	{
		// Called only by the registry at the existing Scr_EndLoadScripts edge.
		const auto load = at<uint32_t (*)(void*, const char*)>(replay::Scr_LoadScript);
		const auto get = at<uint32_t (*)(void*, const char*, uint32_t)>(replay::Scr_GetFunctionHandle);
		const auto module_token = script_module_token(script);
		std::array<std::string, 3> modules;
		size_t count{};
		// Stock IW8 far-call decoding formats a nonzero
		// canonical module token with "%d". The resulting decimal string, not the
		// readable source path, keys the ScriptFile asset and loaded-script table.
		if (module_token) modules[count++] = std::to_string(module_token);
		// Literal-name/custom assets have no module token. Retain readable aliases
		// and the existing extension fallback when a stock identity is unavailable.
		modules[count++] = script;
		modules[count++] = std::string(script) + ".gsc";
		const auto hex = [](uint32_t number)
		{
			char buffer[11];
			std::snprintf(buffer, sizeof(buffer), "0x%08X", number);
			return std::string(buffer);
		};
		std::string attempts;
		for (size_t i = 0; i < count; ++i)
		{
			// Native loading reuses loaded modules and resolves their far-call
			// dependencies. GetFunctionHandle then selects this module's export;
			// searching unrelated modules by function name would change semantics.
			const auto loaded = load(context, modules[i].c_str());
			const auto handle = get(context, modules[i].c_str(), token);
			if (handle) return handle;
			if (i) attempts += "; ";
			attempts += "module=\"" + modules[i] + "\" load=" + hex(loaded) + " handle=" + hex(handle);
		}
		const auto message = "GSC script resolve IW8X_NOT_FOUND requested=\"" + std::string(script) + "\" function=\"" +
			std::string(script_function_name(token)) + "\" function_token=" + hex(token) + " module_token=" + hex(module_token) +
			" attempts=[" + attempts + "] final_handle=0x00000000";
		report(nullptr, message.c_str());
		return 0;
	}

	namespace
	{
		iw8x_status copy_script_result(void* ctx, const native_value& result, uint32_t result_type, value& output)
		{
			// Entities, collections and Weapons share VAR_POINTER; the object type and
			// entity class, not the function name or requested type, distinguish them.
			static constexpr uint8_t native_types[]{ 0, 6, 5, 6, 2, 4, 1, 1, 1, 0xff, 1 };
			if (result_type != IW8X_UNDEFINED && result.type != native_types[result_type]) return IW8X_TYPE_MISMATCH;
			snapshot_reader reader{ ctx };
			output = reader.copy(result);
			if (result_type == IW8X_BOOLEAN && output.type() == value_type::integer)
			{
				const auto n = output.as<int32_t>();
				if (n != 0 && n != 1) return IW8X_TYPE_MISMATCH;
				output = value(n != 0);
			}
			return result_type != IW8X_UNDEFINED && static_cast<uint32_t>(output.type()) != result_type ? IW8X_TYPE_MISMATCH : IW8X_OK;
		}

		struct script_result
		{
			const native_value* floor;
			uint32_t type;
			value& output;
			iw8x_status status{ IW8X_INTERNAL_ERROR };
			std::exception_ptr error;
		};

		void receive_script_result(void* ctx, const void*, void* data) noexcept
		{
			auto& result = *static_cast<script_result*>(data);
			try
			{
				if (read<native_value*>(ctx, replay::layout::stack_top) != result.floor + 1 ||
					read<uint32_t>(ctx, replay::layout::inparam_count) != 1 || read<uint32_t>(ctx, replay::layout::outparam_count) ||
					read<uint32_t>(ctx, replay::layout::frame_count))
					return;
				result.status = copy_script_result(ctx, *(result.floor + 1), result.type, result.output);
			}
			catch (...)
			{
				// The native caller must release its return slot before C++ unwinding.
				result.error = std::current_exception();
			}
		}

		iw8x_status invoke_script_native(
			const entity_ref* self, uint32_t function_handle, uint32_t result_type, std::span<const value> args, value& output)
		{
			if (!in_game_scope()) return IW8X_WRONG_THREAD;
			if (!function_handle) return IW8X_NOT_FOUND;
			if (current_job) return IW8X_BUSY;
			if (args.size() > 256) return IW8X_LIMIT;
			auto* ctx = server_context();
			if (!vm_idle(ctx) || read<uint8_t>(ctx, replay::layout::error_flag) || read<uint8_t>(ctx, replay::layout::loading))
				return IW8X_BUSY;
			if (self)
			{
				const auto status = validate_entity(*self);
				if (status != IW8X_OK) return status;
			}
			if (result_type > IW8X_WEAPON_SNAPSHOT) return IW8X_INVALID_ARGUMENT;
			if (result_type == IW8X_WEAPON) return IW8X_UNSUPPORTED;
			try
			{
				value_budget budget;
				for (const auto& arg : args)
					preflight(arg, budget);
				stack_room(ctx, args.size() + budget.depth + 2);
				const auto objects = read<void*>(ctx, replay::layout::parents);
				if (!objects || !read<uint32_t>(objects, game::addresses::gsc_api::parent_free_head_offset)) return IW8X_LIMIT;

				const auto* saved_top = read<native_value*>(ctx, replay::layout::stack_top);
				const auto saved_error = read<uintptr_t>(ctx, replay::layout::error_message);
				const auto saved_error_index = read<uint32_t>(ctx, replay::layout::error_index);
				const auto saved_execute = read<uint32_t>(ctx, replay::layout::execute_depth);

				struct cleanup
				{
					void* ctx;
					const native_value* top;
					uintptr_t error;
					uint32_t error_index, execute, self_object{}, thread{};

					~cleanup()
					{
						auto* current = read<native_value*>(ctx, replay::layout::stack_top);
						if (current < top || reinterpret_cast<uintptr_t>(current) > read<uintptr_t>(ctx, replay::layout::stack_max))
							std::terminate();
						while (current > top)
						{
							at<void (*)(void*, uint32_t, uint64_t)>(replay::RemoveRefToValue)(ctx, current->type, current->payload);
							*current-- = {};
						}
						if (thread) at<void (*)(void*, uint32_t)>(replay::RemoveRefToObject)(ctx, thread);
						if (self_object) at<void (*)(void*, uint32_t)>(replay::RemoveRefToObject)(ctx, self_object);
						put(ctx, replay::layout::stack_top, top);
						put<uint32_t>(ctx, replay::layout::inparam_count, 0);
						put<uint32_t>(ctx, replay::layout::outparam_count, 0);
						put(ctx, replay::layout::error_message, error);
						put(ctx, replay::layout::error_index, error_index);
						put(ctx, replay::layout::execute_depth, execute);
					}
				} guard{ ctx, saved_top, saved_error, saved_error_index, saved_execute };

				if (self)
				{
					push(ctx, value(*self));
					auto* receiver = read<native_value*>(ctx, replay::layout::stack_top);
					if (receiver != saved_top + 1 || receiver->type != 1 || !receiver->payload) return IW8X_INTERNAL_ERROR;
					// Transfer the stack reference to the guard. Self is not an argument.
					guard.self_object = static_cast<uint32_t>(receiver->payload);
					*receiver = {};
					put(ctx, replay::layout::stack_top, saved_top);
					put<uint32_t>(ctx, replay::layout::inparam_count, 0);
				}

				// Native Scr_ExecThread callers marshal parameters right-to-left through
				// Scr_Add*, then pass argc. The engine consumes those params, executes
				// the cached program-buffer-relative export, and publishes one outparam.
				for (auto it = args.rbegin(); it != args.rend(); ++it)
					push(ctx, *it);
				if (read<uint32_t>(ctx, replay::layout::inparam_count) != args.size() ||
					read<uint32_t>(ctx, replay::layout::outparam_count))
					return IW8X_INTERNAL_ERROR;
				if (self)
				{
					script_result result{ saved_top, result_type, output };
					using callback = void (*)(void*, const void*, void*);
					slow_scope timer("vm.script_on", {}, 0, function_handle);
					guard.thread = at<uint32_t (*)(void*, uint32_t, uint32_t, uint32_t, callback, const void*, void*)>(
						replay::Scr_ExecThreadInternal)(ctx, function_handle, guard.self_object, static_cast<uint32_t>(args.size()),
						&receive_script_result, nullptr, &result);
					if (result.error) std::rethrow_exception(result.error);
					if (read<native_value*>(ctx, replay::layout::stack_top) != saved_top ||
						read<uint32_t>(ctx, replay::layout::inparam_count) || read<uint32_t>(ctx, replay::layout::outparam_count) ||
						read<uint32_t>(ctx, replay::layout::frame_count))
						return IW8X_INTERNAL_ERROR;
					return result.status;
				}
				{
					slow_scope timer("vm.script", {}, 0, function_handle);
					at<void (*)(void*, uint32_t, uint32_t)>(replay::Scr_ExecThread)(
						ctx, function_handle, static_cast<uint32_t>(args.size()));
				}
				if (read<native_value*>(ctx, replay::layout::stack_top) != saved_top + 1 ||
					read<uint32_t>(ctx, replay::layout::inparam_count) || read<uint32_t>(ctx, replay::layout::outparam_count) != 1 ||
					read<uint32_t>(ctx, replay::layout::frame_count))
					return IW8X_INTERNAL_ERROR;

				return copy_script_result(ctx, *(saved_top + 1), result_type, output);
			}
			catch (const vm_failure& e)
			{
				return e.status;
			}
		}
	}

	iw8x_status call_script_native(uint32_t function_handle, uint32_t result_type, std::span<const value> args, value& output)
	{
		return invoke_script_native(nullptr, function_handle, result_type, args, output);
	}

	iw8x_status call_script_on_native(
		entity_ref self, uint32_t function_handle, uint32_t result_type, std::span<const value> args, value& output)
	{
		return invoke_script_native(&self, function_handle, result_type, args, output);
	}

	std::vector<value> read_legacy_arguments(void* ctx)
	{
		using namespace replay;
		const auto count = at<uint32_t (*)(const void*)>(Scr_GetNumParam)(ctx);
		if (count > 256) throw std::invalid_argument("GSC native argument limit exceeded");
		std::vector<value> result;
		result.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			const auto type = at<uint8_t (*)(void*, uint32_t)>(Scr_GetType)(ctx, i);
			switch (type)
			{
			case 0:
				result.emplace_back();
				break;
			case 6:
				result.emplace_back(at<int32_t (*)(void*, uint32_t)>(Scr_GetInt)(ctx, i));
				break;
			case 5:
				result.emplace_back(at<float (*)(void*, uint32_t)>(Scr_GetFloat)(ctx, i));
				break;
			case 2: {
				const auto string = at<const char* (*)(void*, uint32_t)>(Scr_GetString)(ctx, i);
				if (!string) throw std::runtime_error("null VM string");
				const auto length = strnlen_s(string, 65536);
				if (length == 65536) throw std::length_error("VM string exceeds limit");
				result.emplace_back(std::string_view(string, length));
				break;
			}
			case 4: {
				vector v{};
				at<void (*)(void*, uint32_t, float*)>(Scr_GetVector)(ctx, i, &v.x);
				result.emplace_back(v);
				break;
			}
			case 1: {
				if (at<uint8_t (*)(void*, uint32_t)>(Scr_GetPointerType)(ctx, i) != 22)
					throw std::invalid_argument("array/struct argument marshalling is not yet verified");
				const auto entity = capture_entity(at<uint64_t (*)(void*, uint32_t)>(Scr_GetEntityRef)(ctx, i));
				if (!entity.generation) throw std::invalid_argument("unsupported/stale script entity");
				result.emplace_back(entity);
				break;
			}
			default:
				throw std::invalid_argument("unsupported GSC argument type");
			}
		}
		return result;
	}

	std::vector<value> read_arguments(void* ctx)
	{
		if (!(bindings().capabilities & IW8X_CAP_RICH_VALUES)) return read_legacy_arguments(ctx);
		const auto count = at<uint32_t (*)(const void*)>(replay::Scr_GetNumParam)(ctx);
		if (count > 256) throw std::invalid_argument("GSC native argument limit exceeded");
		const auto* top = read<native_value*>(ctx, replay::layout::stack_top);
		snapshot_reader reader{ ctx };
		std::vector<value> result;
		result.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
			result.push_back(reader.copy(read<native_value>(const_cast<native_value*>(top - i), 0)));
		return result;
	}

	void prepare_return(void* ctx, const value& result)
	{
		if (result.type() == value_type::array || result.type() == value_type::struct_)
			throw std::invalid_argument("rich native callback returns require a separate protected VM boundary");
		preflight(result);
		stack_room(ctx, 1);
	}

	iw8x_status validate_return(const value& result)
	{
		if (result.type() == value_type::array || result.type() == value_type::struct_) return IW8X_UNSUPPORTED;
		try
		{
			preflight(result);
			return IW8X_OK;
		}
		catch (const vm_failure& e)
		{
			return e.status;
		}
	}

	void write_return(void* ctx, const value& result)
	{
		push(ctx, result);
	}

	iw8x_status notify_native(entity_ref target, std::string_view name, std::span<const value> args)
	{
		if (!in_game_scope()) return IW8X_WRONG_THREAD;
		auto* ctx = server_context();
		for (const auto& arg : args)
		{
			if (arg.type() == value_type::array || arg.type() == value_type::struct_) return IW8X_UNSUPPORTED;
			preflight(arg);
		}
		stack_room(ctx, args.size() + 1); // NotifyId uses one temporary stack marker.
		// Allocate C++ state before mutating the VM; do not longjmp through RAII.
		const std::string event(name);
		const auto token = at<uint32_t (*)(const char*, uint32_t)>(replay::SL_GetString)(event.c_str(), 0);
		if (!token) return IW8X_INTERNAL_ERROR;
		// A zero-argument notify must also consume native input params first.
		at<void (*)(void*)>(replay::Scr_ClearOutParams)(ctx);
		for (auto it = args.rbegin(); it != args.rend(); ++it)
			push(ctx, *it);
		if (target.generation)
			at<void (*)(void*, int32_t, uint8_t, uint32_t, uint32_t, int32_t)>(replay::Scr_NotifyNum)(
				ctx, target.index, 0, token, static_cast<uint32_t>(args.size()), 0);
		else
			at<void (*)(void*, uint32_t, uint32_t)>(replay::Scr_NotifyLevel)(ctx, token, static_cast<uint32_t>(args.size()));
		at<void (*)(void*, uint32_t, uint64_t)>(replay::RemoveRefToValue)(ctx, 2, token);
		return IW8X_OK;
	}
}
