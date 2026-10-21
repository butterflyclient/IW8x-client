# Stock chat example

This example sends messages through MW 1.44's original multiplayer text-chat
server-command path. Clients must have the old text-chat UI enabled, including
`NLPSQMQRTP` (`cl_textChatEnabled`).

Build from the staged project root:

```powershell
xmake build -r mw120rproxy
xmake build -r replay_plugin_chat
```

Commands:

```text
iw8x_say <message>
iw8x_tell <clientnum> <message>
```

Both commands marshal the actual send onto the verified game/server thread.
The plugin uses only the public IW8x API and contains no retail RVAs or engine
pointers.
