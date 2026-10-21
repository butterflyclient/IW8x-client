#include "plugin/sdk/gsc.hpp"

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
		api.function("iw8x_restart_map",
			[api](iw8x::gsc::context& ctx)
			{
				try
				{
					api.game_thread(
						[api]
						{
							try
							{
								// This native is called globally by the game-end watcher.
								// Apply the script's player presentation to each client.
								for (const auto& player : api.players())
								{
									try
									{
										if (const auto entity = player.entity())
										{
											api.gsc().method(*entity, "iprintlnbold", "^1Restarting Map...");
											api.gsc().method(*entity, "freezecontrols", 1);
											api.gsc().method(*entity, "setclientomnvar", "ui_total_fade", 1);
										}
									}
									catch (const iw8x::api_error& e)
									{
										if (e.code() != IW8X_STALE)
											log(api, "Restart On Game End player presentation status=" + std::to_string(e.code()));
									}
								}

								// Equivalent to wait 0.5; yield rather than blocking RunFrame.
								api.game_thread(
									[api]
									{
										const char* stage = "notify restarting";
										try
										{
											api.level().notify("restarting");
											stage = "map_restart";
											api.gsc().call("map_restart", 1);
										}
										catch (const iw8x::api_error& e)
										{
											log(api, "Restart On Game End " + std::string(stage) + " status=" + std::to_string(e.code()));
										}
									},
									500, IW8X_SCOPE_MAP);
							}
							catch (const iw8x::api_error& e)
							{
								log(api, "Restart On Game End prepare/queue status=" + std::to_string(e.code()));
							}
						},
						0, IW8X_SCOPE_MAP);

					ctx.return_value(true);
				}
				catch (const iw8x::api_error& e)
				{
					log(api, "Restart On Game End queue status=" + std::to_string(e.code()));
					ctx.return_value(false);
				}
			});

		return 1;
	}
	catch (const std::exception& e)
	{
		if (host && host->version == IW8X_PLUGIN_API_VERSION && host->size >= offsetof(iw8x::plugin_host, log) + sizeof(host->log) &&
			host->log)
		{
			host->log(owner, iw8x::gsc::string_view(e.what()));
		}
		return 0;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" __declspec(dllexport) void IW8X_PluginUnload() noexcept
{
}
