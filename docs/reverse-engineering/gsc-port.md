> Historical port report. Binary evidence and its audit scripts remain in the original `prod-120/ida-reports/gsc_port/` tree; they are not staged build inputs.

# Replay 1.20.4 GSC and plugin port

The MW 1.44 GSC/plugin implementation is integrated into `prod-120`, including
native VM adapters, discovery, SDK, compiled scripts, and gameplay lifecycle.
The actual Replay host and all eight SDK examples build. Validation uses offline
fixtures. **No game was launched, attached or tested, and no binaries were
deployed.** Live behavior still needs manual acceptance.

## Coverage and preservation

`inventory.json` retains the original source SHA-256 baseline for all 81 files
under `prod-144/src/gsc`, `src/plugin`, and `src/dedicated/plugins/gsc`.
`coverage.json` maps every file to the Replay implementation, including relocated
tests and version-specific headers. All source files are unchanged.
`audit_port.py` verifies the original hashes, complete source file set, 69 native
mappings, 33 context/dvar fields, and all 2,472 named builtin IDs against the
2,483 captured callback addresses, plus every Replay dvar mapping against its
reference. File coverage alone is not runtime proof.

Existing `engine/game.h` declarations were moved unchanged (after newline
normalization) into `engine/addresses.hpp`; `game.h.before` is the baseline.
`game.h` forwards to that unified header. Added GSC RVAs, fields, builtin
addresses and hook entry bytes also live there. `gsc/internal/replay.hpp` only
provides aliases. Native structs have compile-time size/offset assertions.

| Source responsibility | Replay implementation |
| --- | --- |
| Registrations, ownership, values, events, tasks, scripts and handles | `src/gsc/*.cpp`, existing SDK ABI |
| VM, tables, entities, players, chat and typed dvars | `src/gsc/internal/` |
| Version-specific mappings | `src/engine/addresses.hpp` |
| Compiled loading/overrides/reset/release and diagnostics | `src/gsc/compiled/` |
| Discovery, manifests, unloading, SDK and docs | `src/plugin/` |
| Source native-plugin component/bootstrap | `src/bootstrap/native_plugins.cpp`, `src/bootstrap/dllmain.cpp` |
| Command ingress/game-thread delivery | Command bridge, console producers, GSC game pump |
| Source scheduler-component tests | Replay bootstrap fixture and registry scheduler tests |
| Source engine-adapter tests | Replay invocation fixture and `runtime_callbacks.inc` |

No installed profiles, scripts, binding databases, DLLs or map assets were
modified. Historical staging scripts refuse to overwrite this integration;
the inventory generator refuses to replace the original source baseline.

## Exact binary and recovered ABI

`replay-database.json` identifies `game_dx12_ship_replay_dump.exe`, build
1.20.4.7623265, IDA image base `0x7FF7A95E0000`. `query.py` uses the Replay IDA
MCP endpoint on port 13341. Imported PDB names/types/function bounds belong to
another build in some ranges; explicit assembly, callers and field accesses
take precedence. Failed decompilations are recorded as failures, not evidence.

| Native item | Replay contract | Evidence |
| --- | --- | --- |
| Server context | Global `0xD591B00`; getter `0x12E0E70` | `ScriptContext_Server.json`, `context-init.json` |
| Begin/EndLoadScripts | `0x1316DE0`: context, byte user, **third uint32 precache count**; End `0x1316FE0` | `begin-and-getters.json`, `Scr_EndLoadScripts.json` |
| Stock tables | Function base 1/count `0x418`, method base `0x8000`/count `0x787`; pointers `0xD5B7350`/`0xD5B9410` | `mp-builtin-tables.json`, `table-setup.json` |
| Generic bridge | Replay clear/call/return opcodes `0x4B`, `0x1D`, `0x2F` | `bridge-opcodes.json`, `interpreter-switches.json` |
| Script execution | Global `0x1322C30`; entity `0x13241B0`, result callback/ownership preserved | `vm-execute-seeds.json`, `GScr_ExecEntThread.json` |
| EvaluateVariable | `0x13205E0`: context first, child ID second, **explicit result pointer third** | `vm-arrays.json`, `replay-marshal.json` |
| Weapon | Class 10; **60 descriptor bytes**, 62-byte pool record, alternate mode separate | `weapon-semantics.json`, `weapon-finish.json` |
| Dvars | Type `+0x1D`, current `+0x28`; formatted variant getter `0x13E6B00` | `weapon-and-dvars.json`, `notify-and-dvar.json` |
| Entities | Live-byte pointer `0xBC20F08`, 2,048 slots, world class 0 | `entity-table-xrefs.json`, `entity-lifecycle.json` |
| Players | Table `0xE17F690`, count `0xE17FCD0`; state `+8`, name `+0x534`/36 bytes | `connection-flow.json`, `entity-player-globals.json` |
| Frontend flag | Persistent globals pointer `0xE17FE10`, byte `+0x72` | `persistent-constructor-raw.json`, `runtime-startup-fields.json` |
| Reliable chat | Stock `T` command, reliable enum 1, client virtual slot 1 | `chat-render.json`, `chat-format-refs.json` |

