# Island client frontend XAnim failure

Investigation: 2026-10-21 through 2026-10-22.
Target: Replay 1.20.4.7623265, authenticated
`mp_sm_island_1` / `br` assignment through playlist 104. Source tree:
`iw8x-production/project`.

## Root cause

The ported map's `StreamingInfo` registers AI animation names as transient
assets before the frontend necessarily has a resident definition for them.
Replay's animation-tree parser retains an otherwise optional leaf when the
asset is marked transient, even if its active database entry is still a
default. The later validator correctly rejects that default when the retained
leaf belongs to a `loopsync` / `nonloopsync` group. This produces the frontend
`XAnim Load hit fatal errors` drop.

The three repeated names in the current client log are:

- `hm_grnd_org_walk_cqb_reload`
- `hm_grnd_org_run_reload01_ar`
- `hm_grnd_org_run_reload02_ar`

This is a residency and frontend admission problem, not a missing archive,
an absent map catalogue row, or the earlier MP dvar allocation crash. The real
animation definitions exist in the installed soldier dependency. A real
definition staged behind a default database head does not satisfy the native
validator.

## Runtime evidence

The initial `mw120rproxy.log` for client PID 29184 recorded the same failure at
lines 4469-4475, 7430-7436, and 10889-10895. The immediately preceding native
script loads linked 40 scripts. The error's direct return address is RVA
`0x1317760`; the stack also includes `0x121D950`.

IDA established that `0x121D950` is the return from the tree loader in the
frontend branch of game animation initialization. Settings byte `+0xD3`
selects this branch. The actual gameplay branch has a different return,
`0x121D9D1`. Therefore these drops occur while rebuilding the local frontend
server during a join/return transition.

The current `installed-maps.json` is a generated native-catalogue report,
not the input used to admit custom maps. Its missing native Island row is
expected; playlist 104 remains present. Runtime custom-fastfile verification
and the authenticated assignment select the admitted Island bundle.

## Installed asset proof

The actual installed main fastfile has SHA-256
`be368a0aa50ee199e3bb8880052774c5a255193eb424fbf86c3049d1dbe4fff3`.
Its decoded archive SHA-256 is
`e60b0e5470435988999822e24182035686cd80b7535a4b0dd6381fefe51c6edd`.

The installed `mp_aianimset_soldier_lw_br_tr.ff` has SHA-256
`69fb5eff366f5d4a3e428e430859d50e7db2362735088b66df42e69529192b0e`.
Actual decoded type-7 records 45, 46, and 48 contain the real run01, run02,
and walk definitions respectively and match the validated conversion trace.

The main map's typed asset graph has no direct XAnimParts reference to these
three names among 21,510 root/nested records and 246 XAnimParts records.
However, its type-67 `StreamingInfo`, root 11504, explicitly registers all
three in the transient asset array. The `TransientInfo` header begins at raw
offset 120825054; its 4,980 asset records begin at 120913026. Records 45, 46,
and 48 have type 7 and file-index sentinel `UINT_MAX`. The corresponding
names occur at raw offsets 120993749, 120993777, and 120993831.

Deferring only the two AI dependency fastfiles during preload is insufficient.
Native metadata registration creates the placeholders independently of
loading either AI dependency file. This correction retains the ordinary
dependency lists and their native descriptor flags.

The live public `generic_human.atr` is unchanged stock data: 1,462,754
decompressed bytes, SHA-256
`81ce77649f0aee03e605739cd41c17d2ed651cd31a322470ae03e54982f69ba7`.
The combined healthy frontend snapshot at 22:48:51.528-22:48:52.082 UTC
contained this compiled tree (1,622 nodes), alongside `script_model`,
`vehicles`, `scriptables`, and `mp_frontend`. In that same bounded capture,
all three animation names were absent from 13,945 owned type-7 entries,
including stashes, across the full 716,800-entry table. Frontend state 8,
the empty collision-map name, compiled-tree slots, and NCS list head were
unchanged before and after; there were no read errors. This is an observation
with before/after checks, not an atomic engine snapshot. Presence in the raw
tree alone does not require keeping these optional leaves; transient
registration changes that selection.

