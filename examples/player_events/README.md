# Player events example

Build both the host and example from `src`:

```powershell
xmake build -r mw120rproxy
xmake build -r replay_plugin_player_events
```

Stop the server, then copy `xmake-out/x64/Release/XInput9_1_0.dll` beside
`ModernWarfare.exe`. Copy `xmake-out/examples/player_events`
into the server's `plugins` directory and restart. The example folder contains
`plugin.json` and `player_events_example.dll`. Updating only the example leaves
an older host without the required player API; it rejects plugin loading.

The plugin logs server/map ordering and player messages through the public SDK:

```text
Player connected: slot=3 xuid=unsupported name=Alice
Player spawned: slot=3 entity=3
Player killed: killer=3 victim=5
Player disconnected: slot=3
```

Run `iw8x_player_events` in the operator console to list currently connected
players. It queries the host, including connections established before the
plugin loaded. XUID remains explicitly unsupported until its retail field is
proven. The example uses no engine globals, pointers or RVAs.
