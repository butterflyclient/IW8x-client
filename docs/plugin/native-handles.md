# Native Handles

Native handles let GSC-facing plugin code refer to plugin-owned C++ resources without exposing raw pointers.

The raw ABI type is:

```cpp
typedef int32_t iw8x_native_handle;
```

Handles are positive 31-bit values containing a slot and generation.

## Create a handle

At the raw ABI level:

```cpp
iw8x_native_handle handle{};

const auto status = host->create_handle(
    owner,
    resource_type,
    IW8X_SCOPE_MAP,
    resource_ptr,
    destroy_callback,
    &handle);
```

`resource_type` is a plugin-defined nonzero 64-bit type identifier.

The host associates the handle with:

- the owning plugin;
- the resource type;
- a generation;
- plugin or map scope;
- the current map epoch for map-scoped resources.

## Borrow a resource

```cpp
host->with_handle(owner, handle, resource_type,
    [](void* user, void* resource)
    {
        // resource is borrowed only during this callback.
    },
    nullptr);
```

The pointer is valid only for the duration of that callback.

Borrows are exclusive. A second concurrent borrow returns `IW8X_BUSY`.

## Release

```cpp
host->release_handle(owner, handle, resource_type);
```

Releasing a currently borrowed handle returns `IW8X_BUSY`.

Wrong owner, wrong type, an old generation, or an expired map scope returns `IW8X_STALE`.

## Stale-handle protection

When a slot is reused its generation changes. Old handle integers therefore cannot alias the newly-created object.

Slots are permanently retired instead of allowing generation wraparound to make a stale handle valid again.

## Automatic cleanup

Map-scoped handles expire on script/map shutdown.

All remaining handles owned by a plugin are destroyed during plugin unload.

