# Ownership and Unloading

Every registration created through the host belongs to the plugin ID passed into `IW8X_PluginLoad`.

This ownership model is what makes DLL unloading safe.

## Owned resources

The host tracks plugin ownership for:

- native GSC functions and methods;
- event listeners;
- scheduled and async tasks;
- native handles;
- console commands;
- callback userdata and destroy callbacks.

## Unload sequence

When a plugin is unloaded, the host:

1. Stops admitting new callbacks for that owner.
2. Waits for active host callbacks to drain.
3. Removes native registrations, listeners, pending tasks, command bridges, and handles from host registries.
4. Calls optional `IW8X_PluginUnload` while the DLL is still mapped.
5. Runs host-owned userdata/resource destroy callbacks.
6. Removes the plugin owner.
7. Frees the DLL.

If cleanup callbacks fail, IW8x leaves the DLL mapped and disabled instead of taking an unsafe best-effort `FreeLibrary` path.

## Do not unload from a callback

The host refuses owner unload while already inside a plugin callback and returns `IW8X_BUSY`.

Use the operator command from outside plugin dispatch:

```text
iw8x_plugin_unload <plugin-id>
```

## Plugin-created threads

If a plugin creates its own threads outside the host async API, the plugin is responsible for stopping and joining them in `IW8X_PluginUnload` before code in the DLL can be unmapped.

Prefer the host scheduler/worker API whenever possible.

## Callback failures

The host tracks callback faults. Once a plugin reaches repeated failures, it stops accepting new callback leases.

Current implementation disables callback admission at three recorded faults.

