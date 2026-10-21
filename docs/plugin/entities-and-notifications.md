# Entities and Notifications

## Entity references

The public entity reference is an opaque validated identity:

```cpp
struct iw8x_entity
{
    uint32_t index;
    uint32_t reserved;
    uint64_t generation;
};
```

Plugins must not reinterpret this as an engine pointer.

Generation tracking prevents an old reference from silently becoming valid for a different entity that later reuses the same entity index.

```cpp
if (player.valid())
{
    // Safe to use at this engine boundary.
}
```

Entity references are invalidated when native entity cleanup runs and across script/map lifecycle resets.

## Notifications

```cpp
player.notify("native_test_complete",
{
    "hello from C++",
    1337
});
```

GSC can wait for the notification normally:

```c
self waittill("native_test_complete", message, number);
```

Notification calls must run inside a valid server/GSC engine boundary. Use `api.game_thread()` when calling from arbitrary plugin code.

At most 256 arguments can be supplied to one notification.

## Level notifications

The C++ SDK provides:

```cpp
api.level().notify("backend_ready", {1});
```

The all-zero entity reference is treated specially **only for notify** and targets `level`.

Do not treat `api.level()` as a normal entity and do not expect `valid()` to report it as an entity.

## Field access

The C++ wrapper exposes:

```cpp
entity.get_field("origin");
entity.set_field("field", value);
entity.origin();
```

However, the current Replay 1.20.4 runtime does **not** advertise `IW8X_CAP_FIELDS` and does not install field accessors. These operations currently return `IW8X_UNSUPPORTED`.

Check `host->capabilities()` before using field access in future API/runtime versions.

