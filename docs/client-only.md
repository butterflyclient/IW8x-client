# IW8x client DLL

Build from the project directory:

```powershell
xmake build iw8x_client
```

The x64 Release output is `xmake-out/x64/Release/client/XInput9_1_0.dll`.
The target uses a static C/C++ runtime and Windows system libraries. Its only
project library dependency is the static Discord RPC client.

## Runtime data

The mod root beside the DLL is `IW8x`. Compiled scripts go in `IW8x/.gsc`, Lua
overrides in `IW8x/.lua`, and configuration update metadata in `IW8x/cache`.
Plugins retain their existing `plugins` directory and API. The loadout script
and Replay bytecode conversion remain included.

## Configuration synchronization

Before online hooks read `mw120rproxy.online.ini`, the startup worker checks:

https://raw.githubusercontent.com/choppedshyt/iw8x-dll/refs/heads/main/mw120rproxy.online.ini

The updater uses HTTPS certificate validation and a conditional ETag request.
A SHA256 fingerprint also identifies unchanged content if the ETag changes or
is unavailable. Local edits remain intact while the remote version is unchanged.
A changed configuration is size checked, validated, written to a temporary file
beside the INI, flushed, and atomically renamed over the destination. Failed
downloads and failed replacements leave the existing INI intact. Interrupted
temporary downloads are never loaded as configuration. Update metadata is also
replaced atomically, and malformed metadata falls back to a full content check.

## Compilation boundaries

The client target defines `IW8X_CLIENT_ONLY` and excludes all dedicated-server,
headless, server-bot, collision-port and backend executable sources, the backend
agent, and hosted profile/settings/progression account stores. Hosted HTTP/UDP
matchmaking handlers and social, chat, tournament, publisher, playlist, store and
custom-mod services in mixed modules are excluded by compilation guards.

HTTP client adapters remain available for connecting to the configured backend.
Native gameplay, local matches, GSC, plugins, authentication, profiles,
progression display, frontend, matchmaking, parties, chat, voice and Discord
remain included. The existing custom-content ports remain disabled by default.
The client DLL rejects dedicated-server launch mode.

The full `mw120rproxy` and standalone `magma_backend120` targets retain their
service implementations. Shared JSON journal persistence lives in
`src/common/json_file.inl` and is used by both client journals and server stores.

## Validation

The Release client, full DLL and standalone backend targets built successfully. The client object
audit found no dedicated/backend source objects, hosted HTTP listeners or
request handlers, backend agent symbols, or server account store symbols. Its
DLL imports are Windows system libraries. The client DLL is installed in the game
directory and `IW8x/updates`; both installed copies match the built SHA256.
The existing mod data was backed up and renamed to `IW8x`, with script hashes
preserved and the existing online INI unchanged during installation. New client
runtime behavior requires a fresh manual launch; no gameplay claim follows from
a build or file installation.
