#include "plugin/sdk/gsc.hpp"

#include <string>

namespace
{
	void load_error(const iw8x::plugin_host* host, iw8x::plugin_id owner, std::string_view message) noexcept
	{
		// The original host prefix already has log. Requiring the extended player
		// table here used to hide the SDK's size-mismatch error on an older DLL.
		if (!host || host->version != IW8X_PLUGIN_API_VERSION || host->size < offsetof(iw8x::plugin_host, log) + sizeof(host->log)) return;
		try
		{
			if (host->log) host->log(owner, iw8x::gsc::string_view(message));
		}
		catch (...)
		{
		}
	}

	void log(const iw8x::api& api, const std::string& message)
	{
		api.raw().log(api.owner(), iw8x::gsc::string_view(message));
	}

	std::string xuid_text(const iw8x::player& player)
	{
		try
		{
			return std::to_string(player.xuid());
		}
		catch (const iw8x::api_error& error)
		{
			if (error.code() == IW8X_UNSUPPORTED) return "unsupported";
			throw;
		}
	}

	std::string describe(const iw8x::player& player)
	{
		return "slot=" + std::to_string(player.client_num()) + " xuid=" + xuid_text(player) + " name=" + player.name();
	}
}

extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host, iw8x::plugin_id owner) noexcept
{
	try
	{
		if (host && host->version == IW8X_PLUGIN_API_VERSION && host->size < sizeof(iw8x::plugin_host))
		{
			load_error(host, owner,
				"Player events requires the matching XInput9_1_0.dll with the player API; update the host DLL and restart the server.");
			return 0;
		}
		iw8x::api api(host, owner);
		const auto needed = IW8X_CAP_PLAYERS | IW8X_CAP_COMMANDS;
		if ((host->capabilities() & needed) != needed)
		{
			load_error(host, owner,
				"Player events requires IW8X_CAP_PLAYERS and IW8X_CAP_COMMANDS; check the host DLL and native API installation.");
			return 0;
		}

		const auto supported = host->supported_events();
		const std::pair<uint32_t, const char*> lifecycle[]{ { IW8X_SERVER_STARTING, "SERVER_STARTING" },
			{ IW8X_MAP_STARTING, "MAP_STARTING" }, { IW8X_MAP_READY, "MAP_READY" }, { IW8X_SERVER_READY, "SERVER_READY" },
			{ IW8X_MAP_RESTART, "MAP_RESTART" }, { IW8X_MAP_SHUTDOWN, "MAP_SHUTDOWN" }, { IW8X_SERVER_STOPPING, "SERVER_STOPPING" },
			{ IW8X_SERVER_STOPPED, "SERVER_STOPPED" } };
		for (auto [event, name] : lifecycle)
			if (supported & (1ull << event))
			{
				api.subscribe(event,
					[api, name](const iw8x_event& e)
					{
						log(api, std::string(name) + " epoch=" + std::to_string(e.epoch));
					});
			}
		if (supported & (1ull << IW8X_PLAYER_CONNECTED))
		{
			api.subscribe(IW8X_PLAYER_CONNECTED,
				[api](const iw8x_event& event)
				{
					log(api, "Player connected: " + describe(api.player_from(event.player)));
				});
		}
		if (supported & (1ull << IW8X_PLAYER_SPAWN))
		{
			api.subscribe(IW8X_PLAYER_SPAWN,
				[api](const iw8x_event& event)
				{
					// The event owns a copied identity, never a retail entity pointer.
					const auto entity = event.entity.generation ? std::to_string(event.entity.index) : std::string("none");
					log(api, "Player spawned: slot=" + std::to_string(event.player.client_num) + " entity=" + entity);
				});
		}
		if (supported & (1ull << IW8X_PLAYER_KILL))
		{
			api.subscribe(IW8X_PLAYER_KILL,
				[api](const iw8x_event& event)
				{
					log(api,
						"Player killed: killer=" + std::to_string(event.player.client_num) +
							" victim=" + std::to_string(event.other_player.client_num));
				});
		}
		if (supported & (1ull << IW8X_PLAYER_DISCONNECTED))
		{
			api.subscribe(IW8X_PLAYER_DISCONNECTED,
				[api](const iw8x_event& event)
				{
					// Teardown already invalidated this handle. Read its historical
					// slot directly; live player accessors now return IW8X_STALE.
					log(api, "Player disconnected: slot=" + std::to_string(event.player.client_num));
				});
		}

		api.command("iw8x_player_events",
			[api](const iw8x_string*, uint32_t)
			{
				uint32_t count{};
				for (const auto& player : api.players())
				{
					try
					{
						log(api, "Connected player: " + describe(player));
						++count;
					}
					catch (const iw8x::api_error& error)
					{
						// A disconnect may race enumeration on the console thread.
						if (error.code() != IW8X_STALE) throw;
					}
				}
				log(api, "Connected players: " + std::to_string(count));
			});
		return 1;
	}
	catch (const std::exception& error)
	{
		load_error(host, owner, error.what());
		return 0;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" __declspec(dllexport) void IW8X_PluginUnload() noexcept
{
	// All state is in host-owned callbacks; unload drains and destroys them.
}