The private tree package contains private RawFiles only. Neither it nor the
main map replaces the public `generic_human` tree. The duplicate
`client_character` NCS warning also exists in the healthy frontend and does
not establish the cause.

## Verified native paths

All addresses below are RVAs in the Replay image analyzed through IDA MCP.

| RVA | Behavior |
| --- | --- |
| `0xD8FBD0` | Registers `StreamingInfo`, following its `TransientInfo` pointer at `+0x40`. |
| `0x11AD1A0` | Registers transient metadata. Asset array: pointer `+0x58`, count `+0xA8`, stride 16. |
| `0x11AD379` / `0x11AD37C` | File index `UINT_MAX` bypasses the file-type checks and reaches asset registration. |
| `0x11AD41B` / `0x11AD439` / `0x11AD444` | Creates, initializes, and inserts a zone-zero default when a metadata name is absent. |
| `0x11AD5CE` / `0x11AD5DA` | Marks the entry as transient. |
| `0x13151FA` | Queries type-7 asset information for an otherwise optional tree leaf; return is `0x13151FF`. |
| `0x11AA010` | Returns active asset header, zone index, default status, and transient status. |
| `0x1315203` / `0x131520D` | Keeps the leaf if real, or if marked transient even when default. |
| `0x1315224` | Native optional-leaf removal. |
| `0x14CB720` | Native required sync-group validation; missing/default animations remain errors. |
| `0x131775B` | Raises the final XAnim error, returning at the logged `0x1317760`. |

The asset-info ABI is `bool(unsigned type, const char* name, asset_info*)`.
The 16-byte output has a header pointer at `+0`, a `uint16_t` zone index at
`+8`, default status at `+0xA`, and transient status at `+0xB`. On a false
return the output is not initialized and must not be inspected or changed.

The query at `0x13151FA` is an optional-only boundary. Referenced and alias
references set the keep flag earlier in the parser. Referenced/forced leaves
branch directly to retention at `0x13151BA`, and parent flag bit 3 does the
same at `0x13151C2`; both bypass this query. Those required paths still reach
the native validator.

Preload zone flag `0x00100000` can leave real imported XAnimParts stashed
behind default heads. The `DB_LoadFastfiles` mode argument is a bit field:
mode 2 alone does not identify preload. The relocated entry-table operands
for the parser query (`0x11AA038`), default predicate (`0x11AC53F`), and
transient predicate (`0x11AC6B7`) are already patched consistently.

## Correction and scope

The correction belongs in `night_animtrees.cpp`. It verifies the native
asset-info entry point and optional-query callsite, calls the original
function, and narrows its local result only for the exact parser caller,
an admitted Island bundle, and an active native frontend. When the optional
asset is both default and transient, its local transient status is cleared.
The native parser then performs its existing removal of an unavailable
optional leaf.

The database entry, header, zone, and default status are not changed.
Failed lookups do not expose uninitialized output. Other callers, other
asset types, actual resident animations, and gameplay keep native behavior.
Required references bypass this optional query and continue to fail normally
if their data is missing. The existing private-tree frontend guard remains
`state >= 5`; native code sets that state before constructing the frontend
server.

The existing MP dvar registration implementation and its regression cases
are independent of this fix. Their post-build hashes match the baseline
recorded before editing. The unrelated dedicated lifecycle edits also match
their baseline. No dependency scheduling or installed-asset changes were made.

## Verification status

Verified on 2026-10-21, using the existing x64 Release configuration with
`island_port_probe=true`:

| Check | Result |
| --- | --- |
| `xmake build -j 4 custom_fastfile_tests` | Passed, exit 0. |
| `xmake run custom_fastfile_tests --bindings-only <Replay executable>` | 2,891 checks passed, exit 0. |
| `xmake build -j 4 production_runtime_tests` | Passed, exit 0. |
| `xmake run production_runtime_tests <Replay executable>` | 3,733 checks passed, exit 0; 1,182/1,182 native signature bindings resolved. Includes the five existing MP dvar cases. |
| `xmake build -j 4 mw120rproxy` | Passed, exit 0; rebuilt the animation adapter and linked the release proxy. |
| `git diff --check` | Passed. |
| Independent source review against IDA | No concrete correctness issues found in ABI, optional caller scope, output preservation, or hook installation. |

