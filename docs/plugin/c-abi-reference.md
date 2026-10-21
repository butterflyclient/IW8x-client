# C ABI Reference

The stable ABI is declared in:

```text
src/plugin/sdk/abi.h
```

Current version:

```cpp
#define IW8X_PLUGIN_API_VERSION 1u
```

The ABI uses Win64 C calling conventions with default 8-byte packing.

## Core types

```cpp
typedef uint32_t iw8x_plugin_id;
typedef uint64_t iw8x_registration;
typedef int32_t  iw8x_native_handle;
typedef uint32_t iw8x_status;
```

Strings are borrowed pointer/count views:

```cpp
typedef struct iw8x_string
{
    const char* data;
    uint32_t size;
} iw8x_string;
```

## Entity reference

```cpp
typedef struct iw8x_entity
{
    uint32_t index;
    uint32_t reserved;
    uint64_t generation;
} iw8x_entity;
```

`reserved` must be zero for plugin-created ABI values.

## Value representation

ABI value kinds:

```text
IW8X_UNDEFINED
IW8X_INTEGER
IW8X_NUMBER
IW8X_BOOLEAN
IW8X_STRING
IW8X_VECTOR
IW8X_ENTITY
IW8X_ARRAY
IW8X_STRUCT
```

`iw8x_value` is 24 bytes in ABI v1. Input views are borrowed for the duration of the host call.

## Capability flags

```text
IW8X_CAP_FUNCTIONS
IW8X_CAP_METHODS
IW8X_CAP_ENTITIES
IW8X_CAP_NOTIFY
IW8X_CAP_FIELDS
IW8X_CAP_GAME_TASKS
IW8X_CAP_ASYNC
IW8X_CAP_COMMANDS
IW8X_CAP_INTERCEPTION
IW8X_CAP_DVARS
IW8X_CAP_PLAYERS
IW8X_CAP_CHAT
```

The current Replay 1.20.4 runtime advertises:

- functions;
- methods;
- entities;
- notify;
- game tasks;
- async;
- commands.
- read-only string dvars.
- copied player identities/names, current entity lookup and player enumeration.
- stock MW multiplayer text-chat send (`broadcast` / `tell`).

It does not currently advertise fields or interception.

## Host table

`iw8x_plugin_host` contains:

| Entry | Purpose |
| --- | --- |
| `capabilities` | Runtime feature bitmask |
| `supported_events` | Emitted lifecycle event bitmask |
| `register_native` | Register global function or method |
| `argument_count` | Number of arguments for current invocation |
| `argument` | Read one invocation argument |
| `return_value` | Publish invocation return value |
| `error` | Mark invocation as failed |
| `entity_valid` | Validate an entity generation |
| `get_field` | Reserved field read path |
| `set_field` | Reserved field write path |
| `notify` | Notify entity or level |
| `subscribe` | Subscribe to supported lifecycle event |
| `schedule` | Schedule server/game-thread callback |
| `async` | Worker callback followed by server completion |
| `cancel_task` | Cancel pending task |
| `create_handle` | Create typed native handle |
| `with_handle` | Borrow typed native resource |
| `release_handle` | Release typed native handle |
| `add_command` | Add operator/console command |
| `log` | Log against plugin owner |
| `callback_error` | Report SDK callback failure |
| `hook_function` | Reserved logical interception API; unsupported in v1 runtime |
| `get_dvar_string` | Copy a string dvar value from the verified game/server-thread scope |
| `player_valid` | Validate a connection handle, including candidates |
| `player_name` | Copy the last sampled display name |
| `player_entity` | Current entity handle or `IW8X_NOT_FOUND` |
| `player_client_num` | Validated client slot |
| `player_xuid` | `IW8X_UNSUPPORTED` until retail XUID storage is proven |
| `players` | Borrow an accepted-player snapshot during the receiver call |
| `broadcast` | Send a server-authored message through the stock all-chat command path |
| `tell` | Send a server-authored stock-chat message to one validated player |
| `call_builtin` | Invoke a stock global or entity method; owned SDK result, borrowed C receiver tree |
| `get_dvar` | Resolve a readable Replay 1.20.4 name and read string/bool/int/float through native accessors |
| `register_script` | Register a stock script export by path and canonical token (lower-level API) |
| `call_script` | Invoke a cached registered export with owned SDK results |
| `register_script_name` | Preferred script registration by readable path and function name |
| `call_script_on` | Invoke the same cached export with a generation-validated entity as GSC self |

Player entries append after `get_dvar_string`; chat entries append after the
player entries. Previous table offsets and ABI
version 1 are preserved. `iw8x_player` is 24 bytes; `iw8x_event` is 88 bytes with
its original 32-byte prefix unchanged. New plugins must size-check host entries
before use; old binary plugins remain compatible. See [Players](players.md).

Generic invocation and typed dvars append at offsets 256 and 264.
`register_script`, `call_script`, `register_script_name`, and `call_script_on` occupy offsets
272, 280, 288, and 296 (x64 host size 304). Capability bits 12 through 15 are
`IW8X_CAP_GSC_CALL`, `IW8X_CAP_RICH_VALUES`, `IW8X_CAP_TYPED_DVARS`, and
`IW8X_CAP_SCRIPT_CALL`. The value layout is unchanged.
See [Stock GSC and Dvars](stock-gsc-and-dvars.md) for callback lifetimes and
the supported rich-value boundaries.

## Registration ownership

On successful registration, host-owned callback userdata remains registered until the feature is removed by scope cleanup or plugin unload. Its destroy callback runs exactly once before the owning DLL is freed.

Script export registrations contain no plugin userdata. Register before
`Scr_EndLoadScripts`; engine handle resolution happens only in that window.
Matching owner/normalized-path/token/return-type requests reuse a registration.
Handles refresh on script reload, and plugin unload removes registrations.
Unknown canonical names or missing exports return `IW8X_NOT_FOUND`; pending
or invalidated exports return `IW8X_STALE` when called.

## Entry points

Required:

```cpp
uint32_t IW8X_PluginLoad(const iw8x_plugin_host*, iw8x_plugin_id);
```

Optional:

```cpp
void IW8X_PluginUnload(void);
```

No allocation ownership or exception may cross the ABI boundary.
