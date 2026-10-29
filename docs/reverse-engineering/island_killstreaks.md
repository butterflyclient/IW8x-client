# Island killstreak activation

The user reported that killstreaks do not function in playlist 104 on
`mp_sm_island_1`. This investigation follows the XAnim, loot, MP dvar
registration, and client capacity fixes. It uses the installed Island
companion, Replay 1.20.4.7623265, Source 1.64.5 scripts/native contracts,
IDA MCP, and frozen live process observations.

## Causes

Two independent compatibility defects affect this path.

1. The imported shared killstreak module retains three Source method IDs
   that name different operations in Replay. Resetting and publishing the
   script's killstreak slots therefore does not update Replay's intended
   native index/availability arrays.
2. The imported BR utility defaults both WWII content switches to `1`.
   Replay's running weapon registry contains the ordinary call-in devices
   but none of the selected `_ch3` alternatives. Precision initialization
   fails while constructing its call-in weapon and leaves
   `deployweaponobj` undefined. Its aircraft settings also select the newer
   `plane_juniform87` model.

The first defect is not presented as the sole explanation of every failed
input: in the baseline, an ordinary slot-3 keypress reached the script's
use handler and incremented its unique ID, but no precision device was
equipped and the owned streak remained available. The second defect
explains why that deployment path cannot finish initialization.

## Native method comparison

Installed root 332 / numeric module `2356` is
`scripts/mp/killstreaks/killstreaks`. Its compiler bytecode has 10,906 bytes.
Offsets identify the opcode before native link relocation.

| Offset | Opcode / argc | Source ID | Replay ID | Operation |
| ---: | --- | --- | --- | --- |
| 1646 | `7E` / 0 | `872D` | `872B` | Reset native killstreak indexes |
| 5135 | `80` / 2 | `872C` | `872A` | Set a native slot's streak index |
| 5157 | `80` / 2 | `872E` | `872C` | Set a native slot's availability |

The Source and Replay callback bodies establish argument types, native
storage, and range checks. The comparison is not a blanket numerical
offset rule. Replay `872E` is `SetPowerAmmo`, with a string-slot/int-ammo
contract. Replay `872C` is availability, which accepts only `-1..1`, not
the precision index `4`. The Source reset-availability method `872F`
maps to Replay `872D`, but module 2356 contains no such call and the fix
does not introduce one.

Replay retains its native slot range `0..3` and index maximum `33`.
The runtime streak table assigns precision `4`, UAV `1`, and cluster
`14`; rows 34–37 are specialist perks. The patch does not relax native
range checks.

IDA callback evidence is in `native-contracts-summary.json` and
`ida-killstreak-contracts-20261022.json` in the task evidence directory.
The verified Replay callbacks are RVA `1207EB0` (index), `1207FB0`
(reset indexes), and `1208060` (availability). The corresponding Source
RVAs are `346F380`, `346F440`, and `346F4E0`.

## Content selection

Source `scripts/cp_mp/utility/game_utility.gsc` contains separate device
and model predicates. In BR, each calls `GetDvarInt` with default `1`:

```text
scr_br_use_ww2_killstreak_call_in_device
scr_br_use_ww2_model_swaps
```

Installed module 2244 contains the same default-one operands. The device
predicate controls precision, cluster, UAV, and related deployment
utility branches; the model predicate controls aircraft and related
effect choices.

The frozen server weapon-table scan inspected 550 slots, found 335 names,
and completed without read errors or a table-pointer change:

| Use | Ordinary registered Replay device | Native index | Selected WWII alternative |
| --- | --- | ---: | --- |
| Precision | `iw8_spotter_scope_mp` | 273 | `iw8_spotter_scope_mp_ch3`, absent |
| Cluster | `iw8_green_beam_mp` | 270 | `iw8_spotter_scope_mp_ch3`, absent |
| UAV / generic radio | `ks_gesture_generic_mp` | 275 | `ks_gesture_generic_mp_ch3`, absent |

No registered weapon name in that snapshot contains `_ch3`. This is a
bounded native weapon-registry observation, not an inventory of every DB
asset pool or archive.

The frozen startup log reports `Cannot set the attachments on null weapon`
in module 2333 at return offset 276, immediately after the precision
`MakeWeapon` call at 273. Replay native function `341` rejects an unresolved
base weapon before applying attachments. The ordinary scope plus
`spotterscope` attachment is also used by Replay's original precision
module; substituting a projectile such as `artillery_mp` would be wrong.
The existing constructor rebinding is correct and remains unchanged.

