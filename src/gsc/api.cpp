#include "api.hpp"
#include "context.hpp"
#include "gsc/internal/bindings.hpp"
#include "gsc/internal/state.hpp"
#include "gsc/internal/players.hpp"
#include "plugin/sdk/dvars.hpp"

#include <type_traits>
namespace iw8x::gsc
{
	namespace
	{
		// Every status-returning host entry is noexcept even on allocation failure.
		template <auto Function>
		struct boundary;

		template <class... Args, iw8x_status (*Function)(Args...)>
		struct boundary<Function>
		{
			static iw8x_status call(Args... args) noexcept
			{
				try
				{
					return Function(args...);
				}
				catch (const std::bad_variant_access&)
				{
					return IW8X_TYPE_MISMATCH;
				}
				catch (const std::invalid_argument&)
				{
					return IW8X_INVALID_ARGUMENT;
				}
				catch (const std::length_error&)
				{
					return IW8X_LIMIT;
				}
				catch (...)
				{
					return IW8X_INTERNAL_ERROR;
				}
			}
		};

		uint64_t capabilities() noexcept
		{
			return internal::bindings().capabilities | IW8X_CAP_COMMANDS;
		}

		iw8x_status valid(entity_ref entity)
		{
			if (!internal::in_game_scope()) return IW8X_WRONG_THREAD;
			const auto fn = internal::bindings().valid;
			return fn ? fn(entity) : IW8X_UNSUPPORTED;
		}

		iw8x_status get_field(entity_ref entity, iw8x_string name, iw8x_value_callback receive, void* data)
		{
			if (!receive) return IW8X_INVALID_ARGUMENT;
			const auto status = valid(entity);
			if (status != IW8X_OK) return status;
			const auto fn = internal::bindings().get_field;
			if (!fn) return IW8X_UNSUPPORTED;
			value output;
			const auto result = fn(entity, internal::checked_name(name), output);
			if (result != IW8X_OK) return result;
			const encoded_value encoded(output);
			receive(data, &encoded.view());
			return IW8X_OK;
		}

		iw8x_status set_field(entity_ref entity, iw8x_string name, const iw8x_value* input)
		{
			if (!input) return IW8X_INVALID_ARGUMENT;
			const auto status = valid(entity);
			if (status != IW8X_OK) return status;
			const auto fn = internal::bindings().set_field;
			if (!fn) return IW8X_UNSUPPORTED;
			return fn(entity, internal::checked_name(name), value::copy(*input));
		}

		iw8x_status notify(entity_ref entity, iw8x_string name, const iw8x_value* input, uint32_t count)
		{
			if (!internal::in_game_scope()) return IW8X_WRONG_THREAD;
			if (count > 256 || (count && !input)) return IW8X_INVALID_ARGUMENT;
			if (entity.generation)
			{
				const auto status = valid(entity);
				if (status != IW8X_OK) return status;
			}
			else if (entity.index || entity.reserved)
				return IW8X_STALE;
			const auto fn = internal::bindings().notify;
			if (!fn) return IW8X_UNSUPPORTED;
			std::vector<value> args;
			args.reserve(count);
			for (uint32_t i = 0; i < count; ++i)
				args.push_back(value::copy(input[i]));
			return fn(entity, internal::checked_name(name), args);
		}

		iw8x_status get_dvar_string(iw8x_string name, iw8x_value_callback receive, void* data)
		{
			if (!receive) return IW8X_INVALID_ARGUMENT;
			if (!internal::in_game_scope()) return IW8X_WRONG_THREAD;
			const auto raw = text(name);
			if (raw.empty() || raw.size() > 127 || raw.find('\0') != std::string_view::npos) return IW8X_INVALID_ARGUMENT;
			const auto fn = internal::bindings().get_dvar_string;
			if (!fn || !(internal::bindings().capabilities & IW8X_CAP_DVARS)) return IW8X_UNSUPPORTED;
			value output;
			const auto status = fn(raw, output);
			if (status != IW8X_OK) return status;
			const encoded_value encoded(output);
			receive(data, &encoded.view());
			return IW8X_OK;
		}

		iw8x_status player_valid(iw8x_player player)
		{
			const auto fn = internal::bindings().player_valid;
			return fn && (internal::bindings().capabilities & IW8X_CAP_PLAYERS) ? fn(player) : IW8X_UNSUPPORTED;
		}

