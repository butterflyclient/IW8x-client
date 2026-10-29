# Island crates open without producing loot

This follows the Island XAnim and `ray_trace_ents` corrections documented in
`island_client_xanim.md`. The user confirmed that the infil sequence works,
then reported that lootboxes open but produce no items. The investigation
uses the installed Island companion, Replay 1.20.4.7623265, the running
playlist 104 / `mp_sm_island_1` / BR session, and native Source 1.64.5 and
Replay code analysis.

## Cause and scope

The installed converted `br_lootcache.gsc` and `br_pickups.gsc` retained
seven Source native-function IDs whose Replay implementations occupy
different slots. Opening a crate changes its part state before starting
the separate loot-generation thread. Consequently, a successful opening
animation does not establish that content lookup, trajectory calculation,
or Scriptable spawning completed.

The pickup limit initializer also calls Source function `0x434`, which has
no Replay builtin. In the frozen server log for PID 34456, line 10175
explicitly reports that missing function at `br_pickups.gsc` offset 28669.
Lines 10176–10187 then record operations involving 510 and undefined,
undefined-to-bool/int casts, and undefined division. Source native code
proves that this function returns the total reserved Scriptable capacity.
It must not be substituted with the free/remaining-instance getter.

The wrong dispatches are established by the installed opcode bytes and
both engines' native registries and function bodies. The pre-fix observer
had already exhausted its 64 unique error records before the first human
spawn. That log cannot identify the exact last failing instruction of a
later crate opening, or prove that these are the only remaining port issues.

## Native contracts and installed call sites

Offsets below identify the opcode, before native link relocation.
Fixed-argument opcodes encode their argument count as `0x1D + argc`.

| Installed script | Offset | Arguments | Source ID | Replay ID | Operation |
| --- | ---: | ---: | --- | --- | --- |
| `br_lootcache.gsc` | 2220 | 2 | `0x3F0` | `0x3EF` | Query the cache's native contents |
| `br_lootcache.gsc` | 3961 | 4 | `0x3F1` | `0x3F0` | Draw an item from the fallback loot table |
| `br_pickups.gsc` | 4161 | 3 | `0x40F` | `0x40E` | Find vehicles inside the drop radius |
| `br_pickups.gsc` | 4575 | 2 | `0x3D4` | `0x3D3` | Calculate the gravity-arc payload |
| `br_pickups.gsc` | 15631 | 5 | `0x3DA` | `0x3D9` | Spawn a Scriptable with a custom weapon |
| `br_pickups.gsc` | 15691 | 4 | `0x3AF` | `0x3AE` | Spawn an ordinary standalone Scriptable |
| `br_pickups.gsc` | 28863 | 0 | `0x3D5` | `0x3D4` | Query remaining reserved instances |
| `br_pickups.gsc` | 28666 | 0 | `0x434` | Island provider | Query total reserved capacity |

The native comparison covers argument types, return types and error
behavior, rather than treating the numerical difference as a general
mapping rule. The ordinary crate descriptor supplies defined angle and
integer payload values to both spawn calls. Source accepts some explicitly
undefined optional values which Replay only defaults when omitted; no
broader optional-argument compatibility claim is made.

Replay's cache query returns undefined for reserved instances, whereas
Source supports them. The script's existing fallback remains in place and
its random-draw call is one of the seven translations. Both exact Plunder
fallback tables were found in the running server's native StringTable pool:

| Table | Native rows × columns |
| --- | ---: |
| `mp/loot_set_cache_contents_rare_dmz_ground_tablets.csv` | 92 × 9 |
| `mp/loot_set_cache_contents_lege_dmz_ground_tablets.csv` | 29 × 9 |

The native `mp/loot_item_defs.csv` has 252 rows and 33 columns, including
130 item rows. The existing Island table-route alias resolves to that table.
No table alias or content change is warranted by this evidence.

