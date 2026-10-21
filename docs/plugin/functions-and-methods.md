# GSC Functions and Methods

Native registration extends the GSC builtin dispatch tables owned by IW8x.

Plugins do not patch the VM or hardcode builtin IDs themselves.

## Global function

```cpp
api.function("native_add", [](iw8x::gsc::context& ctx)
{
    const int a = ctx.argument<int>(0);
    const int b = ctx.argument<int>(1);
    ctx.return_value(a + b);
});
```

## Entity method

```cpp
api.method("native_test",
    [](iw8x::gsc::context& ctx, iw8x::gsc::entity self)
    {
        if (!self.valid())
            ctx.error("invalid entity");

        ctx.return_value(true);
    });
```

Method receivers are generation-validated before the plugin callback executes.

## Naming rules

Native names are normalized to lowercase and must:

- be 1-127 characters;
- contain only ASCII letters, digits, and `_`;
- not begin with a digit.

Example:

```text
Discord_Send -> discord_send
```

Persistent native binding names must remain uniquely owned by the same plugin and native kind. Reusing a persisted name for another plugin or changing it between function and method is rejected.

## Capacity

IW8x reserves up to 256 additional function slots and 256 additional method slots in the current Replay 1.20.4 dispatch-table extension.

The active native registry also has a 512-entry limit across functions and methods.

## Stable builtin IDs

Registration is backed by a persistent binding database so compiled GSC does not receive a different native ID merely because plugin discovery order changes.

See [Native Binding IDs](native-bindings.md).

## Duplicate registration

A duplicate active registration returns `IW8X_DUPLICATE`.

Registrations are automatically removed when their owning plugin unloads.

