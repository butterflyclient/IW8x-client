# Values and Call Context

## `iw8x::gsc::value`

The C++ SDK represents values with an owning `iw8x::gsc::value`.

```cpp
enum class value_type : uint32_t
{
    undefined,
    integer,
    number,
    boolean,
    string,
    vector,
    entity,
    array,
    struct_,
    weapon_ = IW8X_WEAPON_SNAPSHOT
};
```

The C++ object owns strings, arrays, and objects. `encoded_value` produces a temporary ABI view that borrows from the owning value.

## Live Replay 1.20.4 VM support

The generic stock caller marshals owned dense arrays and canonical-ID-keyed
structs. See [Stock GSC and Dvars](stock-gsc-and-dvars.md) for collection limits
and the exact supported paths.

Live native calls currently support:

- undefined
- 32-bit integer
- float
- boolean
- string
- vector
- entity
- dense array (generic invocation and native callback input)
- struct with canonical field IDs (generic invocation and native callback input)
- owned Weapon snapshots, including null weapons and alternate mode

Registered native callback returns and notification arguments still reject
arrays/structs because those paths lack a separate protected interpreter frame.

## Strings

When a value is pushed back into the live GSC VM, strings must:

- contain no embedded NUL bytes;
- be at most 65,535 bytes.

The ABI helper also rejects obviously invalid borrowed string views.

## Numbers

`number` is a 32-bit floating-point value.

Non-finite floats and vector components are rejected by the C++ value wrapper.

## Booleans

Retail GSC represents booleans as integer `0` or `1` in the current adapter. `context::argument<bool>()` accepts those values and rejects other integers instead of treating every nonzero integer as true.

## `gsc::context`

Native callbacks receive a typed context:

```cpp
api.function("example", [](iw8x::gsc::context& ctx)
{
    const auto count = ctx.argument_count();
    const int id = ctx.argument<int>(0);
    const float scale = ctx.argument<float>(1);
    const std::string_view name = ctx.argument<std::string_view>(2);

    ctx.return_value(id);
});
```

Arguments are copied into plugin-owned `value` instances and cached by argument index for the lifetime of the context.

## Returning values

```cpp
ctx.return_value(123);
ctx.return_value(1.5f);
ctx.return_value("text");
ctx.return_undefined();
```

A call may publish only one successful return value.

`ctx.return_value()` still returns `IW8X_UNSUPPORTED` for arrays/structs.
Collection results from `api.gsc().call()` and `.method()` are supported.

## Script errors

```cpp
if (ctx.argument_count() < 1)
    ctx.error("expected an argument");
```

`ctx.error()` marks the current invocation as `IW8X_CALLBACK_ERROR` and throws `api_error` inside the plugin-side adapter so normal plugin callback code stops immediately.

## Limits

Native calls accept at most 256 arguments.