Before restart, live precision settings had `modelbase` and `modelbasealt`
both equal to `plane_juniform87`, and no `deployweaponobj`. That directly
demonstrates the selected model state. The historical device dvar value
and exact earlier constructor argument were not captured before the old
server exited. The device diagnosis combines the installed predicate,
constructor error, missing deployment object, and registered-name scan;
it does not misrepresent the model flag as a direct read of the separate
device flag.

## Targeted implementation

`src/gsc/compiled/island_killstreak_native_abi.hpp` and its neighboring
implementation accept only the exact module name, length, complete input
fingerprint, and three expected opcode/ID pairs. All sites are validated
before mutation. Only bytes 1647, 5136, and 5158 change. The entire output
fingerprint is checked, and a failure restores the original IDs.

| Identity | Input | Translated output |
| --- | --- | --- |
| FNV-1a | `D794D364148C86B9` | `9D09B63C4ED0FA5F` |
| SHA-256 | `cabe73df66f8958d1e5dc2b76065b90a8e11ac834fd93c0496808146c0a50fb2` | `77c200bef882b09eff83a4c246529a5ced8e13f0f93ce153cdb2f06bd74c7649` |

`gsc.cpp` applies the translation to the private compiler copy only during
canonical Island server loading with no executing VM frames. It tracks
translated addresses across repeat linking, then clears them on script
epoch changes and teardown. The registered asset and installed fastfile
are unchanged. Existing trace and loot translations remain in place.

In `src/dedicated/runtime/lifecycle.cpp`, Island BR builds append both
content switches with value `0` after playlist rules and before the native
`iw8_ds_config_ready` barrier. Script initialization then caches Replay's
ordinary call-in devices and aircraft. This uses normal native console
configuration and is scoped to Island BR under `IW8X_ISLAND_PORT_PROBE`.
Other map/mode configuration retains its original single final barrier.
MP dvar registration and the existing BR script-permission policy remain
in place.

`link_diagnostics.cpp` also reserves 32 additional unique error reports for
the selected killstreak modules after the general 64-event budget fills.
This retains the earlier loot budget and native error handling. It helps
observe late activation failures without suppressing them.

## Baseline evidence

The baseline server was PID 41316, started at 2026-10-22 03:52:49 UTC with
the explicit Island/BR/playlist-104 command. Client PID 31264 used the
previous combined DLL, SHA-256
`BFB1353A50FE5EBB241A6BA56C838E887CC03A86EFD0254E842DACE707E764E4`.

At the owned precision snapshot, human entity 0 had script slot 1 with
`streakname=precision_airstrike`, `available=1`, `isgimme=1`, and
`isspecialist=0`. The selected slot was 1. The level allowed killstreaks,
was outside the grace period, and had not ended. Replay's native index
array was `[0,0,0,0]` while availability was `[0,1,0,0]`.

A normal key-3 press at approximately 04:25:23 UTC changed the use-handler
unique ID from 41 to 42. The 45-second read-only sampler did not observe
successful deployment or consumption. Field membership was sampled at
about 100 ms, so an unobserved short-lived field is not proof that it never
existed.

The original general error budget had exhausted during first-player spawn.
Operator customization errors in numeric module 1919 are not labeled as
killstreak errors. The live `level.allowkillstreaks=1` observation also
prevents treating an earlier unrelated dvar permission warning as proof
that the feature was globally disabled.

The old server later recycled into `mp_donetsk` without preserving the
explicit Island override. The old client subsequently failed to open
`ww_global_cp.ff` after disconnect/frontend mode transition. These events
predate the candidate deployment. Their logs are preserved under
`predeploy-20261022T0441389676695Z`; the evidence does not establish what
initiated the client's CP selection.

## Build and deployment

Both `gsc_island_loader_tests` and ordinary `gsc_loader_tests` built and ran
successfully. The Island suite exercises the complete installed fixture,
exact output bytes, malformed profile rejection without partial changes,
private-copy ownership, repeat links, epoch reset, teardown, and excluded
map/frontend contexts. Existing loot and trace checks also pass.

The final `mw120rproxy` Release build completed successfully. Its
26,449,408-byte `XInput9_1_0.dll` has SHA-256
`D79A4AE270BDB1C9242C9137C3FAB8D16241EBA0127F848AB31F18069C337135`.
The third-party JSON source-encoding warning remains; no build failure
occurred. Build logs are retained with the task evidence.