Both executable-dependent suites used the installed
`D:\Games\Call of Duty Modern Warfare (1.20.4.7623265)-001\Call of Duty Modern Warfare (1.20.4.7623265)\Call of Duty Modern Warfare (1.20.4.7623265)\game_dx12_ship_replay.exe`.

The focused checks cover the three observed animation names, active frontend
states 5 and 8, gameplay states 0 and 4, missing Island admission, disabled
custom maps, unrelated callers/types, resident transients, ordinary defaults,
failed lookups, null output, unreadable frontend state, original argument
forwarding, and preservation of every other output byte. Mapped-image checks
exercise signature rejection, preparation rollback, successful installation,
and idempotency. They do not execute the native asset database in an
uninitialized mapped image.

Built artifact:
`xmake-out/x64/Release/island-probe/XInput9_1_0.dll`, 26,436,096 bytes.
SHA-256:
`323eb9b33786fe970561318fad16e012a064d2cbb5b1c3b3ccc87df358f03275`.
Build and runtime test logs are retained in the investigation directory as
`build-mw120rproxy.log`, `build-production-runtime-tests.log`, and
`production-runtime-tests.log`.

Preserved baseline SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `src/client/component/mp_dvar_registration.cpp` | `e1fd593504092f9fd68a3a8f47ba04bfdbebd9ce20355709ef29fd9b01f7e3ee` |
| `src/client/mp_dvar_registration.hpp` | `ea38eb3f8e15686ea64fd3d3793ae588135f1351ca45243a02e98ffc89fe7aff` |
| `src/client/tests/runtime.cpp` | `214d508d7d6e228211db687d4e9f74e5f351db784b2410d80547f8df412507a4` |
| `src/dedicated/runtime/lifecycle.cpp` | `d9f28d4cb3f5115b587a3e613b4f3fed4a9a772846da96fc64b2e17a2febc12a` |

Initial offline verification left the new DLL in the build output. A later
reported failure was traced to a fresh client, PID 19996, which still loaded
the previous runtime DLL. Its startup banner was the old
`private original trees selected for registered, admitted gameplay maps; stock frontend trees retained`
message, and the installed SHA-256 was still
`0a8523d31a89654eb2014925c959638b8c720c8588202725d36e37335a87eb10`.
This report therefore did not exercise the new optional-animation adapter.

The exact supplied failure appears at lines 5084-5116 of the saved
`build/xanim-investigation-20261021/mw120rproxy-19996-predeployment.log`:
thread 34480, native script epoch 3 with 40 links, and join tick 116625968.
A second occurrence at lines 5722-5755 has join tick 116680515. That client run
logged neither the new hook installation banner nor a prune diagnostic. Individual
failure lines do not contain wall-clock timestamps.

The tested DLL was installed into the actual game directory at
2026-10-21 19:30:51 EDT (23:30:51 UTC). Its installed SHA-256 now matches the
built artifact above. The old DLL is preserved under the game directory at
`backups/xanim-20261021-232951Z/XInput9_1_0.dll`; the replacement operation
also retained `XInput9_1_0.replaced.dll` there. Both backups match the old hash.
The adjacent `ledger.json` records paths, hashes, and the installation result.
A staged, hash-verified file was atomically replaced without terminating
either existing process (19996 or 40112) or writing their memory. Those
existing processes retain the old loaded module until restarted.

## Live retest: 2026-10-21 EDT / 2026-10-22 UTC

At the user's request, the dedicated server and graphical client were reopened
with the installed DLL above. Server PID 38976 started at 00:07:11 UTC with
explicit Island / BR / playlist 104 arguments; client PID 36920 started at
00:08:51 UTC with `--client-console`. Both startup logs contain the new
`unavailable optional frontend animation pruning installed` banner.

