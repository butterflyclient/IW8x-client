# Events and Lifecycle

The Replay 1.20.4 adapter delivers the events below through the existing guarded
plugin subscription registry. Query `supported_events()` before subscribing.

## Supported events in ABI v1 runtime

| Event | Constant | Supported now |
| --- | --- | --- |
| Scripts begin load | `IW8X_SCRIPTS_BEGIN_LOAD` | Yes |
| Scripts loaded | `IW8X_SCRIPTS_LOADED` | Yes |
| Scripts shutdown | `IW8X_SCRIPTS_SHUTDOWN` | Yes |
| Map restart | `IW8X_MAP_RESTART` | Yes |
| Server starting / ready / stopping / stopped | `IW8X_SERVER_STARTING`, `IW8X_SERVER_READY`, `IW8X_SERVER_STOPPING`, `IW8X_SERVER_STOPPED` | Yes |
| Map starting / ready / shutdown | `IW8X_MAP_STARTING`, `IW8X_MAP_READY`, `IW8X_MAP_SHUTDOWN` | Yes |
| Player connecting / connected | `IW8X_PLAYER_CONNECTING`, `IW8X_PLAYER_CONNECTED` | Yes |
| Player disconnecting / disconnected | `IW8X_PLAYER_DISCONNECTING`, `IW8X_PLAYER_DISCONNECTED` | Yes |
| Player spawn / death / kill | `IW8X_PLAYER_SPAWN`, `IW8X_PLAYER_DEATH`, `IW8X_PLAYER_KILL` | Yes |

Use the host's `supported_events()` bitmask rather than assuming an enum member is implemented.

## Reserved event IDs

`GAME_INITIALIZED` remains reserved and unsupported. Team/name/chat events are
unmapped. `MAP_READY`, `SERVER_READY`, `PLAYER_SPAWN`, `PLAYER_DEATH`, and
`PLAYER_KILL` alias `MAP_STARTED`, `SERVER_STARTED`, `PLAYER_SPAWNED`,
`PLAYER_DIED`, and `PLAYER_KILLED`. Existing numeric IDs are unchanged.

## Subscribe from C++

```cpp
const auto supported = host->supported_events();

if (supported & (1ull << IW8X_SCRIPTS_LOADED))
{
    api.subscribe(IW8X_SCRIPTS_LOADED,
        [](const iw8x_event& event)
        {
            // Script events retain the original map-resource epoch.
        });
}
```

Subscriptions are owned by the plugin and are removed automatically on unload.

## Map epoch

Lifecycle state carries an internal epoch used to invalidate map-scoped tasks and handles when the script/map generation ends.

`IW8X_SCRIPTS_BEGIN_LOAD` begins a new active map/script scope. `IW8X_SCRIPTS_SHUTDOWN` ends it and invalidates map-scoped resources.

New lifecycle/player events use a separate lifecycle epoch, advanced before the
native gameplay script-load owner. Player connection identity survives map/script
loads until native disconnect or full server shutdown.

## Event semantics and ordering

An accepted remote connection emits connecting, connected, then its spawn(s).
Connecting has a stable allocated candidate slot before final game validation.
Rejection invalidates it before that call returns; it never emits connected or
enters enumeration. Connected follows the native connect response, after the
last rejection check and the `CS_CONNECTED` state write. Repeated responses
and map re-entry do not create new connections.

Disconnecting runs before DropClient/FreeClient teardown, with readable copied
identity/name. Disconnected runs after original return and identity invalidation;
its player is historical and live access returns `IW8X_STALE`. Nested Drop/Free
emits one pair, completing after the outer drop; direct FreeClient replacement
is covered too. Spawn follows the real MP ClientSpawn owner and preserves the
connection generation across respawns.

Death processing records the engine's resolved attacker at its PlayerKilled
commit call, after early-return checks and turret-owner attribution. After the
original PlayerDie returns, death identifies the victim and optional killer;
kill reverses these fields. Suicide/world/non-player deaths have no kill event.
An entity freed during script processing is omitted.

Gameplay script loading emits server starting, map starting, and scripts begin
load before `GScr_MainMP_LoadScripts` (Replay RVA `0x126ECB0`). Script-loaded
notification follows its original return. Frontend loads/shutdowns are excluded.
Map ready and server ready are signalled after `ServerStart_Finalize` (`0x137E290`)
returns; they are delivered in that order at the next idle gameplay server frame.
The native ServerStart state at `0xE195760` must be zero before commands or tasks
can drain. This excludes initialization frames and pending restart transitions.

A full stop emits server stopping before persistent shutdown, map shutdown and
scripts shutdown before `G_MainMP_ShutdownGame`, then server stopped after native
application server shutdown. Map-only teardown does not imply server stopped.
The map-restart event remains at the pending non-savegame restart boundary before
native processing. Normal restarts use the existing compiled loader's full native
script reload route; savegame processing is preserved.

## Payload and ABI compatibility

The original 32-byte event prefix is unchanged. Primary and secondary 24-byte
`iw8x_player` values, `reason`, and `flags` are appended; total size is 88 bytes.
`entity` belongs to the primary player, with zero generation meaning absent.
Only death/kill set `IW8X_EVENT_HAS_REASON`: `reason` is the native integer
means-of-death argument. Other reasons/flags are zero; no disconnect code is
invented from text. New SDK clients size-check the appended host table. Old
binary clients retain their prefix and function offsets.

## Threading and ownership

Readiness callbacks run at the verified `G_MainMP_RunFrame` tail after the VM is
idle. Other lifecycle events run synchronously on their native main/server
boundary with `game_scope(false)`: copied player reads work; VM/entity/dvar
operations return `IW8X_WRONG_THREAD`. Schedule VM work with `api.game_thread()`.
Worker jobs never gain game-thread authority, and nested scopes cannot lift an
outer restriction. Engine-free scopes may overlap a VM scope without borrowing it.

Dispatch snapshots listeners under the registry lock, releases it, and takes
an owner lease before each callback. Unload closes admission, waits for active
leases, destroys registrations, then frees the DLL. Callback exceptions remain
contained. Reentrant subscriptions affect subsequent/nested dispatch, not the
current snapshot. Callbacks/destructors never execute under the registry lock.

See [Players](players.md), [the example](../../examples/player_events/README.md),
and [Replay hook evidence](../reverse-engineering/gsc-port.md).