The startup NightLoot message reports installation of a policy. Its active
map predicate admits `mp_kstenod` and `mp_escape2`, not Island; that policy
does not explain this failure and is unchanged.

## Implementation

`src/gsc/compiled/island_loot_native_abi.hpp` recognizes only the two exact
installed module names, lengths and full FNV-1a fingerprints. It validates
every opcode and native ID before changing bytes, then validates the entire
output fingerprint. An incompatible module is rejected without partial
translation. Only the seven low bytes of the function IDs differ.

| Module root | Length | Input FNV-1a | Output FNV-1a |
| --- | ---: | --- | --- |
| 708 / `br_lootcache.gsc` | 4832 | `C0665D9B59072881` | `40A219E3A9E0B5D1` |
| 742 / `br_pickups.gsc` | 29808 | `71D088A53F0BAD8A` | `3C73267FB4BA5EAB` |

The fixtures are from the installed-stage extraction, after relative-operand
conversion. Their SHA-256 values are:

- Cache: `5c52f9122e41050683226ae0cd2cd4280cc6d3f26e697591d5f527ff6a94b340`.
- Pickups: `5adb4fe7d45f22355819bc03b7aee720ec0a5fc601161091414d0bb83105282d`.
- Installed `magma_island_scripts.ff`: `d4e84049a042fa3b83e69daccc918f92eb7acce5632d2c925e3d57e8e44b3421`.

`gsc.cpp` applies this only to the private compiler copy during the
canonical Island server script load. The registered assets and archive
remain unchanged. The translated-copy identity is tracked through repeated
native links, then cleared on script-load epoch changes and teardown.
Existing quest and trace translations remain active.

`src/diagnostics/island/island_builtins.cpp` provides Source `0x434` through
the existing Island extension mechanism. Source RVA `0x34B7B60` reads its
total reserved counter at `0x1343CC90`. Replay uses RVA `0xD591AC8` as the
same reserved/runtime versus fixed-map-instance boundary in its
IsReserved and cache-lookup code. Three exact instruction sequences,
including their RIP-relative references, are verified before publishing
the provider. The provider reads the current engine counter on every call
and returns it with the existing `Scr_AddInt`. It does not cache the value
across world teardown.

`src/gsc/compiled/link_diagnostics.cpp` retains the existing 64-event
general budget and reserves another 16 unique records for these two exact
loot modules after that budget fills. This allows a later live crate
failure to be observed. It preserves native error handling and does not
change gameplay or suppress failures.

## Verification

The two loader targets built and ran successfully after the change:
`gsc_island_loader_tests` and `gsc_loader_tests`. The Island checks cover
the exact installed fixtures, rejection of wrong module/size/opcode/ID/
fingerprint without mutation, exact successful output, retention of
`0x434`, private-copy ownership, excluded contexts/maps, repeated linking
after relocation, epoch renewal and teardown.

Independent reviews checked the native contracts and capacity provider.
The installed Replay executable's SHA-256 is
`68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0`.
All three provider guard sequences match that executable and resolve to
the same aligned, writable engine counter.

The `mw120rproxy` Release build also passed with exit 0 at 02:21:20 UTC.
It compiled the three changed production translation units and linked
`xmake-out/x64/Release/island-probe/XInput9_1_0.dll`, 26,442,240 bytes,
SHA-256 `40370d8ed961cbba7d0c8b8f6f839fa934b4530841e8a3259af8d0e7a28ee260`.
The build log is `island-loot-main-build-20261022.log` in the evidence folder.

The exact fixture, build/test output, source and test-executable hashes are
in `loot-native-abi-validation-installed-20261022.json`, SHA-256
`5efee220625966524dc03a270a8605cada5ef0627c316a89e6c2c834e36ebc21`.
An independent reviewer reproduced both complete installed fixtures and
all four fingerprints, verified the seven-byte-only output changes, and
read the successful test output.

The original MP dvar registration implementation, its test source, and
the unrelated dedicated lifecycle changes are preserved. The XAnim and
trace fixes remain part of this candidate.