The server completed its native gameplay initialization and published a ready
Island / BR / 104 session. Two native BR UI matchmaking selections produced
authenticated Island assignments and native `JoinLobby` events. The client
completed game initialization and reached connection state 9 after each join.
IDA confirms that this state is `CA_ACTIVE`, written by
`CL_CGameMP_FirstSnapshot` at RVA `0x15DC7A5`. The server sent baseline snapshots
and received delta requests. These observations establish successful network
and client-game entry; they do not establish BR character spawning.

| Event | Saved log evidence |
| --- | --- |
| First authenticated assignment and native join | `join2-client.log` lines 842-856. |
| First baseline and requested delta | `server-38976-complete.log` lines 12142-12150. |
| First gameplay transition to `CA_ACTIVE` | `join2-client.log` line 3615; still active at lines 4512-4518. |
| Requested normal disconnect | `join2-client.log` lines 4848-4895; code 3 `PLATFORM/DISCONNECTED_FROM_SERVER`. |
| Second BR UI selection and authenticated join | `join2-client.log` lines 6141-6146 and 6259-6273. |
| Second gameplay transition to `CA_ACTIVE` | `join2-client.log` line 8060, tick 120368750. |
| Second baseline and requested delta | `server-38976-complete.log` lines 13300-13308. |
| Timed match end and normal server recycling | `server-38976-complete.log` lines 13348 and 13389-13408. The resulting client disconnect is code 3 `EXE/DISCONNECTED`. |

The fresh client log contained zero XAnim fatals through 00:28:34 UTC. It also
contained zero actual optional-animation prune diagnostics. The return paths
survived, but a post-disconnect frontend tree build at state 5 or above was not
observed in that capture. Consequently this is a bounded successful join/rejoin
test, not proof that the intermittent placeholder case was encountered or
eliminated in all native DB lifecycle orders.

The user reported remaining at **Gearing Up** after joining. That separate
BR class/infiltration/spawn gate is being investigated using genuine script
state. `CA_ACTIVE` alone must not be described as successful player spawning
or completed gameplay. The animation validator and spawn state were not
bypassed for this test.

The automatic replacement server, PID 18304, omitted the explicit Island map
arguments and selected stock Donetsk from playlist 104. It was closed normally
and a new explicit Island server, PID 26948, was started at 00:30:31 UTC with
the existing observational `IW8X_ISLAND_LOADOUT_TRACE=1` setting. The DLL was
hash-verified again and not changed. This later diagnostic run is not included
in the two-join result above.

### Third join: pruning observed, separate capacity rejection

A third normal UI join against diagnostic server PID 26948 exercised the new
optional-animation branch. The saved client capture
`join3-client-36920-20261022T004639805011Z.log` records the authenticated
Island / BR / 104 assignment at line 11256 and native JoinLobby at line 11270.
After the frontend's 40-script load, lines 12130-12137 contain eight
`pruned unavailable optional frontend animation ... (transient default)`
diagnostics, beginning with `sdr_cqb_fast_gun_down`. These eight names do not
individually include the three originally reported human reload animations.
The frontend subsequently received its first snapshot and reached state 9 at
line 12144. There is no XAnim fatal in this capture. This establishes live
execution of the correction and completion of that frontend rebuild.

The subsequent network Island join did not reach gameplay. At line 13355,
native `Com_Error` reports `Mismatching client count: 102 vs 20`, returning to
RVA `0x15F0E0F`; line 13379 places the network connection in state 3. The error
is also visible in `client-count-error-0044.png`, captured at 00:46:18 UTC.
The server's corresponding capture,
`join3-server-26948-20261022T004639805011Z.log`, reports 102 clients in its
gamestate at lines 11876-11877. It records no actual native `ClientSpawn`.

IDA identifies this as a distinct allocation failure: the first count byte
read by parser RVA `0x15F0C90` is compared to the allocated client count at
`0xEEB0CE0`. MP GameAllocate (`0x15E8CC0`) takes that capacity from the current
`ui_maxclients` dvar (`0xD386878`, registered name `LOSORKOTOO`) and reuses an
existing allocation when `0xEEB0680` is nonzero. This evidence does not identify
which earlier transition selected 20. The preserved MP dvar registration hook
forwards the allocator's flags unchanged; the client capture contains no
registration-restoration warning.

