# Snipers Only

Minimal MW 1.44 plugin. The first spawn in a map builds and caches an AX-50 with
`scripts/mp/class::buildweapon` using the root `iw8_sn_alpha50` (no custom
attachment overrides, no camo/reticle, variant -1,
three undefined arguments, and `game_utility::isnightmap()`). It calls the player's
`inventory_utility::_takeweapon` for the current weapon, waits 50 ms, then calls
`_giveweapon` and `_switchtoweaponimmediate` with the new Weapon. It refills ammo
using `setweaponammostock` and `setweaponammoclip` with 999, as in the local
`refillweaponammo` helper. An AX-50 needs only its single clip filled; it does not
need the helper's extra left/right hand calls or a redundant reserve refill.

Stock GSC looks up the default attachment map by root and adds the receiver,
front/back pieces, magazine, and scope itself. Passing `iw8_sn_alpha50_mp`
instead misses that lookup. The plugin checks the returned asset and attachment
list before taking the old weapon, so an empty/invalid build preserves it.
Successful construction and these checks run once per map. Script reload and
map/script shutdown clear the owned Weapon snapshot. Subsequent spawns use six
GSC calls instead of thirteen, with no weapon reconstruction or attachment queries.

Script exports are registered at plugin load and resolved by the host during
`Scr_EndLoadScripts`. Inventory helpers use `script_function::on(player, ...)`
to bind GSC `self`. The delayed task is map-scoped and checks the entity generation.
Welcome chat waits 4.5 seconds in the scheduler, and kill chat runs on the next
safe game pump. These are one-shot tasks; no sleeps or repeating watchdog run on
the game thread. Player accessors already validate connection generations.

Build from the staged project root with:

```powershell
xmake build replay_plugin_snipers_only
```
