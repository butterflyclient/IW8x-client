#include "api.hpp"
#include "context.hpp"
#include "gsc/internal/bindings.hpp"
#include "gsc/internal/state.hpp"

#include <atomic>
#include <optional>
namespace iw8x::gsc
{
	namespace
	{
		struct frame
		{
			uint64_t id;
			std::span<const value> args;
			invocation_result result;
			bool returned{};
		};

		std::atomic<uint64_t> next_call{ 1 };
		thread_local frame* current{};
	}

	namespace internal
	{
		iw8x_status register_native(
			iw8x_plugin_id id, iw8x_string name, uint32_t method, iw8x_native_callback callback, void* data, iw8x_destroy_callback destroy)
		{
			if (!callback || method > 1) return IW8X_INVALID_ARGUMENT;
			const auto plugin = find_owner(id);
			lease active(plugin);
			if (!active) return IW8X_STOPPING;
			auto key = std::make_pair(method != 0, checked_name(name));
			if (!(bindings().capabilities & (method ? IW8X_CAP_METHODS : IW8X_CAP_FUNCTIONS))) return IW8X_UNSUPPORTED;
			auto& s = state();
			std::lock_guard lock(s.mutex);
			if (s.natives.contains(key)) return IW8X_DUPLICATE;
			if (s.natives.size() >= 512) return IW8X_LIMIT;
			if (bindings().reserve_native)
			{
				const auto status = bindings().reserve_native(method != 0, plugin->name, key.second);
				if (status != IW8X_OK) return status;
			}
			s.natives.emplace(std::move(key), native_entry{ { plugin, data, destroy }, callback });
			return IW8X_OK;
		}

		iw8x_status argument_count(uint64_t call, uint32_t* count)
		{
			if (!current || current->id != call) return IW8X_STALE;
			if (!count) return IW8X_INVALID_ARGUMENT;
			*count = static_cast<uint32_t>(current->args.size());
			return IW8X_OK;
		}

		iw8x_status argument(uint64_t call, uint32_t index, iw8x_value_callback receive, void* data)
		{
			if (!current || current->id != call) return IW8X_STALE;
			if (!receive || index >= current->args.size()) return IW8X_INVALID_ARGUMENT;
			const encoded_value encoded(current->args[index]);
			receive(data, &encoded.view());
			return IW8X_OK;
		}

		iw8x_status return_value(uint64_t call, const iw8x_value* v)
		{
			if (!current || current->id != call) return IW8X_STALE;
			if (!v) return IW8X_INVALID_ARGUMENT;
			if (current->returned || current->result.status != IW8X_OK) return IW8X_INVALID_ARGUMENT;
			auto owned = value::copy(*v);
			if (bindings().validate_return)
			{
				const auto status = bindings().validate_return(owned);
				if (status != IW8X_OK) return status;
			}
			current->result.result = std::move(owned);
			current->returned = true;
			return IW8X_OK;
		}

		iw8x_status error(uint64_t call, iw8x_string message)
		{
			if (!current || current->id != call) return IW8X_STALE;
			current->result.status = IW8X_CALLBACK_ERROR;
			current->result.result = {};
			current->result.error = std::string(text(message).substr(0, 2048));
			return IW8X_OK;
		}
	}

	invocation_result invoke(std::string_view name, bool method, entity_ref self, std::span<const value> args)
	{
		using namespace internal;
		if (!in_game_scope()) return { IW8X_WRONG_THREAD, {}, "native invoked outside server boundary" };
		if (args.size() > 256) return { IW8X_LIMIT, {}, "too many arguments" };
		if (method && (!bindings().valid || bindings().valid(self) != IW8X_OK)) return { IW8X_STALE, {}, "invalid method receiver" };
		native_entry entry;
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			const auto it = s.natives.find({ method, checked_name(string_view(name)) });
			if (it == s.natives.end()) return { IW8X_NOT_FOUND, {}, "native is no longer registered" };
			entry = it->second;
		}
		lease active(entry.plugin);
		if (!active) return { IW8X_STOPPING, {}, "plugin is stopping" };
		slow_scope timer("native.callback", name, entry.plugin->id);
		const auto id = next_call.fetch_add(1);
		if (!id) return { IW8X_LIMIT, {}, "invocation IDs exhausted" };
		frame invocation{ id, args, { IW8X_OK, {}, {} }, false };

		struct restore
		{
			frame* previous;

			~restore()
			{
				current = previous;
			}
		} guard{ current };

		current = &invocation;
		if (!guarded(entry.plugin,
				[&]
				{
					entry.callback(entry.data, id, self);
				}))
		{
			invocation.result = { IW8X_CALLBACK_ERROR, {}, "native callback threw" };
		}
		else if (invocation.result.status != IW8X_OK)
		{
			fault(entry.plugin, invocation.result.error.c_str());
		}
		return std::move(invocation.result);
	}
}