The 80-byte cross-DLL Weapon snapshot contains Replay descriptor bytes 0..59,
reserved bytes 60/61, alternate byte 62 and zero byte 63, plus map/script
generations. Stale snapshots/entities return `IW8X_STALE`; legacy 12-byte Weapons
remain rejected. The Replay dvar catalog has 6,724 mappings from the existing
`dvar_names.h`, rather than importing 1.44 dvar identities.

## Stock resolution and native bindings

Replay's far-call decoder `0x13254F0` formats nonzero canonical module tokens as
decimal asset names. `canonical-module-reader.json` records the decoder;
`canonical-module-format.json` confirms the literal at `0x27169CC` is **`%d`**.
`linker-bytecode-flow.json` traces module/function operands from `0x1322350`
into export linking at `0x1316800`.

Readable script calls keep module scope. Only during EndLoadScripts the host
tries decimal module identity, readable path, then `.gsc` path. Native
`Scr_LoadScript` (`0x1317400`) handles loaded modules and far-call dependencies;
`Scr_GetFunctionHandle` (`0x1317270`) selects the requested export. Referencing
another module does not export its functions locally. Successes and failures
are cached per script generation; registrations survive reload while old
handles/snapshots are invalidated. Failed lookup logs every attempted module.
Builtins such as `getcurrentweapon` use their table and entity receiver without
caller-module lookup. Their class-10 Weapon result is decoded automatically.

Replay native binding databases require `"target":"replay-1.20.4"`.
Unmarked/1.44 data is rejected without rewriting it. Custom function IDs begin
at `0x419`, methods at `0x8787`; `0x519` belongs to the host VM trampoline.

## Lifecycle, installation and ownership

The common splash installer invokes the bootstrap after native prerequisites.
Dedicated mode waits for BAP; client-hosted MP does not. Runtime/compiled loader
installation precedes discovery. The pinned initialization thread pumps only
async work and diagnostic I/O; engine/GSC completions remain on the game thread.

Gameplay script loading at `0x126ECB0` begins map/script epochs before native
loading, then emits loaded after return. Frontend lifecycle is filtered out.
Readiness is queued after `ServerStart_Finalize` (`0x137E290`) returns, and
delivered at an idle gameplay RunFrame tail only when native ServerStart state
`0xE195760` is zero. Initialization/restart and active VM frames defer work.
See `runtime-idle-boundary.json`, `runtime-startup-boundaries.json`,
`runtime-request-owner.json` and `runtime-map-spawn.json`.

There are 14 runtime and eight compiled-loader hook seams with one owner per
entry. Dedicated bot resolution runs through the existing observer at the
single EndLoadScripts hook. Connection observation filters actual DirectConnect
return `0x136F353`; restart/migration cannot invent identities. Spawn observes
native completion. Death forwards eleven/twelve arguments and the committed,
engine-attributed killer. Nested Drop/Free emits one disconnect pair. Entity
generations invalidate before and after cleanup. Full server teardown observes
persistent/application shutdown; map-only teardown does not stop the server.

MinHook owners share the existing installation mutex. Batch failure cancels
queued enables and reverses partial application, keeping prepared originals for
retry while observers stay disarmed. Known console commands copy input and
registration identity into a bounded queue: 256 entries, 1,023 bytes per input,
32 deliveries per pump. Unload/re-registration retires queued input. Locks are
released before callbacks, nested tasks defer, and inherited count/time budgets
bound each frame. Slow samples use bounded deferred logging.