		iw8x_status player_name(iw8x_player player, iw8x_value_callback receive, void* data)
		{
			if (!receive) return IW8X_INVALID_ARGUMENT;
			const auto fn = internal::bindings().player_name;
			if (!fn || !(internal::bindings().capabilities & IW8X_CAP_PLAYERS)) return IW8X_UNSUPPORTED;
			value output;
			const auto status = fn(player, output);
			if (status != IW8X_OK) return status;
			const encoded_value encoded(output);
			receive(data, &encoded.view());
			return IW8X_OK;
		}

		iw8x_status player_entity(iw8x_player player, iw8x_entity* entity)
		{
			if (!entity) return IW8X_INVALID_ARGUMENT;
			*entity = {};
			const auto fn = internal::bindings().player_entity;
			return fn && (internal::bindings().capabilities & IW8X_CAP_PLAYERS) ? fn(player, *entity) : IW8X_UNSUPPORTED;
		}

		iw8x_status player_client_num(iw8x_player player, uint32_t* output)
		{
			if (!output) return IW8X_INVALID_ARGUMENT;
			*output = UINT32_MAX;
			const auto status = player_valid(player);
			if (status == IW8X_OK) *output = player.client_num;
			return status;
		}

		iw8x_status player_xuid(iw8x_player player, uint64_t* output)
		{
			if (!output) return IW8X_INVALID_ARGUMENT;
			*output = 0;
			const auto status = player_valid(player);
			return status == IW8X_OK ? IW8X_UNSUPPORTED : status;
		}

		iw8x_status players(iw8x_players_callback receive, void* data)
		{
			if (!receive) return IW8X_INVALID_ARGUMENT;
			if (!(internal::bindings().capabilities & IW8X_CAP_PLAYERS)) return IW8X_UNSUPPORTED;
			const auto snapshot = internal::connected_players();
			receive(data, snapshot.data(), static_cast<uint32_t>(snapshot.size()));
			return IW8X_OK;
		}

		iw8x_status validate_chat_message(iw8x_string message, std::string_view& output)
		{
			output = text(message);
			if (output.empty()) return IW8X_INVALID_ARGUMENT;
			if (output.size() > 120) return IW8X_LIMIT;
			for (const auto ch : output)
			{
				const auto byte = static_cast<unsigned char>(ch);
				if (byte < 0x20 || ch == '"') return IW8X_INVALID_ARGUMENT;
			}
			return IW8X_OK;
		}

		iw8x_status broadcast(iw8x_string message)
		{
			internal::slow_scope timer("chat.broadcast");
			if (!internal::in_game_scope()) return IW8X_WRONG_THREAD;
			std::string_view raw;
			const auto valid = validate_chat_message(message, raw);
			if (valid != IW8X_OK) return valid;
			const auto fn = internal::bindings().broadcast_chat;
			if (!fn || !(internal::bindings().capabilities & IW8X_CAP_CHAT)) return IW8X_UNSUPPORTED;
			return fn(raw);
		}

		iw8x_status tell(iw8x_player player, iw8x_string message)
		{
			internal::slow_scope timer("chat.tell", {}, 0, player.client_num);
			if (!internal::in_game_scope()) return IW8X_WRONG_THREAD;
			const auto player_status = player_valid(player);
			if (player_status != IW8X_OK) return player_status;
			std::string_view raw;
			const auto valid = validate_chat_message(message, raw);
			if (valid != IW8X_OK) return valid;
			const auto fn = internal::bindings().tell_chat;
			if (!fn || !(internal::bindings().capabilities & IW8X_CAP_CHAT)) return IW8X_UNSUPPORTED;
			return fn(player, raw);
		}

		void log(iw8x_plugin_id id, iw8x_string message) noexcept
		{
			try
			{
				const auto plugin = internal::find_owner(id);
				internal::queue_log(plugin ? std::string_view(plugin->name) : "host", text(message).substr(0, 2048));
			}
			catch (...)
			{
			}
		}

