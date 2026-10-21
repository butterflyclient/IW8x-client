#include "native_plugins.h"

#include "common/commands/command.hpp"
#include "common/console.h"

#include "dedicated/assets/bap.h"
#include "dedicated/dedicated.h"

#include "engine/image.hpp"

#include "gsc/api.hpp"
#include "gsc/compiled/gsc.hpp"
#include "gsc/compiled/link_diagnostics.hpp"
#include "gsc/internal/runtime.hpp"
#include "gsc/ownership.hpp"
#include "plugin/loader.hpp"

#include <atomic>
#include <charconv>
#include <mutex>
namespace native_plugins
{
	namespace
	{
		std::mutex install_mutex;
		std::atomic<bool> installed{};
		bool prepared{};
		std::filesystem::path plugin_root;

		void PluginLog(const char* plugin, const char* message)
		{
			console::Print("[Plugin:%s] %s", plugin, message);
		}

		void Prepare()
		{
			if (prepared) return;
			HMODULE module{};
			wchar_t path[MAX_PATH]{};
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&Install), &module))
				throw std::runtime_error("cannot locate Replay plugin host");
			const auto length = GetModuleFileNameW(module, path, MAX_PATH);
			if (!length || length >= MAX_PATH) throw std::runtime_error("plugin host path is too long");
			plugin_root = std::filesystem::path(path).parent_path() / L"plugins";
			iw8x::gsc::internal::set_log_sink(&PluginLog);
			iw8x::gsc::internal::initialize_tables(plugin_root);
			command::add("iw8x_plugins",
				[](const command::params&)
				{
					for (const auto& plugin : iw8x::plugins::loaded())
						OP_INFO("Plugin %u | %s", plugin.id, plugin.name.c_str());
				});
			command::add("iw8x_plugin_unload",
				[](const command::params& args)
				{
					uint32_t id{};
					const auto text = args[1];
					if (text.empty())
					{
						OP_WARN("Usage: iw8x_plugin_unload <plugin-id>");
						return;
					}
					const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
					if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
					{
						OP_WARN("Usage: iw8x_plugin_unload <plugin-id>");
						return;
					}
					OP_INFO("Plugin %u unload status %u", id, iw8x::plugins::unload(id));
				});
			prepared = true;
		}
	}

	hook::Status Install()
	{
		std::lock_guard lock(install_mutex);
		if (installed.load(std::memory_order_acquire)) return hook::Status::Installed;
#ifndef IW8X_CLIENT_ONLY
		if (dedicated::Enabled() && bap::Enabled() && !bap::Ready()) return hook::Status::NotReady;
#endif
		try
		{
			Prepare();
			auto status = iw8x::gsc::internal::install_runtime();
			if (status != hook::Status::Installed) return status;
			status = gsc::Install(game::base());
			if (status != hook::Status::Installed) return status;
			// The diagnostic hooks preserve the native linker/error path. A missing
			// diagnostic never changes whether the loader itself is functional.
			gsc::link_diagnostics::Install(game::base());
			iw8x::plugins::discover(plugin_root);
			installed.store(true, std::memory_order_release);
			LOG_INFO("Plugins", "Replay GSC and plugin API ready; %zu plugins loaded from %ls", iw8x::plugins::loaded().size(),
				plugin_root.c_str());
			return hook::Status::Installed;
		}
		catch (const std::exception& e)
		{
			LOG_ERR("Plugins", "%s", e.what());
			return hook::Status::Failed;
		}
	}

	void PumpWorker() noexcept
	{
		if (installed.load(std::memory_order_acquire)) iw8x::gsc::pump_worker();
	}
}
