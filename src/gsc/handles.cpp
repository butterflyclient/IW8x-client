#include "gsc/internal/state.hpp"
namespace iw8x::gsc::internal
{
	namespace
	{
		// Positive 31-bit GSC integers: 16-bit slot (one-based) + 15-bit generation.
		// Retire slots at generation exhaustion instead of wrapping into stale IDs.
		constexpr uint32_t slot_mask = 0xFFFF, max_generation = 0x7FFF;

		resource* lookup(registry& s, const std::shared_ptr<owner>& plugin, iw8x_native_handle token, uint64_t type)
		{
			if (token <= 0) return nullptr;
			const uint32_t index = static_cast<uint32_t>(token) & slot_mask;
			const uint32_t generation = static_cast<uint32_t>(token) >> 16;
			if (!index || index > s.resources.size()) return nullptr;
			auto& r = s.resources[index - 1];
			if (!r.occupied || r.plugin != plugin || r.type != type || r.generation != generation ||
				(r.scope == IW8X_SCOPE_MAP && (!s.map_active || r.epoch != s.epoch)))
				return nullptr;
			return &r;
		}
	}

	void retire_resource(resource& item)
	{
		item.occupied = false;
		item.plugin.reset();
		item.data = nullptr;
		item.destroy = nullptr;
		if (item.generation == max_generation)
			item.retired = true;
		else
			++item.generation;
	}

	iw8x_status create_handle(
		iw8x_plugin_id id, uint64_t type, uint32_t scope, void* data, iw8x_destroy_callback destroy, iw8x_native_handle* output)
	{
		if (!type || !data || !destroy || !output || scope > IW8X_SCOPE_MAP) return IW8X_INVALID_ARGUMENT;
		const auto plugin = find_owner(id);
		lease active(plugin);
		if (!active) return IW8X_STOPPING;
		auto& s = state();
		std::lock_guard lock(s.mutex);
		if (scope == IW8X_SCOPE_MAP && !s.map_active) return IW8X_STALE;
		size_t index = 0;
		while (index < s.resources.size() && (s.resources[index].occupied || s.resources[index].retired))
			++index;
		if (index == slot_mask) return IW8X_LIMIT;
		if (index == s.resources.size()) s.resources.emplace_back();
		auto& r = s.resources[index];
		r.plugin = plugin;
		r.data = data;
		r.destroy = destroy;
		r.type = type;
		r.scope = scope;
		r.epoch = s.epoch;
		r.occupied = true;
		r.borrows = 0;
		*output = static_cast<int32_t>((r.generation << 16) | static_cast<uint32_t>(index + 1));
		return IW8X_OK;
	}

	iw8x_status with_handle(iw8x_plugin_id id, iw8x_native_handle token, uint64_t type, iw8x_resource_callback callback, void* data)
	{
		if (!callback) return IW8X_INVALID_ARGUMENT;
		const auto plugin = find_owner(id);
		lease active(plugin);
		if (!active) return IW8X_STOPPING;
		void* pointer{};
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			auto* r = lookup(s, plugin, token, type);
			if (!r) return IW8X_STALE;
			if (r->borrows) return IW8X_BUSY; // Exclusive borrow; no global lock in callback.
			++r->borrows;
			pointer = r->data;
		}
		const bool ok = guarded(plugin,
			[&]
			{
				callback(data, pointer);
			});
		owned_data expired;
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			auto& r = s.resources[(static_cast<uint32_t>(token) & slot_mask) - 1];
			--r.borrows;
			if (r.scope == IW8X_SCOPE_MAP && (!s.map_active || r.epoch != s.epoch))
			{
				expired = r;
				retire_resource(r);
			}
		}
		destroy_data(expired);
		return ok ? IW8X_OK : IW8X_CALLBACK_ERROR;
	}

	iw8x_status release_handle(iw8x_plugin_id id, iw8x_native_handle token, uint64_t type)
	{
		const auto plugin = find_owner(id);
		lease active(plugin);
		if (!active) return IW8X_STOPPING;
		owned_data removed;
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			auto* r = lookup(s, plugin, token, type);
			if (!r) return IW8X_STALE;
			if (r->borrows) return IW8X_BUSY;
			removed = *r;
			retire_resource(*r);
		}
		destroy_data(removed);
		return IW8X_OK;
	}
}
