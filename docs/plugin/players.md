# Players

Player identity represents a connection independently of its current entity:

```c
typedef struct iw8x_player {
    uint32_t client_num;
    uint32_t reserved;
    uint64_t generation;
    uint64_t xuid;
} iw8x_player;
```

This is a 24-byte fixed-width POD with 8-byte alignment and no pointers.
`reserved` is zero. Generations are process-wide, monotonic, nonzero and never
reused. Reconnecting in the same slot gets a new generation; respawn, map
re-entry and script reload retain it. Rejection, disconnect and final server
shutdown invalidate it. Invalid/forged/old handles return `IW8X_STALE`.

```cpp
api.subscribe(IW8X_PLAYER_CONNECTED, [api](const iw8x_event& event) {
    const auto player = api.player_from(event.player);
    const auto slot = player.client_num();
    const auto name = player.name();
    api.game_thread([player] {
        if (!player.valid()) return;
        if (const auto entity = player.entity(); entity && entity->valid()) {
            // Entity operations are allowed at this server task boundary.
        }
    });
});
```

| C entry / C++ method | Behavior |
| --- | --- |
| `player_valid` / `valid()` | `OK` for a live candidate/connection, `STALE` otherwise; C++ returns bool. |
| `player_client_num` / `client_num()` | Validated slot; stale handles are rejected. |
| `player_name` / `name()` | Copy sampled from the verified client field at connect/teardown; empty during connecting. Live rename tracking is unsupported. |
| `player_entity` / `entity()` | Current captured entity, or `NOT_FOUND` / `nullopt` until spawn or after entity/map retirement. |
| `player_xuid` / `xuid()` | `UNSUPPORTED` for live handles, `STALE` for old handles. The POD field is zero/unknown, not an authenticated identity. |
| `players` / `api.players()` | Accepted-connection snapshot, excluding candidates. Revalidate if disconnect can race enumeration. |

These operations read copied host data under a mutex and are usable on workers
and console callbacks. Engine operations on the returned entity still require
the verified server/game scope. Entity free clears the association. Staleness
can occur after lookup; validate at point of use.

C name/list receivers borrow the result only during their call. SDK wrappers
copy inside the plugin DLL and contain allocation exceptions there. No STL
object, allocation ownership or raw engine pointer crosses the ABI.

Address, ping, team, alive, XUID storage and team/name/chat events are unproven
and unsupported. No guessed fields or GSC-table aliases stand in for native
lifecycle owners. See [Events and Lifecycle](events-and-lifecycle.md).
