# Plugin Manifest

Every native plugin directory must contain a `plugin.json` file.

## Schema

ABI v1 accepts exactly four top-level keys:

```json
{
  "name": "Native Example",
  "version": "1.0.0",
  "api": 1,
  "entry": "native_example.dll"
}
```

| Field | Rules |
| --- | --- |
| `name` | Required string, 1-128 bytes |
| `version` | Required string, 1-64 bytes |
| `api` | Required unsigned integer, must equal `1` |
| `entry` | Required DLL filename, 1-255 bytes |

The manifest is limited to 64 KiB.

## Entry path restrictions

`entry` must be a filename in the same plugin directory. It cannot contain `/`, `\\`, or `:` and must end in `.dll`.

The loader canonicalizes both the plugin directory and DLL path and rejects an entry that resolves outside the plugin directory.

This is valid:

```json
"entry": "example.dll"
```

These are rejected:

```json
"entry": "../example.dll"
"entry": "bin/example.dll"
"entry": "C:\\example.dll"
```

## API version

The current manifest API is:

```cpp
#define IW8X_PLUGIN_API_VERSION 1u
```

Plugins built for another API version are rejected before their DLL is loaded.

## Loading behavior

The DLL is loaded with a restricted dependency search using its own directory plus the normal default DLL directories. The loader does not use the process current working directory as a plugin dependency search path.

`IW8X_PluginLoad` is mandatory. `IW8X_PluginUnload` is optional.