## Candidate deployment and live retest

The old client PID 33536 and server PID 34456 exited after targeted window
close requests. Their final logs were frozen before reopening either
process. The new DLL was atomically installed and both candidate and backup
hashes verified at 02:23:49 UTC. The previous successful trace/XAnim build,
SHA-256 `542216f81a4f45385c24e4836bd6e7f46110a5198f29f506f1dd96133d1241b9`,
is preserved at `backups/island-loot-20261022-022349Z/XInput9_1_0.dll`
under the game directory.

The deployment ledger is `loot-deployment-20261022-022349Z.json`.
`loot-candidate-predeployment.json` records the unchanged hashes of both
MP dvar registration files, their runtime test source, and the unrelated
dedicated lifecycle source.

Explicit Island / BR / playlist 104 server PID 40020 started at 02:23:50
UTC. Client PID 39152 started at 02:25:09 UTC with `--client-console`.
Both use the same candidate DLL and the existing numeric-canonical and
source-script-memory settings. Server loadout tracing remains enabled.
The existing backend PID 18128 remains running. Exact arguments and
timestamps are in the separate server and client launch records.

The completed startup capture
`lootfix-ready-pid40020-20261022T022942156683Z-f2e7d48d.json` and its
companion server log verify the candidate's native script load:

| Observation | Server log line |
| --- | ---: |
| Existing trace translation accepted | 9663 |
| Five pickup calls translated | 9676 |
| Two lootcache calls translated | 9703 |
| Total reserved capacity returned as 1,376 | 10130 |
| Island / BR server ready | 10290 |

The capture contains 51 runtime errors, below the general budget of 64.
Neither loot module has an error at this point, and the four previous
pickup initializer errors did not recur. Other port errors remain.
The saved log SHA-256 is
`10f0c4032995cf7a72fc19ac4cff885956341ee552d2b41644dc843f7133aabd`.

Client log lines 600–602 identify the authenticated playlist 104 Island /
BR assignment. `loot-client-mp.png` at 02:30:26 UTC shows the native
Plunder/Island lobby and match countdown. `loot-client-join-progress.png`
at 02:32:02 UTC shows the first-person Island world, weapon and HUD.
The ordinary `join_status` command reports client state 9. Thus the
candidate reaches actual human gameplay with the earlier fixes active.

### Live loot observations

The reopened candidate subsequently reached the active round. At 02:35:07
UTC, `loot-client-after-warmup.png` shows an open supply box next to the
player, a Snapshot Grenade on the ground with the ordinary `F Take`
prompt, a round timer of 29:04 and $7,000 on the cash HUD. No scripted
spawn, native function call or game-memory write was used to produce it.
The opening happened between captures; the root agent had not submitted
an open/use input.

At 02:36:54 UTC, `loot-client-live-drops.png` shows continued live play
with an equipped weapon, eight armor plates and $20,000. These are direct
UI observations. They do not identify the provenance of each inventory
item or establish the exact contents of a particular box.

The user subsequently confirmed: "crates open and drop loot and you can
pick it up". This completes the live functional check of the reported
crate/drop/pickup behavior. The direct user confirmation complements the
screenshots and native object evidence below.

The ignored `capture_pickup_state.py` helper corroborates actual item
creation through objects in `level.br_pickups.scriptables` and their
native definitions and positions. It validates seven layout signatures
and exact process/build identity, uses read-only process rights, and
performs no engine calls or writes.

The snapshot
`pickup-state-visible-open-crate-pid40020-20261022T023613008835Z.json`
compares the same process and VM epoch with the earlier connecting-human
snapshot at 02:30:54 UTC. The registration counter advanced from 0 to 13;
four registered objects remained live at 02:36:13:

| Native instance | Definition | Registration index |
| ---: | --- | ---: |
| 66 | `brloot_offhand_claymore` | 7 |
| 69 | `brloot_plate_pouch` | 10 |
| 70 | `brloot_offhand_smoke` | 11 |
| 71 | `brloot_weapon_ram7_lege` | 12 |

