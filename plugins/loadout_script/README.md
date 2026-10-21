# Loadout script startup

Build with `xmake build loadout_script`. Install the generated `loadout_script.dll`
and `plugin.json` together in the game's `plugins/loadout_script/` directory.
The compiled script belongs at `IW8x/.gsc/1816.gscbin`.

The compiled loader replaces an asset when the engine loads it; replacement alone
does not execute its entry point. This plugin registers `1816::main` before the
existing native `Scr_EndLoadScripts` callback releases export lookup data. It
executes that entry once at `MAP_READY`, on the idle gameplay server thread.
The script's initialization guard handles a preceding native call to `main`.
Its `main` must return integer `1` after initialization; an empty stock entry or
failure cannot produce the plugin's initialization-confirmed message.
Already connected players are handled by the script's startup path.

Startup and failures are logged as `[Plugin:LoadoutScript]`. Script diagnostics
use `[LoadoutScript]`. Hold Aim + Reload after spawning to open the loadout UI.
The plugin runs in a local hosted match or on the dedicated server that owns
the match; a remote client's DLL cannot execute server GSC for that server.

The build succeeds; in-game execution and UI behavior still need manual validation.
