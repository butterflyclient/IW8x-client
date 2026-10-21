#include "plugin/sdk/gsc.hpp"

#include <mutex>

namespace
{
	void log(const iw8x::api& api, const std::string& message)
	{
		api.raw().log(api.owner(), iw8x::gsc::string_view(message));
	}

	struct map_state
	{
		std::mutex mutex;
		std::optional<iw8x::weapon> weapon;
		uint64_t generation{};
	};

	std::optional<iw8x::weapon> stock_weapon(const iw8x::api& api, const iw8x::gsc::script_function& build,
		const iw8x::gsc::script_function& night, const std::shared_ptr<map_state>& state, const char*& stage)
	{
		uint64_t generation;
		{
			std::lock_guard lock(state->mutex);
			if (state->weapon) return state->weapon;
			generation = state->generation;
		}
		// Engine calls stay on the game thread, outside the cache mutex. The root
		// selects stock default attachments; _mp would skip that lookup.
		stage = "isnightmap";
		const auto is_night = night().as<bool>();
		stage = "buildweapon";
		const auto weapon =
			build("iw8_sn_alpha50", iw8x::array{}, "none", "none", -1, iw8x::value{}, iw8x::value{}, iw8x::value{}, is_night)
				.as<iw8x::weapon>();
		stage = "validate stock weapon";
		const auto basename = api.gsc().call("getweaponbasename", weapon).as<std::string>();
		const auto attachments = api.gsc().call("getweaponattachments", weapon).as_array();
		if (basename != "iw8_sn_alpha50_mp" || attachments.empty())
		{
			log(api,
				"Snipers Only build rejected: basename=" + basename + " attachments=" + std::to_string(attachments.size()) +
					"; keeping current weapon");
			return std::nullopt;
		}
		std::lock_guard lock(state->mutex);
		if (state->generation != generation) return std::nullopt;
		state->weapon = weapon;
		return weapon;
	}
}

