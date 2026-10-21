#include "plugin/sdk/abi.h"
extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x_plugin_host* host, iw8x_plugin_id owner)
{
	const char name[] = "failed_plugin_command";
	host->add_command(
		owner, { name, sizeof(name) - 1 },
		[](void*, const iw8x_string*, uint32_t)
		{
		},
		nullptr, nullptr);
	return 0; // Host must remove the command before FreeLibrary.
}
