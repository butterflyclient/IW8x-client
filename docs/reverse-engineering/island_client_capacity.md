# Island client-count mismatch

Investigation: 2026-10-22. Replay 1.20.4.7623265, playlist 104,
`mp_sm_island_1` / `br`.

## Observed failure

The reported dedicated command is valid:

```text
game_dx12_ship_replay.exe --dedi -bap --playlist 104 --map mp_sm_island_1 --mode br --min-players 1 +seta lui_skip_season_video 1
```

The frozen client log records `Mismatching client count: 102 vs 20` at
lines 15635 and 15682. This is client PID 39152's incoming-gamestate
rejection, returning from native `Com_Error` at RVA `0x15F0E0F`.
The corresponding server PID 8488 applies `max_players=102 native_slots=102`
at lines 417 and 457 and sends a gamestate with 102 total clients at lines
11548-11549. `--min-players 1` controls the start threshold, not allocation
capacity. The failure is not an argument-order error or a one-player limit.

Before this join, the client selects playlist 104 and synchronizes the
authenticated Island map at lines 14540-14545. Native lobby progression,
map resynchronization, and frontend shutdown follow at lines 14547-14556.
The same process had already completed earlier gameplay joins. This is
separate from the fixed XAnim failure and the fixed trace/loot native calls.

Evidence is frozen under
`build/client-count-20261022-032300Z/`. `baseline.json` records the logs,
timestamps and SHA-256 values; `source-baseline.json` records the pre-edit
source hashes. The installed DLL at capture was
`40370d8ed961cbba7d0c8b8f6f839fa934b4530841e8a3259af8d0e7a28ee260`.

## Native cause and correction boundary

Read-only IDA analysis of the Replay dump establishes:

| Native boundary | Behavior |
| --- | --- |
| `GameAllocate`, RVA `0x15E8CC0` | Gameplay allocations read `ui_maxclients` through pointer `0xD386878`, current integer at `+0x28`. |
| Allocator flags bit 1 (`flags & 2`) | The separate frontend path uses capacity 1. |
| `0x15E8D1A` | A nonzero allocated-local-client count at `0xEEB0680` causes reuse without resizing. |
| `0x15E8EEC` | Publishes the actual allocated gameplay capacity at `0xEEB0CE0`; multiple preceding allocations also depend on this count. |
| `CL_ConnectAndPreloadMap`, `0x15E7290` | Performs frontend shutdown and map setup before calling `GameAllocate(1)` at `0x15E7360`. |
| `Party_GetNumGameSlots`, `0x118C7D0` | Returns the received party capacity at `PartyData + 0x4BF34`. |
| Native StartMap and party-state parsing | Normally synchronize `ui_maxclients` from party capacity using `Dvar_SetInt`, `0x13E97B0`. |
| Gamestate parser, `0x15F0C90` | Compares the server count to the actual allocated capacity and rejects a mismatch. |

The custom join path previously synchronized BR configuration and map
selection without ensuring that the received capacity survived the later
frontend and registration transitions. Allocation could therefore retain
the smaller party/setup value 20 while the server advertised 102. The saved failure proves
the resulting allocation disagreement; it does not identify the exact
earlier native write that last selected 20.

The new first-join memory capture also records this transition directly:
the remote party remains at 102 while `ui_maxclients` changes from 102 to
20 after frontend teardown, then back to 102 before gameplay allocation.
This confirms that the UI count can change independently of the received
party capacity. It does not identify the instruction that wrote 20.

The registered `ui_maxclients` dvar itself defaults to 200, with a native
range of 1-200. Twenty is a later selected value, not that dvar's registration
default. The dedicated startup source also documents a private LAN party
setup default of 20; the saved client failure does not prove that specific
write was its source.

The correction runs at the existing allocator hook after the original MP
dvar registration restoration and immediately before the original native
allocator. It uses the current received remote GameParty capacity, rather
than a hardcoded playlist value, and synchronizes the registered integer
dvar through the native setter. The original allocation flags are forwarded.

The helper requires an authenticated Island assignment and a remote
dedicated GameParty. The assignment uses the existing cancellation/reset
lifetime. Frontend allocations, local games, unrelated assignments and
dedicated processes retain their existing paths. Native byte guards verify
the accessor, setter, allocation input and published counters. A fresh
allocation must read back the expected dvar value before proceeding.

An already allocated mismatching buffer cannot be resized by changing a
dvar. That condition produces a normal `ERR_DROP` before reuse; the fix
does not write allocation counters, call an unproven teardown, or suppress
the incoming gamestate validation.

The native evidence is `native-client-capacity-ida-20261022.json`, SHA-256
`801378ce080843937d7d4ba7ad72fd279d912c0a080f4755724a1d9c0fea8a6f`.
The report uses instruction decoding because Hex-Rays did not return a
decompilation. All recorded ranges decoded without errors; IDA database
contents were unchanged.

`native-client-capacity-contracts-20261022.json` additionally records the
1-200 domain/default-200 registration and ordinary local-client count getter.
Its SHA-256 is
`fed1d0bde9be4d5dc4f5419217c6bf585692c624057b011a190c10d24f388157`.