extern "C" __declspec(dllexport) uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host, iw8x::plugin_id owner) noexcept
{
	try
	{
		iw8x::api api(host, owner);

		const auto build_weapon = api.gsc().script("scripts/mp/class", "buildweapon", iw8x::gsc::value_type::weapon_);

		const auto is_night_map = api.gsc().script("scripts/cp_mp/utility/game_utility", "isnightmap", iw8x::gsc::value_type::boolean);

		const auto take_weapon = api.gsc().script("scripts/cp_mp/utility/inventory_utility", "_takeweapon");

		const auto give_weapon = api.gsc().script("scripts/cp_mp/utility/inventory_utility", "_giveweapon");

		const auto switch_weapon = api.gsc().script("scripts/cp_mp/utility/inventory_utility", "_switchtoweaponimmediate");

		const auto state = std::make_shared<map_state>();
		const auto clear_cache = [state](const iw8x_event&)
		{
			std::lock_guard lock(state->mutex);
			state->weapon.reset();
			++state->generation;
		};
		api.subscribe(IW8X_SCRIPTS_BEGIN_LOAD, clear_cache);
		api.subscribe(IW8X_SCRIPTS_SHUTDOWN, clear_cache);
		api.subscribe(IW8X_MAP_SHUTDOWN, clear_cache);

		//
		// Welcome message
		//
		// PLAYER_CONNECTED can occur outside the VM-safe game scope, so queue
		// the actual chat work. Waiting 4.5 seconds also gives the client time
		// to finish entering the game.
		//
		api.subscribe(IW8X_PLAYER_CONNECTED,
			[api](const iw8x_event& event)
			{
				if (!event.player.generation) return;

				try
				{
					api.game_thread(
						[api, player_ref = event.player]
						{
							try
							{
								const auto player = api.player_from(player_ref);

								api.broadcast("^2Welcome ^7" + player.name() + "^2 to Snipers Only! ^7Good luck and have fun.");
							}
							catch (const iw8x::api_error& e)
							{
								log(api, "Snipers Only welcome status=" + std::to_string(e.code()));
							}
						},
						4500, IW8X_SCOPE_MAP);
				}
				catch (const iw8x::api_error& e)
				{
					log(api, "Snipers Only queue welcome status=" + std::to_string(e.code()));
				}
			});

		//
		// Kill message
		//
		// PLAYER_KILL is published from a non-VM-safe death callback.
		// Do not call broadcast() directly from it. Carry the copied player
		// identities into the next safe server RunFrame instead.
		//
		api.subscribe(IW8X_PLAYER_KILL,
			[api](const iw8x_event& event)
			{
				if (!event.player.generation || !event.other_player.generation)
				{
					return;
				}

				try
				{
					api.game_thread(
						[api, killer_ref = event.player, victim_ref = event.other_player]
						{
							try
							{
								const auto killer = api.player_from(killer_ref);
								const auto victim = api.player_from(victim_ref);

								api.broadcast("^3" + killer.name() + " ^7sniped ^1" + victim.name() + "^7!");
							}
							catch (const iw8x::api_error& e)
							{
								log(api, "Snipers Only kill message status=" + std::to_string(e.code()));
							}
						},
						0, IW8X_SCOPE_MAP);
				}
				catch (const iw8x::api_error& e)
				{
					log(api, "Snipers Only queue kill message status=" + std::to_string(e.code()));
				}
			});

		//
		// Sniper loadout
		//
		api.subscribe(IW8X_PLAYER_SPAWN,
			[api, build_weapon, is_night_map, take_weapon, give_weapon, switch_weapon, state](const iw8x_event& event)
			{
				if (!event.player.generation) return;

				// Carry the stable player identity across deferred work.
				// The entity generation captured by the spawn event can be
				// retired/rebound by stock spawn processing before the next
				// game-thread pump.
				try
				{
					api.game_thread(
						[api, build_weapon, is_night_map, take_weapon, give_weapon, switch_weapon, state, player_ref = event.player]
						{
							const char* stage = "player lookup";

							try
							{
								const auto player = api.player_from(player_ref);

								stage = "player.entity";

								const auto entity = player.entity();

								if (!entity || !entity->valid())
								{
									log(api, "Snipers Only missing/stale entity before equip");
									return;
								}

								stage = "getcurrentweapon";

								const auto current = api.gsc().method(*entity, "getcurrentweapon");

								const auto built = stock_weapon(api, build_weapon, is_night_map, state, stage);
								if (!built) return;
								const auto weapon = *built;

								stage = "_takeweapon";

								take_weapon.on(*entity, current);

								//
								// Equivalent to the script's wait 0.05.
								// Do not block the game thread.
								//
								stage = "queue delayed give";

								api.game_thread(
									[api, give_weapon, switch_weapon, player_ref, weapon]
									{
										const char* delayed_stage = "delayed player lookup";

										try
										{
											const auto player = api.player_from(player_ref);

											delayed_stage = "delayed player.entity";

											const auto entity = player.entity();

											if (!entity || !entity->valid())
											{
												return;
											}

											delayed_stage = "_giveweapon";

											give_weapon.on(*entity, weapon);

											delayed_stage = "_switchtoweaponimmediate";

											switch_weapon.on(*entity, weapon);

											//
											// refillweaponammo is local to
											// script.gsc, not a stock export.
											//
											delayed_stage = "setweaponammostock";

											api.gsc().method(*entity, "setweaponammostock", weapon, 999);

											delayed_stage = "setweaponammoclip";

											api.gsc().method(*entity, "setweaponammoclip", weapon, 999);
										}
										catch (const iw8x::api_error& e)
										{
											log(api,
												"Snipers Only stage=" + std::string(delayed_stage) + " status=" + std::to_string(e.code()));
										}
									},
									50, IW8X_SCOPE_MAP);
							}
							catch (const iw8x::api_error& e)
							{
								log(api, "Snipers Only stage=" + std::string(stage) + " status=" + std::to_string(e.code()));
							}
						});
				}
				catch (const iw8x::api_error& e)
				{
					log(api, "Snipers Only stage=queue spawn equip status=" + std::to_string(e.code()));
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
