#include "plugin/sdk/gsc.hpp"

#include <string>

extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host, iw8x::plugin_id owner)
{
	try
	{
		iw8x::api api(host, owner);
		const auto needed = IW8X_CAP_GSC_CALL | IW8X_CAP_RICH_VALUES | IW8X_CAP_TYPED_DVARS;
		if ((host->capabilities() & needed) != needed) return 0;
		api.command("gsc_gameplay_status",
			[api](const iw8x_string*, uint32_t)
			{
				api.game_thread(
					[api]
					{
						const auto gametype = api.gsc().call("getdvar", "g_gametype").as<std::string>();
						const auto chat = api.dvars().get_string("cl_textChatEnabled");
						const auto entities = api.gsc().call("getentarray", "player", "classname");
						const auto message =
							"GSC gametype=" + gametype + " chat=" + chat + " player entities=" + std::to_string(entities.as_array().size());
						api.raw().log(api.owner(), iw8x::gsc::string_view(message));
					});
			});
		// Explicit command because giving a weapon changes live gameplay.
		api.command("gsc_gameplay_give",
			[api](const iw8x_string*, uint32_t)
			{
				api.game_thread(
					[api]
					{
						for (const auto& player : api.players())
						{
							if (const auto entity = player.entity()) api.gsc().method(*entity, "giveweapon", "iw8_ar_mike4");
						}
					});
			});
		return 1;
	}
	catch (...)
	{
		return 0;
	}
}