The DLL was installed at 04:43:03 UTC using an atomic replacement after
the old game processes exited. Both installed and rollback hashes were
verified. The rollback copy is under the game directory at
`backups/killstreak-slots-20261022-044302Z/XInput9_1_0.dll`.
The backend process was left running.

Fresh server PID 36380 started at 04:43:18 UTC with:

```text
game_dx12_ship_replay.exe --dedi -bap --playlist 104 --map mp_sm_island_1 --mode br --min-players 1 +seta lui_skip_season_video 1
```

Client PID 19508 started at 04:43:19 UTC with `--client-console`. Both
launch records identify the final DLL and the existing canonical Island
and source-script-memory environment settings.

## Live verification

The user confirmed twice after deployment that killstreaks work. This
completes the live functional check of the reported failure. The client,
server, and backend were left running; further navigation and activation
tests stopped after the confirmation.

The candidate also has independent initialization and match evidence:

| Observation | Evidence |
| --- | --- |
| Both WWII selection dvars are `0`; killstreaks are enabled | `ww2-dvars-candidate-island-settings-20261022T044658222135Z.json`; native external string dvars, stable 5,560-entry registry scan, no read errors |
| Precision deployment weapon is now present | `airstrike-settings-candidate-ready-20261022T044829601457Z.json`; script weapon object 8554, class 10, reference 1 |
| Precision selects Replay aircraft | Same snapshot: `veh8_mil_air_alfa10` and `veh8_mil_air_alfa10_east` |
| Ordinary call-in devices are registered | `weapon-index-table-candidate-ready-20261022T044837246401Z.json` |
| Human reached the active round | `state-candidate-roundstart-20261022T045340469343Z.json`; prematch and infil started, grace 0, game not ended |
| Island lobby and gameplay reached | `candidate-warzone.png`, `candidate-world.png`, and `candidate-round-active.png` |

The script weapon reference `1` is not native weapon-table index `1`.
The native ordinary precision scope index is 273.

The first candidate log audit at 04:55:24 UTC has 64 server runtime-error
records and zero client records. All 64 server events occur before the
first human `hasspawned=1` marker. The old precision error at 2333:276 is
absent while the general diagnostic budget was still available. Neither
saved log contains a fatal or Com_Error. The two changed error signatures
are an existing vehicle error with new random coordinates and an operator
setup error newly visible before the budget cutoff. Complete classification
is in `candidate-log-audit-20261022T045524459985Z/`.

The final frozen logs at 04:59:31 UTC also record purchases of
`manual_turret` and `uav`, followed by a `killstreak_end` event for `uav`.
This corroborates an actual UAV lifecycle in addition to the user's
functional confirmation. Both process identities and the installed DLL
hash still matched. No fatal or Com_Error occurred in either saved log.
Seven additional server records concern missing newer throwing-knife
variants in inventory utility module 2245 and resulting boolean checks;
they are separate from the corrected precision constructor failure.

The last automated activation sample ran from 04:55:28 to 04:56:13 UTC,
before the user's confirmation. It completed 2,130 samples with stable
process, DLL, and VM identities, but did not overlap an acquired streak or
successful activation. It therefore does not independently prove native
index 4 after acquisition, consumption, plane impacts, or a UAV sweep.
The nonzero `_radarStrength` metadata entry is not an active UAV count.
Successful gameplay is attributed to the user's live test and the later
UAV lifecycle log, not to that empty-inventory sample. Every streak type,
damage result, and repeated map lifetime has not been exhaustively tested.

The diagnostic reserve remains unused in the first audit, but its general
budget is exhausted and its scoped list excludes precision adapter 2333
and utility 2244. Reports also deduplicate over process lifetime. Absence
of a later error alone is not treated as proof of successful activation.

An independent preservation review reconstructed the pre-change lifecycle
file by reversing only the intended QueueConfig delta in memory; its
SHA-256 exactly matches the frozen baseline. Ten protected production
files covering MP dvar registration, client capacity, XAnim, and prior
loot/trace fixes are hash-identical. The review record is
`lifecycle-independent-review.json`. No unrelated source changes were reset.

All investigation artifacts are under
`build/killstreak-investigation-20261022/`. The relevant prior fixes are
documented in `island_client_xanim.md`, `island_lootboxes.md`, and
`island_client_capacity.md`.