Each is a class-8 reference with a corresponding native ScriptableSv
context and the native loot flag set. Capacity remains 1,376 and the
script cleanup limit is 510. The capture reports no read errors or
truncation, unchanged VM identity, and idle VM boundary checks. The player
had moved away by this capture; those four objects were 1,631–3,386 native
units from the current human position. The comparison spans connection,
warmup and match play, so it proves real pickup creation rather than
isolating one crate's output. External reads are not atomic and native
indices can be reused.

The final frozen logs at 02:36:50 UTC have prefix
`lootfix-active-box-pid40020-client39152-20261022T023650368207Z-7c500ca8`.
The server has 64 general runtime errors, all outside the two loot modules.
Neither loot module has a recorded error, and none of the extra 16 loot
slots has been consumed. The client has no runtime-error report. Neither
saved log contains a fatal, XAnim-load or client-count mismatch diagnostic.

| Saved log | Lines | SHA-256 |
| --- | ---: | --- |
| Server | 13,647 | `a6de69e6637cead47058dae1cc3adae109fe49f6b03aaed671c8e19a6060ce65` |
| Client | 3,199 | `896a3370e61df7be9b9acc6953d14a0b3e2c14a71bc9514f84c3236d7fd8af99` |

This verifies that the corrected build loads, joins, spawns the human and
creates real loot during the live Island match, with an opened box and
usable ground-item prompt directly observed and item collection confirmed
by the user. The automated comparison does not isolate one box. Exhaustive
crate-rarity coverage, repeated map lifetimes and a complete round remain
outside this result.

At wrap-up, the test server had automatically recycled to PID 37992 with
`--recycled-host 40020:134371094300779591`. Its replacement arguments
omitted the explicit Island map/mode, reproducing the separately known
recycle-selection limitation. The client PID 39152 and backend PID 18128
remained running. The root agent requested closure of that replacement to
restore the explicit Island launch with the same tested DLL; no lifecycle
code was changed. `loot-user-confirmation-and-recycle.json` preserves the
confirmation, replacement arguments and frozen logs.

The restored server is PID 30216, started at 02:44:14 UTC with explicit
`--playlist 104 --map mp_sm_island_1 --mode br --min-players 1` and the same
verified `40370d8e...` DLL. Its launch is recorded in
`loot-restored-server-launch-20261022.json`; it is still initializing at
this checkpoint. The successful live loot test above belongs to PID 40020.

## Evidence

Evidence is retained under
`build/xanim-investigation-20261021/lootbox-20261022/`:

- `native-loot-contracts-20261022T020946Z.json`: native registry entries,
  function bytes, disassembly and argument/result contracts;
  SHA-256 `275e1a7bc3f2e64923271f294ea2633d7d5a5e99807068ca1eab105ab6404035`.
- `capacity-provider-validation-20261022T021309Z.json`: exact executable
  and provider source hashes, signature matches, resolved targets and
  section layout; SHA-256 `e6e2a704cb5c0e195f9f65d911c1c038a8ff587faadac9a1d0f76238f566107b`.
- `lootbox-session-20261022T015721150056Z-f676dd8a-server.log`: frozen
  pre-fix runtime evidence; SHA-256 `91cc61ca3b8a49313752f772f33d69824b70b4d6912cd21059ef5ac21a5fb048`.
  The matching `-identity-and-log-analysis-v2.json` and
  `-server-runtime-errors-v2.json` use per-event source association.
- `native-loot-table-pid34456-20261022T020333466693Z-d669c59c.json`:
  bounded native item-table capture.
- `native-cache-table-presence-pid34456-20261022T021351138981Z-c3406d82.json`:
  exact fallback-table presence, stable process and pool identity.

The read-only runtime snapshots are bounded external observations, not
atomic snapshots taken at the failing crate instruction.