This third failure occurs before player initialization and cannot explain the
earlier Gearing Up stall. The diagnostic server's post-disconnect script
snapshot shows `game["state"] = "playing"`, `prematch_done = 1`, and
`infil_setup_complete = 1`, with no connected human. Undefined class/spawn
fields observed while that rejected client was in server states 3/4 are not
evidence of a stalled active player. At that stage a fresh active human
capture was required; the fourth join below supplies it.

### Fourth join: active human remained at Gearing Up

A fourth normal UI join supplied the active-human evidence missing from the
third attempt. The saved `gearing-up-active-human-0108a-client.log` identifies
`mp_sm_island_1` / `br` and a 102-to-102 session count at lines 18535-18537;
line 18794 records the network transition from state 8 to `CA_ACTIVE` (9).
The `client-keyboard-right.png` screenshot at 01:08 UTC shows Gearing Up.

The read-only client sampler
`gearing-up-client-capacity-pid36920-20261022T010830482520Z-fd35ee6a.json`
covers 01:08:30-01:08:35 UTC. All 250 samples retain cached capacity 102,
`ui_maxclients = 102`, one allocated local client, connection state 9, and
frontend state 0; there are no read errors. The third join's 102-versus-20
rejection therefore does not explain this active gameplay stall.

The server capture `gearing-up-active-human-0108a.json`, taken at
01:08:58 UTC, identifies PID 26948, human slot 0 in native state 5, and player
script object 13450. Its context identity is unchanged and the VM is idle at
both boundary checks. These are bounded external observations rather than an
atomic engine snapshot. The installed DLL is still the earlier
`323eb9b3...` animation-fix build.

| Observed player or level state | Value |
| --- | --- |
| Player class / selection | `class = "gamemode"`, `selectedclass = 1`, `waitingtoselectclass = 0`. |
| Player spawn / infil | `waitingtospawn = 0`, `hasspawned = 0`; `br_infilstarted` is absent from the script child table. |
| Match progress | `game["state"] = "playing"`, `prematch_done = 1`, `graceperiod_done = 1`. |
| BR infil gate | `br_ready_to_jump = 0`, `br_infils_disabled = 0`. |
| C130 | `level.br_ac130` is explicitly undefined, type 0. |
| Infil structure | `c130pathstruct` exists; `playersinc130 = 0`. |
| Level callbacks | `onplayerconnect`, `onstartgametype`, `getspawnpoint`, and `modeonspawnplayer` are present as type-9 function references. |
| Actual spawn tracing | Zero native spawn-entry and spawn-return events in the accompanying server capture. |

Class selection and the grace period have completed, while the player has not
spawned and the aircraft-dependent BR gate remains closed. Generic
`infil_started = 0` and `infil_will_run = 0` are expected here:
`scripts/mp/infilexfil/infilexfil.gsc:68-74` explicitly bypasses generic
infil for BR, sets `infil_setup_complete`, and returns. They are not
additional failure indicators. Likewise, `snatchspawnalltoc130done = 1`
does not prove a player spawned; `br_infils.gsc:105-189` sets it after a
player-array loop that can be empty.

### Verified trace ABI defect and targeted repair

The startup errors in `gearing-up-active-human-0108a-server.log:10292-10300`
identify ScriptFile `1805`, offset 152, with
`size cannot be applied to undefined`, followed by bool/index errors.
The runtime byte window contains `23 07 89 03` at offset 144: a
seven-argument call to builtin `0x389`. The next error, at lines 10301-10303,
is in `br_c130.gsc`; the capture records 28 C130 route/vector errors.

The original source export is `scripts/engine/trace.gsc::ray_trace_ents`,
lines 21-35 in `references/IW8-GSC/Dumps/1.64.5/` under the repository
parent. It expects an array from this call, reads its size, then chooses the
first hit or constructs the default trace result. The decompiler's
`savepotgdata` label is a stale builtin-name mapping; the numeric call and
native bodies establish the actual cross-version contract.

