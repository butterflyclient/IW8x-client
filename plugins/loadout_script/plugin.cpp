#include "plugin/sdk/gsc.hpp"

#include <exception>
#include <string>

namespace
{
	void log(const iw8x::api& api, const std::string& message)
	{
		api.raw().log(api.owner(), iw8x::gsc::string_view(message));
	}
}

extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host, iw8x::plugin_id owner) noexcept
{
	try
	{
		iw8x::api api(host, owner);
		if (!(host->capabilities() & IW8X_CAP_SCRIPT_CALL) || !(host->supported_events() & (1ull << IW8X_MAP_READY)))
			return 0;

		// Register before Scr_EndLoadScripts releases the compiler lookup tables.
		// The canonical asset name selects the installed 1816.gscbin override.
		const auto main = api.gsc().script("1816", "main", iw8x::gsc::value_type::integer);
		api.subscribe(IW8X_MAP_READY,
			[api, main](const iw8x_event&)
			{
				try
				{
					log(api, "starting 1816::main on the gameplay server");
					if (main().as<int>() != 1)
						throw std::runtime_error("custom script did not confirm initialization");
					log(api, "custom 1816 initialization confirmed");
				}
				catch (const std::exception& error)
				{
					log(api, std::string("1816::main failed: ") + error.what());
				}
			});
		log(api, "registered 1816::main for automatic map startup");
		return 1;
	}
	catch (...)
	{
		return 0;
	}
}
