#include "loader.hpp"
#include "gsc/api.hpp"
#include "gsc/ownership.hpp"
#include <Windows.h>

#include <algorithm>
#include <map>
#include <mutex>
namespace iw8x::plugins
{
	namespace
	{
		struct loaded_module
		{
			loaded_plugin info;
			HMODULE handle{};
			iw8x_plugin_unload shutdown{};
			bool unloading{};
		};

		struct modules
		{
			std::mutex mutex;
			std::map<iw8x_plugin_id, loaded_module> entries;
			bool discovering{};
		};

		modules& state()
		{
			static auto* s = new modules;
			return *s;
		}

		void load_one(const std::filesystem::path& path)
		{
			const auto description = read_manifest(path);
			{
				auto& s = state();
				std::lock_guard lock(s.mutex);
				for (const auto& [id, item] : s.entries)
					if (item.info.path == description.entry || item.info.name == description.name)
						throw std::runtime_error("duplicate plugin path or name");
			}
			// Absolute path and restricted dependency search; no CWD DLL search.
			const HMODULE dll =
				LoadLibraryExW(description.entry.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			if (!dll) throw std::runtime_error("LoadLibraryEx failed: " + std::to_string(GetLastError()));
			const auto start = reinterpret_cast<iw8x_plugin_load>(GetProcAddress(dll, "IW8X_PluginLoad"));
			const auto stop = reinterpret_cast<iw8x_plugin_unload>(GetProcAddress(dll, "IW8X_PluginUnload"));
			if (!start)
			{
				FreeLibrary(dll);
				throw std::runtime_error("missing IW8X_PluginLoad export");
			}
			iw8x_plugin_id id{};
			try
			{
				id = gsc::internal::create_owner(description.name);
			}
			catch (...)
			{
				FreeLibrary(dll);
				throw;
			}
			if (!id)
			{
				FreeLibrary(dll);
				throw std::runtime_error("plugin owner IDs exhausted");
			}
			// Publish bookkeeping before calling user code so allocation failure cannot
			// lose a module containing registered callbacks.
			try
			{
				auto& s = state();
				std::lock_guard lock(s.mutex);
				s.entries.emplace(id, loaded_module{ { id, description.name, description.entry }, dll, stop });
			}
			catch (...)
			{
				gsc::internal::unload_owner(id);
				FreeLibrary(dll);
				throw;
			}
			const auto owner = gsc::internal::find_owner(id);
			uint32_t result{};
			bool ok{};
			{
				gsc::internal::lease active(owner);
				ok = active &&
					gsc::internal::guarded(owner,
						[&]
						{
							result = start(&gsc::host_api(), id);
						});
			}
			if (!ok || !result)
			{
				unload(id);
				throw std::runtime_error("IW8X_PluginLoad failed; registrations rolled back");
			}
		}
	}

	void discover(const std::filesystem::path& root)
	{
		auto& s = state();
		{
			std::lock_guard lock(s.mutex);
			if (s.discovering) return;
			s.discovering = true;
		}

		struct reset
		{
			modules& s;

			~reset()
			{
				std::lock_guard lock(s.mutex);
				s.discovering = false;
			}
		} guard{ s };

		if (!std::filesystem::exists(root)) return;
		std::vector<std::filesystem::path> paths;
		for (const auto& item : std::filesystem::directory_iterator(root))
		{
			if (item.is_directory() && std::filesystem::is_regular_file(item.path() / "plugin.json"))
				paths.push_back(item.path() / "plugin.json");
			if (paths.size() > 128) throw std::runtime_error("plugin directory exceeds 128 manifests");
		}
		std::sort(paths.begin(), paths.end()); // Stable builtin allocation order.
		for (const auto& path : paths)
		{
			try
			{
				load_one(path);
			}
			catch (const std::exception& e)
			{
				gsc::internal::report(nullptr, (path.string() + ": " + e.what()).c_str());
			}
		}
	}

	iw8x_status unload(iw8x_plugin_id id)
	{
		loaded_module item;
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			const auto it = s.entries.find(id);
			if (it == s.entries.end()) return IW8X_NOT_FOUND;
			if (it->second.unloading) return IW8X_BUSY;
			item = it->second;
			it->second.unloading = true;
		}
		iw8x_status result;
		try
		{
			result = gsc::internal::unload_owner(id, item.shutdown);
		}
		catch (...)
		{
			result = IW8X_INTERNAL_ERROR;
		}
		if (result == IW8X_OK)
		{
			// All work and host-held plugin destructors have drained at this point.
			FreeLibrary(item.handle);
			auto& s = state();
			std::lock_guard lock(s.mutex);
			s.entries.erase(id);
		}
		else
		{
			auto& s = state();
			std::lock_guard lock(s.mutex);
			s.entries.at(id).unloading = false;
		}
		return result;
	}

	std::vector<loaded_plugin> loaded()
	{
		auto& s = state();
		std::lock_guard lock(s.mutex);
		std::vector<loaded_plugin> out;
		for (const auto& [id, item] : s.entries)
			out.push_back(item.info);
		return out;
	}

	void unload_all()
	{
		auto snapshot = loaded();
		for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it)
			unload(it->id);
	}
}