| Image / builtin | Callback RVA | Verified behavior |
| --- | --- | --- |
| Source 1.64, `0x389` | `0x3065FE0` | `Physics_RaycastEnts`; validates arguments 0-5 and produces trace-result arrays. |
| Replay 1.20, `0x388` | `0x109C2C0` | Matching entity/body raycast contract and closest/all result arrays. |
| Replay 1.20, `0x389` | `0x126DFE0` -> `0x1331220` | Replay-data operation; supplies no GSC return value. |

Both raycast callbacks ignore argument 6, so the repair preserves the
seven-argument count. IDA verified their matching validation errors
`0x1366-0x1374`, query flow, and result construction. The wrong Replay
callback explains the helper's undefined result.

This helper participates in the actual Island infil path:
`br.gsc:1562-1572` builds the C130 route before waiting for prematch;
`br_c130.gsc:213-226` consumes the trace fraction and position for the
map-constrained route. Island enables that constraint in
`mp_sm_island_1.gsc:182-191`. Later, `br_infils.gsc:3391` passes the
route to `spawnc130`, and `br_c130.gsc:1073-1101` requires aircraft
progress before setting `br_ready_to_jump`. The Plunder implementation
at `br_gametype_dmz.gsc:847-850` waits for that event. This establishes
a concrete defect in a prerequisite for spawning. The corrected deployment
below subsequently reached a native human spawn and visible gameplay.

The correction is in `src/gsc/compiled/island_trace_native_abi.hpp` and
the existing linker in `src/gsc/compiled/gsc.cpp`. It accepts only numeric
module `1805`, length 3,454, and the installed converted profile's full
FNV-1a fingerprint `0xDDACFBDEF493BF6A`. It also verifies the exact opcode, argument count,
and original ID at offset 144. Only byte 146 changes, from `0x89` to
`0x88`; the resulting fingerprint must be `0x47B2E74478213597`.
The installed input bytecode SHA-256 is
`e288031196f1164096b79632e13c828770301de3c46cbede44555e324d4dc1ac`;
the translated private copy's SHA-256 is
`4189d3d38be504b7da0a4dadaa83d6bf809e5cca2bf6bad386160e8140a6ff70`.

The fixture comes from root 47, ScriptFile header physical offset 200,473,
in the installed `magma_island_scripts.ff`. Its current SHA-256 is
`d4e84049a042fa3b83e69daccc918f92eb7acce5632d2c925e3d57e8e44b3421`,
matching the package recorded by
`D:/Games/iw8x-port-stage/island-120-20261013/analysis/gsc-performance-20261018/installed-script-extraction.json`.
That extraction produced `installed-gscbin/0047_1805.gscbin`; the decoded
package SHA-256 is
`9cdbf7b6340f45743e547925802171b1471f1c7fe3ac0501cb78538a39772f56`.

The initial fixture was taken too early, from
`numeric-common-intermediates/1805.gscbin`, before relative-operand
conversion. Its bytecode SHA-256 is
`30896f2ab6c6c47a15e0681e5159f2102bc497957f1a4c1a0564d821d025f219`
and FNV-1a is `0x2942F396C178AB66`. Applying the existing
`relative_encoding.pack` conversion reproduces the installed input exactly:
70 documented operands account for all 134 differing bytes, while the
native call remains `23 07 89 03`. The corrected test retains both complete
fixtures and explicitly rejects the intermediate without changing it.

Translation runs after compaction in the existing Island gameplay-server,
server-context, active-load-epoch, loading, and no-VM-frames gate. It changes
the private compiler copy. Registered ScriptFiles, original payloads, and
global native dispatch tables retain their contents. Per-copy tracking
prevents translating already linked code; epoch changes and zone teardown
clear that state. Unexpected source content fails before reaching the native
linker, and the error is raised after releasing the loader mutex. The MP dvar
registration fix remains unchanged.

### Trace repair verification: 2026-10-22

