# IW8x Native Plugins

IW8x native plugins let a C or C++ DLL extend the dedicated server without exposing Modern Warfare RVAs or raw GSC VM internals to plugin authors.

The current plugin ABI is **version 1**. Plugins can register GSC functions and methods, send script notifications, subscribe to proven player/map/server/script lifecycle events, inspect safe player handles, schedule work on the server thread, run asynchronous worker jobs, register console commands, and own typed native handles.

The host owns all game-specific mappings and VM integration. Plugins interact only with the public SDK under `src/plugin/sdk`.

Plugins can also call stock GSC functions/methods through `api.gsc()` and read
typed dvars by their readable names through `api.dvars()`. The
[stock-call guide](stock-gsc-and-dvars.md) explains owned collection snapshots,
capabilities, ABI compatibility and the idle-server boundary.

## Quick example

```cpp
#include "plugin/sdk/gsc.hpp"

extern "C" __declspec(dllexport)
uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host,
                         iw8x::plugin_id owner) noexcept
{
    try
    {
        iw8x::api api(host, owner);

        api.function("native_add", [](iw8x::gsc::context& ctx)
        {
            const int a = ctx.argument<int>(0);
            const int b = ctx.argument<int>(1);
            ctx.return_value(a + b);
        });

        return 1;
    }
    catch (...)
    {
        return 0;
    }
}
```

GSC can then call the registered native using the generated native binding IDs used by your script compiler.

## Plugin location

Native plugins are loaded only in dedicated mode. IW8x scans:

```text
<directory containing XInput9_1_0.dll>/plugins/
```

Each plugin lives in its own directory and must contain a `plugin.json` manifest.

```text
plugins/
└── example/
    ├── plugin.json
    └── native_example.dll
```

The loader accepts at most 128 plugin manifests in one discovery pass and loads them in sorted path order.

## What ABI v1 supports

| Feature | Status |
| --- | --- |
| Global GSC functions | Supported |
| Entity/player GSC methods | Supported |
| Entity lifetime validation | Supported |
| GSC notifications | Supported |
| Game-thread tasks | Supported |
| Async worker + server completion | Supported |
| Native handles | Supported |
| Console commands | Supported |
| Read-only string dvar access | Supported on the game/server thread |
| Script lifecycle events | Supported for proven events |
| Player/map/server lifecycle | Supported at verified Replay 1.20.4 boundaries; see [event semantics](events-and-lifecycle.md) |
| Safe player handles | Connection generations, sampled names, entity lookup and enumeration; XUID unsupported |
| Entity field get/set | Reserved in SDK, not enabled by current Replay 1.20.4 runtime |
| Logical GSC function interception | Reserved, returns `IW8X_UNSUPPORTED` |
| Array/struct VM marshalling | Not yet verified |

## Important rule

> Plugins must not hardcode game RVAs or directly access IW8 VM internals. Those mappings belong to IW8x.

Start with [Getting Started](getting-started.md), then see the [C++ SDK](cpp-sdk.md) or [C ABI Reference](c-abi-reference.md).
