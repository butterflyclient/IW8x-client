#include "gsc/internal/state.hpp"

#include "common/commands/command.hpp"
namespace iw8x::gsc::internal
{
	namespace
	{
		void dispatch(const std::string& name, iw8x_plugin_id id, const command::params& params)
		{
			command_entry entry;
			{
				auto& s = state();
				std::lock_guard lock(s.mutex);
				const auto it = s.commands.find(name);
				if (it == s.commands.end() || it->second.plugin->id != id) return;
				entry = it->second;
			}
			lease active(entry.plugin);
			if (!active) return;
			slow_scope timer("command.callback", name, id);
			std::vector<iw8x_string> args;
			args.reserve(params.size());
			for (size_t i = 0; i < params.size(); ++i)
				args.push_back(string_view(params[i]));
			guarded(entry.plugin,
				[&]
				{
					entry.callback(entry.data, args.data(), static_cast<uint32_t>(args.size()));
				});
		}
	}

	iw8x_status add_command(iw8x_plugin_id id, iw8x_string name, iw8x_command_callback callback, void* data, iw8x_destroy_callback destroy)
	{
		if (!callback) return IW8X_INVALID_ARGUMENT;
		const auto plugin = find_owner(id);
		lease active(plugin);
		if (!active) return IW8X_STOPPING;
		const auto key = checked_name(name);
		auto& s = state();
		std::lock_guard lock(s.mutex);
		if (s.commands.contains(key)) return IW8X_DUPLICATE;
		if (s.commands.size() >= 512) return IW8X_LIMIT;
		auto [it, inserted] = s.commands.emplace(key, command_entry{ { plugin, data, destroy }, callback, 0 });
		try
		{
			it->second.bridge_token = command::try_add(key,
				[key, id](const command::params& args)
				{
					dispatch(key, id, args);
				});
		}
		catch (...)
		{
			s.commands.erase(it);
			throw;
		}
		if (!it->second.bridge_token)
		{
			s.commands.erase(it);
			return IW8X_DUPLICATE;
		}
		return IW8X_OK;
	}

	void erase_command_bridge(const std::string& name, uint64_t token)
	{
		command::remove(name, token);
	}
}
