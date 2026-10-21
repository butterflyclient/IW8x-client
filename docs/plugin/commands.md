# Commands

Plugins can register operator/console commands through the existing IW8x command system.

## C++ SDK

```cpp
api.command("native_example_status",
    [host, owner](const iw8x_string* args, uint32_t count)
    {
        host->log(owner,
            iw8x::gsc::string_view("example plugin is loaded"));
    });
```

The callback receives the command argument array exactly as exposed by the command bridge.

## Naming

Command names use the same validated name rules as other plugin registrations:

- ASCII letters, digits, `_`;
- normalized to lowercase;
- cannot begin with a digit;
- maximum 127 characters.

## Duplicates

If either another native plugin or an existing host command already owns the name, registration returns `IW8X_DUPLICATE`.

## Lifetime

Commands are plugin-owned. The host removes the command bridge before the DLL is freed.

The server itself also exposes:

```text
iw8x_plugins
iw8x_plugin_unload <plugin-id>
```

These two commands are owned by the native plugin component, not by third-party plugins.


Console producers copy commands into a bounded queue; callbacks run at the idle
gameplay server frame boundary. At most 32 commands run per pump, with a backlog
limit of 256 and a 1023-byte input limit. Rejected inputs report a console warning.
Nested commands wait until the next pump. Unload/re-registration retires queued
input associated with the old registration. Unknown commands retain the native
Cbuf route.
