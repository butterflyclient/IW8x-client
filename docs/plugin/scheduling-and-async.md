# Scheduling and Async Work

Do not call GSC/entity APIs from arbitrary plugin-created threads.

The host serializes live GSC access through verified engine boundaries and fails closed on competing engine threads.

## Run on the game/server thread

```cpp
api.game_thread([player]
{
    if (player.valid())
        player.notify("native_ready");
});
```

Optional delay:

```cpp
api.game_thread([]
{
    // Runs on a later server pump.
}, 500);
```

The delay is in milliseconds.

## Scope

Two scopes are available:

```cpp
IW8X_SCOPE_PLUGIN
IW8X_SCOPE_MAP
```

The C++ SDK defaults scheduled and async work to `IW8X_SCOPE_MAP`.

Map-scoped tasks are canceled when the active script/map scope ends.

Plugin-scoped tasks survive map transitions but are still removed when the plugin unloads.

## Async worker job

```cpp
api.worker(
    []
    {
        return expensive_database_query();
    },
    [player](auto result)
    {
        if (player.valid())
            player.notify("database_result", {result});
    });
```

The work callback runs on the host worker pump. The completion callback is marshalled back to the server/game pump.

Do not perform live GSC operations in the worker callback.

## Canceling raw ABI tasks

The raw ABI returns an `iw8x_registration` token from `schedule` and `async`.

`cancel_task` can cancel only a pending task. A currently running task returns `IW8X_BUSY`.

## Unload behavior

Plugin unloading stops new callback admission and drains active work before unloading the DLL. Pending owned tasks are removed as part of plugin cleanup.

