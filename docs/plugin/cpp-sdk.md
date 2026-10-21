# C++ SDK

Include:

```cpp
#include "plugin/sdk/gsc.hpp"
```

The main wrapper is `iw8x::api`:

```cpp
iw8x::api api(host, owner);
```

Construction validates the ABI version and `iw8x_plugin_host` structure size. A mismatch throws `iw8x::api_error` with `IW8X_VERSION_MISMATCH`.

`api.gsc().call()` / `.method()` invoke stock Replay 1.20.4 builtins with owned results.
`api.gsc().script(path, name, return_type)` registers a stock script export by
readable name; `api.gsc().call_script<T>(path, name, args...)` invokes a registered
export with an owned typed result. Register exports at plugin load or
`IW8X_SCRIPTS_BEGIN_LOAD`, before the engine's export lookup window closes.
Use `script_function.on(entity, args...)` to call the export with that entity as GSC `self`.
`api.dvars().get_string/get_bool/get_int/get_float()` accepts readable dvar names.
These appended accessors size-check their host entries; see
[Stock GSC and Dvars](stock-gsc-and-dvars.md) for examples and thread/value limits.

## Main helpers

```cpp
api.function("name", callback);
api.method("name", callback);
api.namespace_("prefix");
api.subscribe(event, callback);
api.game_thread(callback, delay_ms, scope);
api.worker(work, complete, scope);
api.command("name", callback);
api.dvar_string("ui_mapname");
api.level();
api.player_from(event.player);
api.players();
api.broadcast("Server message");
api.tell(player, "Private server message");
```

`iw8x::player` provides validated `valid()`, `client_num()`, `name()` and
`entity()` access. XUID lookup reports `IW8X_UNSUPPORTED`; a zero POD XUID is
unknown. See [Players](players.md) for connection generations, enumeration and
the distinction between an entity snapshot and game-thread entity operations.

## Read-only dvar strings

ABI v1 includes an append-only read-only dvar capability. Calls must run from
the verified game/server-thread scope, such as inside a `game_thread()` task:

```cpp
api.game_thread([api]
{
    const auto map = api.dvar_string("ui_mapname");
});
```

Plugins still do not receive a dvar pointer or game RVA.

## Stock text chat

`IW8X_CAP_CHAT` exposes the original MW multiplayer text-chat transport rather
than a custom overlay. Sending must occur from verified game/server scope, so an
operator command or worker should marshal the call through `game_thread()`:

```cpp
api.game_thread([api]
{
    api.broadcast("Welcome to the server");
});
```

Private messages take a generation-validated `iw8x::player`:

```cpp
api.game_thread([api, player]
{
    api.tell(player, "This message is only for you");
});
```

The host bounds and copies the message and rejects command-breaking control
characters and quotes. Clients must have the stock text-chat UI enabled (for
example `NLPSQMQRTP`, the retail `cl_textChatEnabled` dvar).

## Namespaces

`namespace_()` creates a prefixed API view:

```cpp
auto discord = api.namespace_("discord");

discord.function("send_message", [](iw8x::gsc::context& ctx)
{
    // Registers discord_send_message
});
```

The prefix is flattened with `_`. It is not a native GSC namespace feature.

## Errors

Raw ABI status codes are converted to `iw8x::api_error` by `iw8x::check()`.

SDK callback adapters catch C++ exceptions before they can cross the ABI boundary. Callback failures are reported to the host.

After repeated callback failures the host can stop admitting new callbacks for that plugin. Plugin code should treat exceptions as exceptional, not normal control flow.

## Owner identity

Every registration is associated with the `plugin_id` supplied to `IW8X_PluginLoad`.

Do not cache or invent plugin IDs. Use the owner passed by the loader.