		iw8x_status call_builtin(iw8x_plugin_id id, uint32_t method, entity_ref self, iw8x_string name, const iw8x_value* input,
			uint32_t count, iw8x_value_callback receive, void* data)
		{
			internal::slow_scope timer(method ? "gsc.method" : "gsc.builtin", text(name), id);
			if (!internal::in_game_scope()) return IW8X_WRONG_THREAD;
			if (!receive || method > 1 || (count && !input)) return IW8X_INVALID_ARGUMENT;
			if (count > 256) return IW8X_LIMIT;
			const auto plugin = internal::find_owner(id);
			internal::lease active(plugin);
			if (!active) return IW8X_STOPPING;
			if (method)
			{
				const auto status = valid(self);
				if (status != IW8X_OK) return status;
			}
			else if (self.index || self.reserved || self.generation)
				return IW8X_INVALID_ARGUMENT;
			const auto& binding = internal::bindings();
			if (!(binding.capabilities & IW8X_CAP_GSC_CALL) || !binding.call_builtin) return IW8X_UNSUPPORTED;
			const auto key = internal::checked_name(name);
			std::vector<value> args;
			args.reserve(count);
			for (uint32_t i = 0; i < count; ++i)
				args.push_back(value::copy(input[i]));
			value result;
			const auto status = binding.call_builtin(method != 0, self, key, args, result);
			if (status != IW8X_OK) return status;
			const encoded_value encoded(result);
			// Owner lease covers result delivery as well as engine execution.
			return internal::guarded(plugin,
					   [&]
					   {
						   receive(data, &encoded.view());
					   })
				? IW8X_OK
				: IW8X_CALLBACK_ERROR;
		}

		iw8x_status get_dvar(iw8x_plugin_id id, iw8x_string name, uint32_t type, iw8x_value_callback receive, void* data)
		{
			if (!internal::in_game_scope()) return IW8X_WRONG_THREAD;
			if (!receive || (type != IW8X_STRING && type != IW8X_BOOLEAN && type != IW8X_INTEGER && type != IW8X_NUMBER))
				return IW8X_INVALID_ARGUMENT;
			const auto plugin = internal::find_owner(id);
			internal::lease active(plugin);
			if (!active) return IW8X_STOPPING;
			const auto raw = text(name);
			if (raw.empty() || raw.size() > 127 || raw.find('\0') != std::string_view::npos) return IW8X_INVALID_ARGUMENT;
			const auto retail = dvar_names::resolve(raw);
			if (retail.empty()) return IW8X_NOT_FOUND;
			const auto& binding = internal::bindings();
			if (!(binding.capabilities & IW8X_CAP_TYPED_DVARS) || !binding.get_dvar) return IW8X_UNSUPPORTED;
			value result;
			const auto status = binding.get_dvar(retail, type, result);
			if (status != IW8X_OK) return status;
			const encoded_value encoded(result);
			return internal::guarded(plugin,
					   [&]
					   {
						   receive(data, &encoded.view());
					   })
				? IW8X_OK
				: IW8X_CALLBACK_ERROR;
		}

		iw8x_status register_script(
			iw8x_plugin_id id, iw8x_string script, uint32_t function_token, uint32_t result_type, iw8x_registration* registration)
		{
			const auto& binding = internal::bindings();
			if (!(binding.capabilities & IW8X_CAP_SCRIPT_CALL) || !binding.call_script) return IW8X_UNSUPPORTED;
			return internal::register_script(id, script, function_token, result_type, registration);
		}

		iw8x_status invoke_script(iw8x_plugin_id id, iw8x_registration registration, const entity_ref* self, const iw8x_value* input,
			uint32_t count, iw8x_value_callback receive, void* data)
		{
			if (!internal::in_game_scope()) return IW8X_WRONG_THREAD;
			if (!registration || !receive || (count && !input)) return IW8X_INVALID_ARGUMENT;
			if (count > 256) return IW8X_LIMIT;
			const auto plugin = internal::find_owner(id);
			internal::lease active(plugin);
			if (!active) return IW8X_STOPPING;
			internal::slow_scope timer("gsc.script", {}, id, registration);
			const auto& binding = internal::bindings();
			if (!(binding.capabilities & IW8X_CAP_SCRIPT_CALL) || (self ? !binding.call_script_on : !binding.call_script))
				return IW8X_UNSUPPORTED;
			uint32_t function_handle{}, result_type{};
			std::string_view label;
			const auto resolved = internal::registered_script_handle(id, registration, function_handle, result_type, &label);
			timer.name(label);
			if (resolved != IW8X_OK) return resolved;
			std::vector<value> args;
			args.reserve(count);
			for (uint32_t i = 0; i < count; ++i)
				args.push_back(value::copy(input[i]));
			value result;
			const auto status = self ? binding.call_script_on(*self, function_handle, result_type, args, result)
				: binding.call_script(function_handle, result_type, args, result);
			if (status != IW8X_OK) return status;
			const encoded_value encoded(result);
			return internal::guarded(plugin,
					   [&]
					   {
						   receive(data, &encoded.view());
					   })
				? IW8X_OK
				: IW8X_CALLBACK_ERROR;
		}