The proxy remains pinned for process lifetime. Explicit plugin unload uses the
source lease/drain/destructor/FreeLibrary path; map/server stop retains plugins.
No engine-quit hook or loader-lock cleanup is added.

## Compiled scripts and restart

Replay ScriptFile is 40 bytes: name `+0`, compressed length `+8`, uncompressed
length `+0xC`, bytecode length `+0x10`, buffer `+0x18`, bytecode `+0x20`.
Load/postload owners `0xDFE370`/`0xE01280` publish final payloads; preload does
not. Linking `0x1322350` mutates bytecode, so pristine snapshots precede linking.
Release dispatch `0xF5F0E0` retires aliases/storage generations. Oversized
replacements use verified native block-6 tail storage in the zone-1 script
allocation, not host heap pointers. Allocation wrapper is `0x11A8C20`, range
getter `0x11A9500`. `IW8x`, `.gsc` and catalog aliases remain supported.
See `compiled-asset-reader.json`, `scriptfile-stream-layout.json`,
`compiled-ownership.json` and `program-cleanup-proof.json`.

Shutdown owner `0x121F880` permits pristine restoration only after all native
compiler/program/VM reset conditions are empty. Partial clear retains state.
Restart owner `0x136BF60` promotes normal pending non-savegame restart to full
native script reload, preserving full EAX/savegame behavior without forcing a
partial shutdown flag. See `script-teardown-restart.json`, `final-reset-proof.json`
and `reset-postconditions.json`.

Link diagnostics inspect native children without creating objects or changing
references. The variadic Com_Error adapter at `0x12AB4A0` preserves GP/XMM/stack
arguments and LastError, then calls the original. It does not suppress errors;
diagnostic history retains TLS values rather than skipped stack RAII objects.

## Build and offline checks

From the staged project root in PowerShell:

```powershell
xmake build -y mw120rproxy
xmake build -y replay_plugins
xmake build -y gsc_core_tests
xmake build -y gsc_invocation_tests
xmake build -y gsc_loader_tests
xmake build -y gsc_bootstrap_tests
xmake build -y gsc_restart_tests
.\xmake-out\tests\gsc_core_tests.exe
.\xmake-out\tests\gsc_invocation_tests.exe
.\xmake-out\tests\gsc_loader_tests.exe
.\xmake-out\tests\gsc_bootstrap_tests.exe
.\xmake-out\tests\gsc_restart_tests.exe
# Historical binary audit: ../../prod-120/ida-reports/gsc_port/audit_port.py
```

Host: `xmake-out/x64/Release/XInput9_1_0.dll`. Examples/manifests are isolated in
`xmake-out/examples/`. `gsc_live_probe` is a compile-only opt-in target under
`xmake-out/manual-probe`; it is not installed or run. `validation.json` records
fixture results and artifact hashes. An existing unrelated xmake warning refers
to absent `tests/asset_mark_guard_tests.cpp` and does not block these targets.
An additional legacy `headless_d3d_tests` build cannot link because
`tests/headless_d3d_tests.cpp` is also absent (LNK1561, no entry point). Its
production console/command objects compile; no replacement test was invented.

Fixtures cover actual DLL load/unload, ABI/ownership/faults, scheduler
concurrency/reentry, 200-player bursts, 4,000 timers, script cache/reload/errors,
native value/Weapon ownership, Replay signatures/layouts, production lifecycle
wrappers, frontend filtering, compiled growth/release/reset, restart flags,
real MinHook rollback/retry, bootstrap and the variadic error boundary.

## Preserved limits and manual acceptance

Inherited source limits remain explicit: fields, logical interception,
GAME_INITIALIZED, incoming chat, live rename/XUID/address/ping/team/alive,
rich registered-callback returns/notify collections, sparse/associative/cyclic
values and arbitrary readable struct keys are unsupported. Generic stock and
script/self calls preserve rich argument/results without per-function wrappers.

Offline fixtures do not establish live engine behavior. Manual Replay acceptance
should cover first MP startup, client hosting/dedicated paths, player events,
map restart/full reload, stock calls and unload. Never install a 1.44 native
binding database as Replay IDs. Source/data preservation and binary evidence
are established; live acceptance is not claimed.