| Check | Result |
| --- | --- |
| Corrected installed-profile `gsc_island_loader_tests` build and executable | Passed, exit 0; includes rejection of the pre-conversion intermediate. |
| `gsc_loader_tests` build and executable | Earlier run passed, exit 0; ordinary target unchanged by the profile correction. |
| `git diff --check` | Passed. |
| Independent native/code review | No substantive findings; independently confirmed the complete fixture hashes and changed offsets `[146]`. |
| Corrected installed-profile `mw120rproxy` Release build | Passed, exit 0; compiled `gsc.cpp` and linked the proxy. |
| Live trace-fix startup and human spawn | Passed: translation accepted, native `ClientSpawn` entered and returned, `hasspawned=1`, first-person world and HUD visible. |
| Full infil sequence | Subsequently confirmed working by the user; the saved automated spawn capture covers pre-match warmup. |

The focused checks in `src/gsc/compiled/tests/island_trace.hpp`, invoked
by `src/gsc/compiled/tests/loader.cpp`, cover wrong module, size,
fingerprint, opcode, argument count and native ID; rejection preserves all
bytes. They also exercise the production linker gate, exact successful
mutation, unchanged registered storage, repeated links after simulated
relocation, epoch renewal, and teardown reset. The Island test target needed
a test-only `DedicatedIslandBR()` stub to link; no production implementation
was changed for that fixture.

The corrected full-build artifact is
`xmake-out/x64/Release/island-probe/XInput9_1_0.dll`, 26,437,632 bytes,
SHA-256
`542216f81a4f45385c24e4836bd6e7f46110a5198f29f506f1dd96133d1241b9`.
The successful build log is
`build/xanim-investigation-20261021/island-trace-main-build-installed-20261022.log`.
The initial trace build, SHA-256
`f79c99cf008787a780fcf3e269c4b3a6f27ff94558e649303cf2f0966f7df939`,
used the incorrect intermediate fingerprint and rejected PID 40228's installed
script. It is historical evidence, as are the preceding XAnim-only artifacts.

The initial source/test hashes and test output are retained unchanged in
`build/xanim-investigation-20261021/island-trace-native-abi-validation-20261022.json`.
Its SHA-256 is
`7237b3a4cb0ba16d42507f6bad1930328f793bd2c9dec907efc91c4975952b86`.
That report records the earlier intermediate fixture and is historical
evidence, not validation of the installed conversion profile.

The corrected fixture, full package/extraction provenance, both input
profiles, independent conversion review, source/test executable hashes, and
successful focused test output are retained separately in
`build/xanim-investigation-20261021/island-trace-native-abi-validation-installed-20261022.json`.
Its SHA-256 is
`0e76df4ccd617f03e739670a8120e9aff2b82535a8813287e928bb94b3bf0fd6`.
The corrected `gsc_island_loader_tests` build and run both passed with exit 0.
The ordinary loader's earlier passing result remains applicable to its
unchanged source; this correction changes only probe-specific files.

The revised report also records the failed initial deployment accurately:
PID 40228 rejected the intermediate-profile guard before native linking.
Its selected buffer was unreadable after `ERR_DROP` (`ReadProcessMemory`
error 299), so no live fingerprint was obtained from that address. The
installed-stage correction is supported by the exact package hash and
independently reproduced conversion. The subsequent corrected deployment
also passed the complete runtime fingerprint guard.

The fourth-join JSON, companion server/client logs, screenshot, and capacity
sampler named above are in the existing live evidence directory below.
The corrected deployment below records the translation diagnostic, absence
of the prior startup trace/C130 failure chain, and actual human spawn with
Gearing Up dismissed. The build and `CA_ACTIVE` transitions are supporting
checks; the native spawn record and visible world establish the result.

### Corrected deployment and live retest

The corrected `542216f8...` DLL was atomically installed and hash-verified at
2026-10-22 01:35:58 UTC after both game processes had exited. The preceding
`f79c99cf...` DLL is preserved at
`backups/island-trace-installed-20261022-013557Z/XInput9_1_0.dll` under the
game directory. The adjacent ledger and
`island-trace-installed-20261022-013557Z-deployment.json` record both hashes.
The existing backend PID 18128 remained running.

