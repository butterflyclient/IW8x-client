# Building Plugins

The public SDK headers live in:

```text
src/plugin/sdk/
├── abi.h
├── gsc.hpp
└── value.hpp
```

## C++20 example

The repository contains a working example at:

```text
examples/native/plugin.cpp
examples/native/plugin.json
```

The Replay xmake target is `replay_plugin_native`.

From the `restruct-120/project` directory:

```powershell
xmake build -y replay_plugin_native
```

For a Release configuration it outputs the example into:

```text
xmake-out/examples/native/
```

The build target also copies `examples/native/plugin.json` beside the DLL.

## Player lifecycle example

From `restruct-120/project`, run `xmake build -y replay_plugin_player_events`. The DLL and manifest are
written to `xmake-out/examples/player_events/`; copy that directory
under the server's `plugins/` directory. The example subscribes to lifecycle
events and provides the `iw8x_player_events` console command using only the
public SDK. See [its README](../../examples/player_events/README.md).

`xmake build -y replay_plugins` builds all eight existing SDK examples against
the Replay headers. They stay in this isolated output tree until you install
them yourself. The host build is `xmake build -y mw120rproxy` and produces
`xmake-out/x64/Release/XInput9_1_0.dll`.

## Runtime library boundary

The stable cross-DLL ABI in `abi.h` uses plain C-compatible structs, integer handles, pointer/count string views, and function pointers.

Do not pass STL objects, C++ exceptions, owning C++ pointers, or compiler-specific class layouts across the raw ABI boundary.

The C++ SDK in `gsc.hpp` and `value.hpp` wraps that ABI for convenience while keeping plugin-owned C++ state allocated and destroyed inside the plugin DLL.

## Required exports

```cpp
extern "C" __declspec(dllexport)
uint32_t IW8X_PluginLoad(const iw8x_plugin_host*, iw8x_plugin_id) noexcept;
```

Optional:

```cpp
extern "C" __declspec(dllexport)
void IW8X_PluginUnload() noexcept;
```

No exception may cross either export boundary.

## Capability checks

Check capabilities before depending on an optional host feature:

```cpp
const auto needed = IW8X_CAP_FUNCTIONS |
                    IW8X_CAP_METHODS |
                    IW8X_CAP_NOTIFY;

if ((host->capabilities() & needed) != needed)
    return 0;
```

See [C ABI Reference](c-abi-reference.md) for capability flags.

