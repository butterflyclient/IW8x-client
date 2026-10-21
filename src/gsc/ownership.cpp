#include "ownership.hpp"
#include "gsc/internal/state.hpp"
#include "gsc/internal/diagnostics.hpp"

#include <atomic>
#include <exception>
namespace iw8x::gsc::internal
{
	namespace
	{
		thread_local unsigned callback_depth{};
		thread_local uint64_t callback_failures{};
	}

	registry& state()
	{
		static auto* instance = new registry;
		return *instance;
	}

	void fault(const std::shared_ptr<owner>& plugin, const char* message) noexcept
	{
		++callback_failures;
		report(plugin, message);
		if (!plugin) return;
		std::lock_guard lock(plugin->mutex);
		if (++plugin->faults >= 3) plugin->accepting = false;
	}

	bool guarded(const std::shared_ptr<owner>& plugin, const std::function<void()>& callback) noexcept
	{
		const auto previous = diagnostic_owner(plugin ? plugin->id : 0);
		slow_scope timer("plugin.dispatch", plugin ? std::string_view(plugin->name) : "host");
		++callback_depth;
		const auto before = callback_failures;
		bool ok = true;
		try
		{
			callback();
		}
		catch (const std::exception& e)
		{
			fault(plugin, e.what());
			ok = false;
		}
		catch (...)
		{
			fault(plugin, "callback threw a nonstandard C++ exception");
			ok = false;
		}
		--callback_depth;
		diagnostic_owner(previous);
		return ok && before == callback_failures;
	}

	bool inside_callback() noexcept
	{
		return callback_depth != 0;
	}

	lease::lease(std::shared_ptr<owner> plugin)
	{
		if (!plugin) return;
		slow_scope timer("owner.acquire", plugin->name, plugin->id, 0, 500);
		std::lock_guard lock(plugin->mutex);
		if (!plugin->accepting) return;
		++plugin->active;
		owner_ = std::move(plugin);
	}

	lease::~lease()
	{
		if (!owner_) return;
		std::lock_guard lock(owner_->mutex);
		if (!--owner_->active) owner_->idle.notify_all();
	}

	std::shared_ptr<owner> find_owner(iw8x_plugin_id id)
	{
		auto& s = state();
		std::lock_guard lock(s.mutex);
		const auto it = s.owners.find(id);
		return it == s.owners.end() ? nullptr : it->second;
	}

	iw8x_plugin_id create_owner(std::string name)
	{
		if (name.empty() || name.size() > 128) return 0;
		auto plugin = std::make_shared<owner>();
		plugin->name = std::move(name);
		auto& s = state();
		std::lock_guard lock(s.mutex);
		if (s.next_owner > UINT32_MAX) return 0;
		plugin->id = static_cast<uint32_t>(s.next_owner++);
		s.owners.emplace(plugin->id, plugin);
		return plugin->id;
	}

	bool destroy_data(const owned_data& data) noexcept
	{
		return !data.destroy ||
			guarded(data.plugin,
				[&]
				{
					data.destroy(data.data);
				});
	}

	iw8x_status unload_owner(iw8x_plugin_id id, iw8x_plugin_unload before_release)
	{
		// Never wait for our own callback, or unload another plugin from a callback
		// that could be part of a dependency cycle. Operators unload outside dispatch.
		if (inside_callback()) return IW8X_BUSY;
		const auto plugin = find_owner(id);
		if (!plugin) return IW8X_NOT_FOUND;
		{
			std::unique_lock lock(plugin->mutex);
			if (plugin->cleanup_failed) return IW8X_CALLBACK_ERROR;
			if (plugin->unloading) return IW8X_BUSY;
			plugin->unloading = true;
			plugin->accepting = false;
			plugin->idle.wait(lock,
				[&]
				{
					return plugin->active == 0;
				});
		}
		std::vector<owned_data> removed;
		std::vector<std::pair<std::string, uint64_t>> commands;

		struct reset_unloading
		{
			std::shared_ptr<owner> plugin;

			~reset_unloading()
			{
				std::lock_guard lock(plugin->mutex);
				plugin->unloading = false;
			}
		} reset{ plugin };

		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			// Reserve before modifying anything. If allocation fails the loader must
			// keep this DLL mapped; there is no unsafe best-effort FreeLibrary path.
			removed.reserve(s.natives.size() + s.listeners.size() + s.tasks.size() + s.commands.size() + s.resources.size());
			commands.reserve(s.commands.size());
			for (const auto& [name, entry] : s.commands)
				if (entry.plugin == plugin) commands.emplace_back(name, entry.bridge_token);
			const auto remove = [&](auto& map)
			{
				for (auto it = map.begin(); it != map.end();)
				{
					if (it->second.plugin == plugin)
					{
						removed.push_back(it->second);
						it = map.erase(it);
					}
					else
						++it;
				}
			};
			remove(s.natives);
			remove(s.listeners);
			for (auto it = s.scripts.begin(); it != s.scripts.end();)
			{
				if (it->second.plugin == plugin)
					it = s.scripts.erase(it);
				else
					++it;
			}
			for (auto it = s.tasks.begin(); it != s.tasks.end();)
			{
				if (it->second->plugin == plugin)
				{
					removed.push_back(*it->second);
					it = s.tasks.erase(it);
				}
				else
					++it;
			}
			for (auto it = s.commands.begin(); it != s.commands.end();)
			{
				if (it->second.plugin == plugin)
				{
					removed.push_back(it->second);
					it = s.commands.erase(it);
				}
				else
					++it;
			}
			for (auto& resource : s.resources)
				if (resource.occupied && resource.plugin == plugin)
				{
					removed.push_back(resource);
					retire_resource(resource);
				}
		}
		for (const auto& [name, token] : commands)
			erase_command_bridge(name, token);
		bool clean = !before_release || guarded(plugin, before_release);
		for (const auto& data : removed)
			clean = destroy_data(data) && clean;
		if (!clean)
		{
			{
				std::lock_guard lock(plugin->mutex);
				plugin->cleanup_failed = true;
			}
			report(plugin, "unload cleanup failed; DLL stays mapped and disabled until process exit");
			return IW8X_CALLBACK_ERROR;
		}
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			s.owners.erase(id);
		}
		return IW8X_OK;
	}

	std::string checked_name(iw8x_string name)
	{
		const auto view = text(name);
		if (view.empty() || view.size() > 127) throw std::invalid_argument("invalid name length");
		std::string out;
		out.reserve(view.size());
		for (unsigned char c : view)
		{
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
				throw std::invalid_argument("names must contain ASCII letters, digits, or underscores");
			out.push_back(static_cast<char>(c));
		}
		if (out.front() >= '0' && out.front() <= '9') throw std::invalid_argument("name starts with digit");
		return out;
	}
}