Explicit Island / BR / playlist 104 server PID 34456 started at 01:35:58 UTC
with the existing numeric-canonical, source-script-memory and loadout-trace
settings. Client PID 33536 started at 01:37:12 UTC with `--client-console`.
Both were launched after verifying the same installed DLL hash. The separate
`tracefix-installed-server-launch-20261022.json` and
`tracefix-installed-client-launch-20261022.json` records preserve arguments
and timestamps. The MP dvar implementation, its regression test source, and
the unrelated dedicated lifecycle source still match the baseline hashes
listed above.

The frozen ready-server log
`gearing-up-tracefix-ready-pid34456-20261022T0141-server.log` records
the accepted `ray_trace_ents 0x389 -> 0x388` translation at line 9710,
757 completed native links at line 9929, and Island / BR server readiness
at line 10350. It contains no module-1805 or C130 runtime errors, compared
with three trace-module and 28 C130 errors in the earlier active-human
capture. Other script-port diagnostics remain, and the diagnostic logger
caps unique errors at 64; absence after that cap is not proof of success.

The active-human snapshot
`gearing-up-tracefix-active-human-pid34456-20261022T0145.json`, observed
at 01:44:41 UTC, and its companion server log establish the actual spawn:

| Observation | Saved evidence |
| --- | --- |
| Native `ClientSpawn` entered and returned | Server-log lines 12107 and 12109; slot 0, origin `(-155.3, 3242.8, 13286.3)`. |
| Native player active and spawned | Line 12129 and JSON: state 5, script object 12956, `hasspawned=1`, `selectedclass=1`, `waitingtoselectclass=0`, `waitingtospawn=0`. |
| Read-only snapshot consistency | VM idle at both observation boundaries; context identity unchanged. The external reads are not an atomic engine snapshot. |
| Gearing Up dismissed | `tracefix-installed-client-spawn.png`, captured at 01:44:56 UTC, shows the Island world, held weapon, minimap, compass and player HUD. |

The screenshot displays `Waiting for lobby to fill: 1/102`. At this capture,
`prematch_done`, `graceperiod_done` and `br_ready_to_jump` were still zero,
so the direct observation establishes a pre-match spawn. The user subsequently
reported that the infil sequence works and raised a separate defect: opened
lootboxes produce no items. That user confirmation extends the live result;
it is not represented as an automated full-round test.
The subsequent native loot-call repair, build and observed live loot
creation are documented in [island_lootboxes.md](island_lootboxes.md).

The client-capacity observation
`gearing-up-client-capacity-pid33536-20261022T014308676195Z-50039081.json`
ran from 01:43:08 to 01:46:08 UTC. It contains 8,895 samples, 32 changes,
zero read errors and a maximum sampling gap of 371.803 ms. It observed
frontend teardown to zero allocation, fresh allocation for 102 clients,
and connection state 9 with frontend state 0. Party capacity and
`ui_maxclients` remained 102. This successful join did not reproduce the
earlier 102-versus-20 rejection; no capacity repair is claimed.

Normal client logging was restored at 01:47:17 UTC, confirmed at line 3928
of `tracefix-verified-20261022-014758Z-mw120rproxy.log`. Dedicated-server
loadout tracing remained enabled. Both game processes and the existing
backend were left running. The final frozen client/server logs and
`tracefix-verified-20261022-014758Z-ledger.json` record the installed DLL,
process identities, launch arguments, evidence hashes and bounded outcome.
The full round, unrelated script-port diagnostics and the earlier
intermittent client-capacity failure remain separate validation items.

Live evidence is retained under
`build/xanim-investigation-20261021/live-20261022-000711Z/`, including the two
client captures, the completed first server log, launch metadata, and the
bounded console-input helper. The helper submits ordinary commands through
the existing `CONIN$` interface; it does not write engine memory. The first
pre-MP `xstartlobby` command was rejected and is not counted as a join. Both
counted joins used the native BR UI category/playlist selection path.

Investigation snapshots and native-code responses are retained in
`build/xanim-investigation-20261021/`. Relevant reports include
`installed-main-required-xanim-reference-audit.json`,
`installed-main-transient-registration-audit.json`,
`installed-companion-human-animation-metadata.json`,
`client-animation-ncs-29184.json`, and
`client-animation-combined-29184.json`. These are bounded read-only snapshots,
not captures taken at the exact failing instruction.