		iw8x_status call_script(iw8x_plugin_id id, iw8x_registration registration, const iw8x_value* input, uint32_t count,
			iw8x_value_callback receive, void* data)
		{
			return invoke_script(id, registration, nullptr, input, count, receive, data);
		}

		iw8x_status call_script_on(iw8x_plugin_id id, iw8x_registration registration, entity_ref self, const iw8x_value* input,
			uint32_t count, iw8x_value_callback receive, void* data)
		{
			return invoke_script(id, registration, &self, input, count, receive, data);
		}

		iw8x_status register_script_name(
			iw8x_plugin_id id, iw8x_string script, iw8x_string function, uint32_t result_type, iw8x_registration* registration)
		{
			if (!registration) return IW8X_INVALID_ARGUMENT;
			*registration = 0;
			if (result_type > IW8X_WEAPON_SNAPSHOT) return IW8X_INVALID_ARGUMENT;
			if (result_type == IW8X_WEAPON) return IW8X_UNSUPPORTED;
			const auto token = internal::script_function_token(function);
			if (!token) return IW8X_NOT_FOUND;
			return register_script(id, script, token, result_type, registration);
		}

		void callback_error(iw8x_plugin_id id, iw8x_string message) noexcept
		{
			try
			{
				internal::fault(internal::find_owner(id), std::string(text(message).substr(0, 2048)).c_str());
			}
			catch (...)
			{
			}
		}

		iw8x_status hook_function(iw8x_plugin_id owner, iw8x_string script, iw8x_string function, iw8x_native_callback callback, void*,
			iw8x_destroy_callback, iw8x_registration* out)
		{
			if (!callback || !out || text(script).empty() || text(function).empty()) return IW8X_INVALID_ARGUMENT;
			internal::lease active(internal::find_owner(owner));
			return active ? IW8X_UNSUPPORTED : IW8X_STOPPING;
		}
#define ENTRY(name) &boundary<&internal::name>::call
		const iw8x_plugin_host host{ IW8X_PLUGIN_API_VERSION, sizeof(iw8x_plugin_host), &capabilities, &supported_events,
			ENTRY(register_native), ENTRY(argument_count), ENTRY(argument), ENTRY(return_value), ENTRY(error), &boundary<&valid>::call,
			&boundary<&get_field>::call, &boundary<&set_field>::call, &boundary<&notify>::call, ENTRY(subscribe), ENTRY(schedule),
			ENTRY(async), ENTRY(cancel_task), ENTRY(create_handle), ENTRY(with_handle), ENTRY(release_handle), ENTRY(add_command), &log,
			&callback_error, &boundary<&hook_function>::call, &boundary<&get_dvar_string>::call, &boundary<&player_valid>::call,
			&boundary<&player_name>::call, &boundary<&player_entity>::call, &boundary<&player_client_num>::call,
			&boundary<&player_xuid>::call, &boundary<&players>::call, &boundary<&broadcast>::call, &boundary<&tell>::call,
			&boundary<&call_builtin>::call, &boundary<&get_dvar>::call, &boundary<&register_script>::call, &boundary<&call_script>::call,
			&boundary<&register_script_name>::call, &boundary<&call_script_on>::call };
#undef ENTRY
		static_assert(std::is_standard_layout_v<iw8x_plugin_host> && std::is_trivially_copyable_v<iw8x_value>);
		static_assert(sizeof(iw8x_entity) == 16 && sizeof(iw8x_player) == 24 && sizeof(iw8x_value) == 24);
		static_assert(offsetof(iw8x_event, entity) == 16 && offsetof(iw8x_event, player) == 32);
	}

	const iw8x_plugin_host& host_api()
	{
		return host;
	}
}
