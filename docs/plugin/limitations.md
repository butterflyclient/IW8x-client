# Current Limitations

ABI v1 deliberately exposes only behavior that has a verified Replay 1.20.4 implementation.

## Field access is not enabled

`get_field` and `set_field` exist in the ABI and C++ wrapper, but the current runtime does not advertise `IW8X_CAP_FIELDS` and does not bind those operations.

Calls currently return `IW8X_UNSUPPORTED`.

## Logical script interception is reserved

`hook_function` exists so a future ABI can add logical GSC function interception without redesigning the host table.

The current implementation always returns `IW8X_UNSUPPORTED` after validating basic input/owner state.

No arbitrary native detour API is exposed to plugins.

## Rich values and stock calls have explicit boundaries

The generic stock caller supports owned dense arrays and structs, including
nested values. Struct keys use canonical IDs; arbitrary readable field-name
resolution remains unsupported. Sparse/associative collections and cycles are
rejected. Registered native callback returns and notify payloads still reject
rich collections. Generic calls require an idle VM inside the verified server
scope; nested/active calls return `IW8X_BUSY`.

See [Stock GSC and Dvars](stock-gsc-and-dvars.md) for the supported types,
ownership rules, limits, name resolution and typed dvar API.

## Limited lifecycle events

Verified player/map/server/script boundaries are listed in
[Events and Lifecycle](events-and-lifecycle.md). `GAME_INITIALIZED` remains
reserved. Team/name/**incoming chat** events, live rename tracking, XUID, address, ping,
team and alive accessors remain unsupported; see [Players](players.md).

The separate `IW8X_CAP_CHAT` capability supports **sending** server-authored
messages through the stock MW text-chat transport. It does not yet expose an
incoming `PLAYER_CHAT` event, moderation interception, or team-chat send API.

Always query `supported_events()`.

## No direct engine ABI

The plugin SDK intentionally does not expose:

- game RVAs;
- raw `ScriptContext*`;
- game entity pointers;
- direct retail function pointers;
- DB internal pointers.

That boundary is intentional. Game mappings remain an IW8x implementation detail.