## Verification

The focused `client_match_capacity_tests` target passes 287 checks, including
all four byte guards against the installed Replay executable. It executes
the production helper with the native slot getter and a setter stub, covering
20-to-102 synchronization, dynamic 150-slot capacity, the 1/200 domain bounds,
scope exclusions, invalid state, setter readback failure and existing
allocation preservation/refusal. The check count includes fixture assertions.

The existing `production_runtime_tests` passes 3,733 checks, including its MP
dvar restoration cases and 1,182/1,182 installed-image native bindings. The
Release/x64 Island `mw120rproxy` build succeeds. Its existing vendored
`thirdparty/json.hpp` source-encoding warnings remain.

The nine protected prior source files in `source-baseline.json` retain their
hashes, including XAnim, infil/loot ABI translation, the MP registration helper,
the existing runtime tests and dedicated lifecycle. Changes to the allocation
wrapper, `night_files` assignment getter and XMake test target are intentional.

Deployment at 2026-10-22 03:39:40 UTC is recorded in `deployment.json`:

- New DLL: 26,445,824 bytes, SHA-256
  `bfb1353a50fe5ebb241a6ba56c838e887cc03a86efd0254e842dace707e764e4`.
- Rollback DLL: SHA-256
  `40370d8ed961cbba7d0c8b8f6f839fa934b4530841e8a3259af8d0e7a28ee260`,
  under the runtime's `backups/client-capacity-20261022-033940Z/` directory.
- Source, staged copy, installed DLL and rollback copy were hash-checked.

## Bounded live verification

Two actual Island joins succeeded in client PID 31264 without restarting
that client. The first used server PID 40576; the second used PID 41316.
Both servers were launched with the exact reported arguments and the deployed
DLL. Launch records retain the existing `IW8X_ISLAND_NUMERIC_CANONICAL=1`
and `IW8X_ISLAND_SOURCE_SCRIPT_MEMORY=1` opt-ins. The server additionally
uses the existing loadout diagnostic trace. No game allocation counters or
party capacities were modified by the read-only observers.

| Evidence | First join | Same-client rejoin |
| --- | --- | --- |
| Frozen client log | `first-join-client.log`, line 1722 | `second-join-client.log`, line 4630 |
| New allocator hook | `unchanged`, flags 1, party/UI 102, allocated 0, cached 0 | Same |
| Native callsite | `0x15E7365` | `0x15E7365` |
| Server gamestate count | 102, server log lines 11806-11807 | 102, server log lines 11360-11361 |
| Native player spawn | Server log lines 11864-11865 | Server log lines 11417 and 11419 |
| Actual allocated gameplay capacity | 102 with one local client | 102 with one local client |
| Active gameplay state | Connection 9 / frontend 0, tick 132425125 | Connection 9 / frontend 0, tick 133022031 |
| Read-only sampling | 8,984 samples, zero read errors | 8,955 samples, zero read errors |
| Visual evidence | `client-join-progress.png`, Island parachute/warmup | `client-rejoin-in-game.png`, Island on foot with weapon |

The first join used the Warzone Blood Money UI selection. The rejoin followed
the ordinary Leave Game menu and native matchmaking shutdown, then selected
104 with the supported `mm_start 104` / `xstartlobby` console commands from
the MP frontend. The first match's normal departure produces the expected
`PLATFORM/DISCONNECTED_FROM_SERVER` code 3; it is not a capacity failure.

The original test server finished its empty match and automatically recycled.
Its existing restart command omitted `--map` and `--mode`, so that recycled
child (PID 40204) was closed and the original full Island command was used
for PID 41316. The client remained running throughout. This pre-existing
dedicated lifecycle behavior was not changed by the capacity fix.

The saved logs contain no `Mismatching client count`, `XAnim Load hit fatal`,
capacity-preparation drop or MP-registration-unavailable message during these
two joins. `first-join-files.json` and `second-join-files.json` record evidence
hashes; `source-preservation.json` records the nine unchanged protected files.
`leave-game-capacity.json` records the intervening native teardown. The native
bindings were also read and matched in the running client before hook install
(`live-client-31264-native-guards-034238Z.json`).
`verification.json` records the combined results, exact failure-string counts,
deployed hash and final running process/module identities.

### Remaining live coverage

Both live allocation calls reported `unchanged`: native code had already
restored 102 by the final allocation boundary. These tests verify actual
joining, native allocation, spawning and one same-client rejoin, but they do
not demonstrate a live `status=synchronized` call with `ui_before=20`. The
20-to-102 correction, invalid-state handling and existing-buffer mismatch
guard are verified by the focused production-helper tests. The existing MP
dvar restoration warning also did not appear during these live captures;
its recovery branch is covered by the unchanged runtime regression tests.
A naturally
occurring stale-20 allocation remains the specific live retest still needed;
the bounded successes do not establish that every intermittent sequence has
been reproduced. No fault injection or persistent dvar override was used.
