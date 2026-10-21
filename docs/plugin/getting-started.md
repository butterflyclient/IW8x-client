# Getting Started

This guide builds the smallest useful native plugin with the C++ SDK.

## 1. Create a plugin directory

At runtime IW8x discovers plugins under the `plugins` directory beside the proxy DLL:

```text
plugins/
└── hello/
    ├── plugin.json
    └── hello.dll
```

The common Replay startup installs the host after its native prerequisites. Gameplay callbacks and queued commands run at the active server frame boundary; frontend scripts do not receive gameplay events.

## 2. Add a manifest

```json
{
  "name": "Hello Plugin",
  "version": "1.0.0",
  "api": 1,
  "entry": "hello.dll"
}
```

See [Plugin Manifest](manifest.md) for validation rules.

## 3. Export `IW8X_PluginLoad`

```cpp
#include "plugin/sdk/gsc.hpp"

extern "C" __declspec(dllexport)
uint32_t IW8X_PluginLoad(const iw8x::plugin_host* host,
                         iw8x::plugin_id owner) noexcept
{
    try
    {
        iw8x::api api(host, owner);

        const auto needed = IW8X_CAP_FUNCTIONS | IW8X_CAP_COMMANDS;
        if ((host->capabilities() & needed) != needed)
            return 0;

        api.function("hello_native", [](iw8x::gsc::context& ctx)
        {
            ctx.return_value("hello from C++");
        });

        api.command("hello_status", [host, owner](const iw8x_string*, uint32_t)
        {
            host->log(owner, iw8x::gsc::string_view("hello plugin is loaded"));
        });

        return 1;
    }
    catch (const std::exception& e)
    {
        if (host && host->version == IW8X_PLUGIN_API_VERSION &&
            host->size >= sizeof(iw8x::plugin_host))
        {
            host->log(owner, iw8x::gsc::string_view(e.what()));
        }
        return 0;
    }
    catch (...)
    {
        return 0;
    }
}
```

Returning zero means plugin initialization failed. The host rolls back registrations and unloads the DLL.

## 4. Optional unload export

```cpp
extern "C" __declspec(dllexport)
void IW8X_PluginUnload() noexcept
{
    // Join any threads created directly by this DLL here.
}
```

IW8x closes callback admission and waits for active host callbacks to drain before calling this function. Host-owned registrations and userdata are destroyed after this callback.

## 5. Inspect loaded plugins

The Replay host registers these commands for the dedicated or hosted game:

```text
iw8x_plugins
iw8x_plugin_unload <plugin-id>
```

`iw8x_plugins` prints the IDs and names of currently loaded native plugins.

## Next steps

- [Register GSC functions and methods](functions-and-methods.md)
- [Schedule safe server-thread work](scheduling-and-async.md)
- [Send GSC notifications](entities-and-notifications.md)
- [Manage native resources with handles](native-handles.md)

