# Island vehicle compatibility

This investigation concerns playlist 104 on `mp_sm_island_1` with the Replay
1.20 executable. The affected classes are ATV (`atango_physics_mp`), SUV
(`decho_physics_mp`), Tactical Rover (`tromeo_physics_mp`) and Little Bird
(`lbravo_physics_mp`). Evidence is retained in
`build/vehicle-investigation-20261022/`.

Current status at 2026-10-22 13:11 UTC: the permanent `renderer-r2-scope`
shader repair is built and deployed, with all three installed hashes verified.
The build passed 2,869 checks, and all 19 repaired vertex programs passed
offline WARP creation and interface checks. A fresh native playlist 104 join
completed. ATV, SUV and Tactical Rover appearance, entry, short driving,
braking and exit are verified with noclip off. The helicopter passed pilot
entry, bounded vertical ascent, controlled descent to the same pad and a
separately observed normal exit. These are short local acceptance checks;
broader handling, animation, audio and multi-client coverage remain untested.
The earlier bounded native-vertex-only substitution restored the ATV body
with the existing pixel program and `0xE01` depth state; changing depth alone
to `LESS_EQUAL` or `ALWAYS` did not restore it. That diagnostic was removed.
The installed repair also retains the physics, script and dependency fixes.
Qualified live results and their limits are recorded below.

## Confirmed causes

The original installed map contained an incomplete VehicleDef serialization projection.
`evidence/island-backport-20261013/vehicle120/structural_projection.py` creates
zeroed 3,848-byte Replay headers and copies only typed reference owners. Its
receipt explicitly says that gameplay scalar conversion is incomplete. The
original installed headers matched those projected headers byte for byte. Each header
has 1,872 omitted bytes, including vehicle type, health, physics enablement,
physics profile, wheel count and tuning.

The pre-repair main fastfile SHA-256 is
`be368a0aa50ee199e3bb8880052774c5a255193eb424fbf86c3049d1dbe4fff3`.
Its decoded payload is 207,468,569 bytes with SHA-256
`e60b0e5470435988999822e24182035686cd80b7535a4b0dd6381fefe51c6edd`.
The read-only reproduction is
`build/vehicle-investigation-20261022/audit_installed_vehicle_fastfile.py`;
its receipt is `installed-vehicle-fastfile-audit.json`.

| Class | Header physical offset | Root |
| --- | ---: | ---: |
| ATV | 121125396 | 11505 |
| SUV | 121137587 | 11506 |
| Little Bird | 121151407 | 11508 |
| Tactical Rover | 121157153 | 11509 |

At 05:14 UTC on 2026-10-22, the running server had 17 affected vehicles:
eight ATVs, two SUVs, three Little Birds and four Tactical Rovers. Every one
had physics ID zero. All four loaded definitions had `physicsEnabled == 0`
and wheel count zero. Native IDA analysis establishes the consumed offsets:
VehicleDef type at `+0x8`, physics enablement at `+0xF8`, physics netcode type
at `+0xFC`, game profile at `+0x100`, animation profile at `+0x104` and wheel
count at `+0x108`. The native vehicle-input and ground-query
functions require an actual physics instance.

The server and client asset tables contain only one instance of each
installed definition, with no intact native stash to promote. Switching an
asset-table head cannot supply the missing settings.

There is a separate script ABI mismatch. Installed module `2273`
(`scripts/cp_mp/vehicles/vehicle_occupancy`) contains 19 calls whose native
method IDs changed between the source game and Replay. These include all
five `Vehicle_GetInputValue` calls, seat-switch input, weapon flags and
vehicle customization. Their replacements are based on actual native
registration records, function bodies and archived Replay callers; a
uniform numeric offset is not valid.

Island's map script converts 40% of SUV spawns to
`open_jeep_carpoc_spawn` by default. That selects
`veh_s4_mil_ratrace_suv_wz` and `open_jeep_physics_carpoc_mp`, which are not
present in this installed content profile. The installed map bytecode and
the failing spawn log both confirm this branch.

The first live retest exposed a separate visual dependency failure. Six
techset names referenced by the real vehicle materials resolve to copies of
the engine's `null` techset. Their entire 64-byte payloads, except the name
pointer, match the owned default. Each exposes only technique type 30
(`TECHNIQUE_EMISSIVE_3ec9e10063179f3a`), without the required lit/depth
techniques. All six active DB records are in zone zero with flags five and
no stashed real definition. This is not an inference from black pixels.

The null techsets affect 16 of the rover's 46 material slots, 10 of the ATV's
61, 21 of the helicopter's 165 and 33 of the SUV's 80, counted across all
LODs. The helicopter rail/platform models have no null-clone slots. A live
screenshot shows the glass cockpit, rotors and platform while the main body
is absent. The body skin material shared by all four vehicles references
one of the missing techsets. The same fallback was present in the retained
October 16 graph, before the vehicle scalar repair.

Fourteen referenced image headers likewise match the native `$white`
default byte for byte except for the name pointer. Their nonzero image
dimensions do not establish actual vehicle texture ownership. The exact
source image roots were found in complete native `common_br_mp` (eight),
`global_mp` (four) and `global_stream_mp` (two) traversals.

The SUV also logs a missing `iw8_mp_veh_decho_door_front_left` curve. All
three requested SUV door curves are absent from both active and stashed
Replay type-87 records: back left, front left and front right. IDA shows
that the warning path first queries NetConstStrings type `0x1C`, so the
corresponding curve-name table must accompany restored curve assets.

The reviewed visual evidence is
`state-visual-dependencies-candidate-owned-defaults-20261022T065734176674Z.review.json`,
`vehicle-materials-20261022T064923543385Z.json`,
`vehicle-image-owners-20261022T065913473619Z.json`,
`state-door-curves-candidate-suv-doors-20261022T065011360072Z.json` and
`candidate-client-live-heli.png`.

## Model and damage evidence

The four main vehicle models have nondefault headers, bone arrays and
active LOD surface references. The source-only armored SUV model is absent.
The helicopter uses a composite assembly; absence of a literal composite
name from the XModel pool alone does not establish a missing model.

The captured rover is visually distorted despite an enabled driver prompt.
Retained server snapshots show all 17 vehicles at full health. Rover entity
297 remains at 1,225/1,225 health across three captures, without a scripted
destroyed flag. Normal damage depletion does not explain its appearance.
The installed XSurface conversion preserves the native rigid and blended
vertex contracts. The rover has 46 rigid surfaces and 60 rigid groups, with
bone indices 2 through 50 inside its 53 primary bones. All 23 active LOD
stream packets for the four main models decode through Replay's native
frame consumer and exactly match the expected converted source payloads.
This checks 20,363,400 decoded bytes; it does not establish correct live
GPU poses, materials or animation behavior.

The first four wheel tags on each ground vehicle resolve to actual primary
model bones. SUV door tags also resolve. The retained model PhysicsAsset
references resolve to owned, nonempty dynamic rigid-body assets. The
helicopter composite is present as XCompositeModelDef rather than a literal
XModel. The first live retest establishes accepted physics instances for
all four definitions; visible behavior is evaluated separately below.

Relevant evidence includes `state-native-vehicledefs-baseline-*.json`,
`state-vehicle-assets-baseline-*.json`, `state-model-layout-*.json`,
`state-native-vehicle-health-*.json`, `state-installed-vehicle-tags-*.json`,
`native-visual-skin-audit.json`, `native-visual-stream-audit-*.json` and
`baseline-client.png`.

## Targeted corrections

`src/gsc/compiled/island_vehicle_native_abi.{hpp,cpp}` translates the exact
20,860-byte installed occupancy module in the existing private compiler
copy. Input FNV-1a is `64B5A38DFD635FDF`; output is `486C749524D304F3`.
It changes 25 bytes: 19 verified native operands and two exact three-byte
statements. Unknown revisions fail before mutation. Native registered
ScriptFile payloads, branch offsets and field operands remain intact.

The two newer statements have no Replay native counterpart:

- The selected-horn setter releases its argument and receiver and leaves
  the discarded return value. Replay retains its original horn behavior.
- The keyboard hold-use preference query returns false, selecting the
  older keyboard exit policy. Existing gamepad and reload logic remain.

Replay VM handlers were checked for the replacement stack operations:
`0x58` releases/pops a value, `0x5C` pushes integer zero and `0x16` pushes
its byte integer operand. The complete installed bytecode fixture tests
these stack effects and loader ownership, repeat-link, reload and scope
behavior.

The same helper admits the exact 2,687-byte MP occupancy module `2441`
(FNV-1a `FBD9694D41C8026B` to `C7CB0F183793A7F2`). It changes only byte
1,782: the two-argument `IsSameWeapon` call at offset 1,781 moves from Source
function `0x364` to Replay `0x363`. This preserves the carried riot shield
selection across vehicle exit. Replay `0x364` is instead `IsNullWeapon`;
the installed one-argument null checks were already converted correctly
and remain unchanged, including the one in damage module `2269`. This
additional mismatch is not evidence for the original zero-physics failure.
Both native argument/return contracts, the IDA callback bytes and the full
installed fixture were checked. The bounded audit covered 17 vehicle
modules, 549 functions and 778 native call sites.

The Island BR startup configuration sets
`scr_percent_convert_jeeps_into_armored_suvs 0` before
`iw8_ds_config_ready`. This retains the original SUV spawns. The existing
killstreak content settings, MP dvar registration, authenticated client
capacity, loot ABI repairs and XAnim join handling are preserved.

`link_diagnostics.cpp` reserves 32 deduplicated vehicle error reports after
the general startup budget is exhausted. It retains normal engine error
handling and only adds visibility for the verified class, interaction,
damage and occupancy modules.

`tools/porting/restore_island_vehicle_scalars.py` stages the asset repair.
It requires the pinned installed archive, the reviewed projection mask and
a stock Replay archive reconstructed with verified page hashes and native
patch checksums. By default it requires a complete native traversal. Its
explicit `--native-owners-only` mode instead requires every selected vehicle
root to have completed and records the reference traversal's remaining
failure. It restores
omitted scalar bytes from the four exact same-name Replay definitions while
preserving all existing typed owners.
It verifies that every other decoded byte is unchanged, executes complete
Replay LOAD and PRELOAD, and writes a new fastfile plus a receipt.

Independent review found all 195 pointer/script-string leaves completely
inside the 1,976 preserved header bytes. The 1,872 restored bytes consist of
1,765 scalar bytes and 107 padding bytes, with no partial or unknown field.
Native reader bytes were pinned to the current Replay executable. Their
serialized child extents are fixed: four doors, twelve wheel tags, two
tread tags and 33 animation entries. Scalar restoration therefore cannot
change the child stream traversal. Independent native comparison confirms
matching ordered wheel, door, tread, animation-tree, trophy and audio-origin
tags, with all required ground-vehicle tags present in primary model bones.
All 66 VehicleAnim pointers are null in both the native and installed
definitions. The helicopter's native type 12 bypasses wheel processing even
though its raw wheel-count field is four.

| Class | Native game profile | Native animation profile | Doors |
| --- | ---: | ---: | ---: |
| ATV | 3 | 0 | 0 |
| SUV | 5 | 11 | 4 |
| Little Bird | 8 | 9 | 0 |
| Tactical Rover | 5 | 1 | 0 |

All four native definitions enable physics, use netcode type 3, and contain
four in the raw wheel-count field. Source reference recovery reached
91,842 of 91,844 Donetsk roots before a final MapEnts root reached the
offline per-invocation instruction limit and reported `loader did not
return`. All four required roots (91,538, 91,539, 91,542,
91,549) completed before that failure. The explicit owner mode uses their
verified scalar headers; it does not claim complete Donetsk traversal.
Complete LOAD and PRELOAD remain required for the modified Island archive.

The independent sound-reference comparison matches 147 of 148 named
owners. The sole difference is the retained helicopter horn alias
`veh_horn_lbravo_01`, where stock Replay has null. Its entire typed sound
group is preserved, so this is an existing additional source reference,
not a scalar/reference splice. The SUV, ATV and helicopter Havok payloads
are byte-identical to their same-name stock assets. The rover retains its
different source shape, which the first live retest accepts with a valid
physics instance.

The newer `decho_physics_nitrous_mp` definition is outside this four-name
restoration. It must not silently receive settings from another asset.

## Dependency restoration

`tools/porting/build_island_vehicle_techsets.py` recovers exactly six named
techsets from their native Source owners in `techsets_common_base_mp`,
`techsets_common_br_mp` and `techsets_global_stream_mp`. Their isolated
source graph round-trips through the native source reader before the
established Replay layout, stage, format, resource and sampler mappings.
The staged companion contains 31 roots: six techsets, 61 techniques and
25 genuine shader definitions, with no unresolved imports or extra
techsets. Sixteen programs remain byte-identical; nine receive the existing
verified sampler mapping and associated DXBC checksum update. Both Replay
loading paths consume the exact 344,188-byte decoded archive. Final PSO
identity generation is idempotent. These are structural and resource
checks; the live renderer still has to execute the programs.

`tools/porting/restore_island_vehicle_images.py` recovers the fourteen exact
image roots from complete source traversals. It changes only the image
format enum through the pinned source/Replay DXGI equivalence table. It
requires contiguous typed child reads, exact source header hashes and no
external packed references. Both native loading paths consume the isolated
14-root, 9,444-byte archive.

All 56 original image part keys are preserved. Fifty-five packets are
present in the supplied 1.64 installation. The missing leather normal-map
highest mip, key `7DD886E98D372993`, was recovered by exact content key from
the installed 1.54 `pak_compatdon4.xpak`, offset 8,621,528,704, extent
248,704. Native decompression yields 262,144 bytes, SHA-256
`656d7913e14cb2d1e1fd4a872a912abb34e0f469436f23ba0688e46d21a2e761`;
the independently recomputed content key matches. No replacement texture
or synthesized pixels are used.

The staged cache retains all 137 existing index records and all 44,766,592
existing payload bytes. It adds exactly 56 image packets, for 193 keys and
71,368,704 total bytes. Its hash is
`25ce890859dd1864846a842af279ccd0f4f7d937e8a944b3733992b31015529b`.
The native cache checks use the established 1.44 cache consumer; Replay
GPU creation remains a live validation step. Independent evidence is
`state-vehicle-cache-review-20261022T072030993370Z.json`.

`tools/porting/stage_island_vehicle_curves.py` stages the exact existing
`ncs_xac_common_br_mp` owner and the three stock Replay SUV door curves.
Source and Replay curve scalar tails and all control points match exactly;
only packed curve-name references are materialized inline. The four-root
archive is 851 bytes, SHA-256
`270b5226e8c473e8efe89c62a5bc5f9b4043b3c66489c9c77ccfa0a30eae85a7`,
and passes both native loading paths without packed references. Native NCS
aggregation chains distinct same-type owners, so the added common table
preserves the existing level table. Its source-type-one category is skipped
in frontend scenes by `IsFrontEnd` and included during gameplay; that
gameplay transition and the numeric curve cache require live verification.

`tools/porting/assemble_island_vehicle_dependencies.py` appends these roots
to the existing `magma_island_common_stream` dependency. It preserves all
28 old root entries, including their shared markers, and 33,212 script
strings. The complete native LOAD trace relocates the two downstream packed
references when the table grows by 288 bytes; the third reference into the
old root table remains unchanged. The independently pinned reconstructed
prefix must match before new bodies are appended. The final 46-root
archive passes complete LOAD/PRELOAD at exactly 2,154,721 decoded bytes,
with all retained and appended registered identities checked.

The resulting files reuse the already scheduled common-stream dependency,
its techsets companion and its existing cache name. The content manifest
and active optional-image cache size/hash constants are updated together.
The repaired main map is unchanged. All protected earlier source files
remain byte-identical except for those two cache constants and their
explanatory comment in `night_files.cpp`; reversing that exact change
reproduces the prior file hash.

## Validation status

Verified before the live retest:

- `gsc_island_loader_tests` builds and passes with both genuine occupancy
  fixtures, including exact byte changes, preserved null checks and the
  existing loot and killstreak checks.
- `gsc_loader_tests` builds and passes with the Island feature disabled.
- The final `mw120rproxy` build succeeds with the new content manifest.
- The staging tool imports successfully and exposes its command line.
- Reference-mask safety and native fixed child extents were independently
  checked against the current executable.
- Candidate LOAD and PRELOAD each complete all 11,516 assets and consume
  exactly 207,468,569 bytes without an error.
- Independent byte comparison finds 3,110 changed bytes: ATV 778, SUV 789,
  helicopter 758 and rover 785. All 207,453,177 bytes outside the four
  headers are unchanged, including the nitrous definition. Every retained
  owner byte is unchanged and every restored byte matches its native owner.
- The ten protected dvar, capacity, XAnim and prior trace/loot source files
  retain their pre-vehicle hashes. Existing killstreak integration passes
  the loader tests and independent integration review.

The DLL and map were deployed and hash-verified at 06:31 UTC on 2026-10-22.
`build/vehicle-investigation-20261022/deployment.json` records the operation.
The rollback pair is under runtime `backups/vehicles-20261022-063115Z/`.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `XInput9_1_0.dll` | 26,455,040 | `38175b616ba30f2cb67eed965b836794eea7e553d2f28030e5e56fa7e3d7c74c` |
| `mp_sm_island_1.ff` | 207,506,712 | `849bfff3dc43a653142b1c44028445700b9bd273105bc40ecce5b2258008a09a` |

The visual/curve dependency revision was deployed and hash-verified at
07:27:45 UTC. `vehicle-visual-deployment.json` records the four replacements
and rollback directory `backups/vehicle-visuals-20261022-072743Z/`.
`vehicle-visual-build.json` records the rebuilt DLL, unchanged main map and
protected-source comparison. The production DLL build passed; the existing
`custom_fastfile_tests` suite passed all 2,869 checks for registry, native
routing, stock fallthrough, dependencies, caches and pools. The GSC code
has not changed since the successful loader suites above.

| Visual dependency revision file | Bytes | SHA-256 |
| --- | ---: | --- |
| `XInput9_1_0.dll` | 26,455,040 | `30327a72b7f648b8d2d99974200c7d94d5213bc852d74b9ad0013ac6bdcf41da` |
| `magma_island_common_stream.ff` | 2,155,268 | `928985e53898a66ea778c262e1f30cbaa4f8946e5bc8883b18665d089f203984` |
| `techsets_magma_island_common_stream.ff` | 344,408 | `5d5bd7b0a9db8c542ca144557624a2afc7c3c39b82b07c7ccc13adf460b9cd70` |
| `cache_magma_island164_common.xpak` | 71,368,704 | `25ce890859dd1864846a842af279ccd0f4f7d937e8a944b3733992b31015529b` |

The fresh server snapshot at 06:39 UTC contains 18 vehicles (eight ATVs,
three SUVs, three helicopters and four rovers), all with valid physics IDs
1 through 18 and the intended profiles. Startup health is 975 for ATVs,
1,975 for SUVs, 3,475 for helicopters and 1,225 for rovers. The previous
unsupported armored-SUV spawn failure is absent. Both occupancy-module
translations are logged, with no original vehicle physics/occupancy error.

A passive live sample from 06:45:07 through 06:45:53 proves helicopter
entry, exit, re-entry and flight. The native pilot link and the player's
vehicle link agree; both clear on exit and return on re-entry. The sampled
flight covers approximately 15,349 engine units, with altitude changing
from 1,071 to 2,060. Noclip remains disabled throughout. This proves real
vehicle movement and occupancy, not correct rendering. The accompanying
screenshot still shows the missing helicopter body.

The retained samples are
`state-native-vehicledefs-candidate-startup-20261022T063939445913Z.json` and
`state-vehicle-sample-candidate-natural-flight-20261022T064553577937Z.json`.
The vehicle skeleton scalar prefixes still match the source, and all six
rover bone arrays compare exactly (`native-model-pose-audit-20261022T065005651239Z.json`).

## Visual revision live retest

The 07:27 visual revision was tested with server PID 35892 (started
07:28:11.7905283 UTC) and client PID 36920 (07:28:46.0929832 UTC), both
using the recorded `30327a72...cf41da` DLL. From the loaded MP frontend,
`mm_start 104` selected the playlist and the ordinary `xstartlobby` command
started native matchmaking. The client entered Island Plunder, preserving
the existing 102-client allocation. The post-join runtime/crash snapshot
through 07:45:07 contains no fatal XAnim error, engine drop or recorded
vehicle-owner GSC failure; both current crash files are empty.

Both processes own all six restored technique sets, 61 techniques and
91 nonzero returned graphics-pipeline objects. Their complete key
multisets match the staged candidate. The eight logged cached-pipeline
rejections occurred at the first native creation attempt (`0xECBD10`).
IDA and pinned executable bytes prove that Replay clears the cached blob
and retries; the stable returned objects establish recovery in both
processes. These messages are not final pipeline-creation failures.

The client material graph has the six intended technique-set replacements
and fourteen intended image replacements, with all 56 original stream
keys. Both server and client also have the three Decho door curves as
active assets. The gameplay NCS aggregate contains 13 curve names and
maps the restored names to native IDs 20, 21 and 22. This establishes
dependency presence, not a visual door-animation result.

The live rover view now has the complete body, cage and wheels, but those
surfaces are solid black. `visual-retest-rover-0748.png` and
`visual-retest-rover-driving-0750.png` retain that failure. Rendering is
therefore not accepted at this revision despite successful asset loading
and pipeline construction.

Two read-only 45-second samples establish rover driver entry, linked
movement and exit. The player's vehicle reference and the rover's driver
reference agree after entry at 07:48:13; both clear at 07:49:55, and the
player returns to ordinary on-foot movement. Noclip is disabled throughout,
physics ID 12 remains valid, and health remains 1,225. The sampled occupied
segments cover approximately 138 and 174 engine units. Input attribution
is not inferred from movement. A three-second `W+D` attempt was interrupted
by the foreground guard and is not counted as a completed steering test.
Neither sample contains a new ERR/GSC error. The first contains ten
`GetPlayerData(...vehicleCustomization...tracks...)` messages at INFO;
the second contains none.

Relevant retained reports are `state-vehicle-pipelines-visual-client-gameplay-20261022T074555143713Z.json`,
`native-client-visual-dependency-comparison-20261022T074919347780Z.json`,
`script-visual-runtime-review-20261022T074507244699Z.json`, and the two
`state-vehicle-sample-visual-rover-*` reports.

## Opaque vehicle depth comparison

The user's map-techset lead matches the current vehicle state pattern.
Five opaque restored sets use an EQUAL/no-write colour pass (type 34)
after a GREATER_EQUAL/write prepass. The ordinary and velocity prepasses
are types 0, 3 and 6. Replay's packed depth comparison is independently
decoded from native instructions; opaque blend zero already enables the
full RGBA write mask. The translucent sixth set has no such pairing and
is excluded from the proposed correction.

The retained map evidence supports moving depth ownership to the opaque
colour pass: prepasses retain GREATER_EQUAL but stop writing depth, and
the colour pass uses GREATER_EQUAL with depth writes. The earlier map
receipt records partial rooftop success for that strategy. It does not
establish universal visual correctness. The earlier ALWAYS diagnostic
introduced visibility artifacts; the LESS_EQUAL trial ended in the lobby
and is explicitly not rendering evidence. These boundaries are recorded
in `state-historical-depth-acceptance-review-20261022T075641574Z.json`.

The vehicle-only candidate applies those paired states while retaining all
stencil/cull bits, shadow and unrelated passes, transparent techniques,
shader programs, texture bindings and geometry. Exactly 20 state fields
and 35 regenerated pipeline keys account for all 298 changed decoded bytes.
The other 56 key records remain identical. Both full native loading paths
complete all 31 roots at the exact 344,188-byte EOF, and final key generation
is idempotent. The independent byte-scope review is
`state-vehicle-depth-review-20261022T080329807592Z.json`.

The shipping native pipeline constructor was executed offline for all 35
affected format variants. Prepass descriptors differ only in depth writes;
main descriptors differ only in depth writes and the EQUAL-to-GREATER_EQUAL
comparison. Every other descriptor field is identical, including colour
write masks. `native-vehicle-depth-descriptor-review.json` records the
constructor and IDA byte checks. The conversion is reproducible through
`tools/porting/build_island_vehicle_techsets.py --baseline-dir` against the
hash-pinned initial restoration, or through its original Source extraction
path with the same depth finalization.

The matching DLL build and all 2,869 `custom_fastfile_tests` checks pass.
All ten protected source files retain their visual-revision hashes. The
DLL and techset were deployed and hash-verified at 08:07:53 UTC, with the
main map, common-stream asset archive and image cache unchanged.
`vehicle-depth-build.json` and `vehicle-depth-deployment.json` retain the
receipts. Rollback files are under runtime
`backups/vehicle-depth-20261022-080753Z/`.

| Latest file | Bytes | SHA-256 |
| --- | ---: | --- |
| `XInput9_1_0.dll` | 26,455,040 | `a2b799d4e2ecbbe6b95522d718623af17e2909f3dee3b1204014567ef138b225` |
| `techsets_magma_island_common_stream.ff` | 344,408 | `19ac07fbc62eb8f91c06692a182d3898804b3459aedabddf2e4e10307baf36be` |

## Depth revision live result

The depth revision was tested with server PID 12540 (started
08:08:27.5916395 UTC) and client PID 34768 (08:09:03.2236508 UTC). The
ordinary `mm_start 104` / `xstartlobby` route reached Island, warmup and
infil without a fatal XAnim error or client-count rejection. Both processes
owned the expected 91 pipeline objects and keys, and all 20 modified
state/blend fields matched the candidate. The client material graph was
stable at six models, 37 materials, 26 technique sets and 101 images.

Rendering **failed** this comparison. The ATV, SUV and Rover bodies remain
black; the helicopter has shaded glass, steps and rotor parts but a black
main body. The retained images are `depth-retest-atv-front.png`,
`depth-retest-suv-front.png`, `depth-retest-heli-front.png` and
`depth-retest-rover-front.png`. The depth-only change is not a verified
visual fix, and successful pipeline creation is not evidence that a shader
reads compatible renderer data.

The same round provides these separate functional results. Noclip was used
only for travel between tests and was disabled for every control sample.

| Vehicle | Verified result | Evidence limit |
| --- | --- | --- |
| ATV 297 | Driver entry, movement and exit; about 678 units net displacement and health 975 retained | The 45-second sample covers entry. Later guarded controls and a separate post-capture establish movement/exit; steering and braking were not continuously sampled. |
| SUV 302 | Entry, at least 1,447 units of occupied movement, and exit; health 1,975 retained | The full control sequence is continuously sampled. Black surfaces prevent full door-animation acceptance. |
| Helicopter 306 | Pilot entry, airborne movement in the flight image, and exit; about 1,555 units before/after displacement | The flight inputs follow the entry sample. Later health is 3,128/3,475; the damage source and a controlled landing were not established. |
| Rover 305 | Driver entry and at least 1,724 units of movement | The route crossed the coastal combat boundary. The vehicle reference was recycled, so this is not a clean exit pass. |

The Rover's last valid sample retains physics ID 15 and health 1,225 while
its altitude falls from about 496 to 122. The driven image shows the
out-of-bounds countdown above the coast. A subsequent guarded capture has
the same VM epoch and Island world, with only this vehicle absent from the
17 remaining vehicles. No new ERR or GSC runtime record accompanies that
interval. This supports a test-route failure; the exact deletion callback
was not recorded. The next Rover retest must use an inland route.

The control evidence is retained in the `state-vehicle-sample-depth-*`
reports, `depth-suv-control-sequence-20261022T083007151Z.json`,
`depth-rover-control-sequence-20261022T084121368Z.json`, and their companion
images and post-control captures. The post-join log/crash snapshot is
`script-depth-runtime-review-20261022T081855624314Z`; both current crash
files were empty. Fourteen apparently new GSC diagnostic sites in modules
1893/1919 are present verbatim in earlier frozen logs, rather than a new
depth-revision failure.

## Remaining shader renderer ABI mismatch

The black-body investigation found a concrete omission in the initial
six-set restoration: its shader programs received sampler remapping but
not the renderer data-layout conversion already used by the map audit.
A restored body pixel shader still declares and reads entity buffer t48
with Source's 268-byte stride; Replay supplies 184-byte records. The
shared body vertex program also retains Source t63/t64 metadata/attribute
addressing and shifted scene-buffer fields. A valid graphics pipeline does
not validate those data contracts.

The material-local constants are independently ruled out as corrupted
inputs: all nine examined non-collision ATV/Rover materials have the exact
195-byte technique index table, four stage sizes and all 720 payload bytes
from their Source owners. Executing the pinned native selector in isolated
memory verifies that technique 34 selects the pixel descriptor at slot 3
and sets its dirty bit; technique 3 correctly skips index 255. This proves
CPU data and selection behavior, not actual GPU upload or draw-time state.
The reports are `script-material-buffer-comparison-20261022T082842517425Z.json`
and `script-material-buffer-selection-proof-20261022T083632579279Z.json`.

The scoped renderer conversion has been staged against all 24 nonempty
restored programs. Eighteen match historical audit input hashes; the six
additional pixel programs use only scene/entity fields covered by the
existing native proofs. All actual t63, t64 and t48 loads are counted from
DXBC tokens and matched to their plans before conversion. The current
depth states remain fixed for this comparison. Offline validation passes;
the matching DLL rebuild, deployment and fresh live visual acceptance are
not established by this candidate receipt.

## Renderer projection candidate

`tools/porting/island_vehicle_shader_projection.py` applies the audited
`render_audit/rewrite_renderer.py` contracts to the exact restored program
identities. It pins the complete input and output hashes, shader stages,
existing proof files and conversion implementations. The six additional
pixel shaders introduce no new field mappings. Their entity reads are
restricted to offsets already consumed and mapped in the earlier native
audit. Unknown programs, changed bytes, missing load plans and unclassified
fields fail before an archive is emitted.

| Contract | Verified candidate scope |
| --- | --- |
| Unified model metadata, t63 | 33 loads use the established Replay record/table addressing and consumed flag layout. |
| Unified model attributes, t64 | 25 loads classified: 13 position loads retain their addresses; 12 attribute loads receive the existing Replay address conversion. |
| Per-entity data, t48 | Three declarations change stride 268 to 184; 18 reads use proved native fields or the existing identity atlas/no-animation policy. |
| Scene buffer, CB7 | 544 operands processed against the existing field plan; 10 operands use its explicit fallback values. Dynamic or unclassified indices are rejected. |
| Colored prepass outputs | Eight pixel programs exchange normal and motion/object-ID outputs with matching output signatures; only the proved literal-zero extra normal lane is omitted. |
| Prepass attachments and masks | All 30 colored prepasses, types 0/3/6/18/21/24, receive the coupled format rows and inverted color-write masks. |

The prepass conversion maps format rows `[22,18,6,0,0]` and
`[22,18,16,0,0]` to `[22,6,17,0,0]` and `[22,16,17,0,0]`, respectively.
Normals move from output 0 to Replay's single-component output 1; motion
and object identification move from output 1 to output 0. Their signature
records remain in hardware-register order, as required by the earlier
native shader-consumer proof. Depth/stencil-only passes have no colored
attachment conversion.

Every one of the 61 depth/stencil/cull words is byte-identical to the
depth-only baseline, including its 20 adjusted depth states. The existing
material-local CB3 declarations and operands are also preserved. The
typed-read comparison admits only the selected shader programs and their
size/checksum fields, paired prepass format/write-mask fields, relocated
packed references and final pipeline keys. Shader names remain unchanged;
other resident materials owning those exact names share their programs.

The explicit field policies are retained from the map audit: Source scene
fields 196/197 use the authored Island sky values, field 221 uses the
neutral FOV-distance multiplier, and Source-only entity atlas controls use
identity scales and zero frame words. The selected native producer proofs
do not claim exhaustive numerical equivalence for every renderer field.
These qualifications remain relevant to the live visual test.

The candidate is retained under
`build/vehicle-investigation-20261022/native-techset-candidate-renderer-20261022/`.

| Candidate artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `techsets_magma_island_common_stream.ff` | 350,376 | `57f920b9fb542f4140b34fbb224ffe882bf2eab6d5e0a46f3a04c42884f3e950` |
| Decoded companion payload | 350,156 | `2fd8b6ef532299893f82febae6e94120362ed4c1d240eb310298b0c8174dc0bf` |

Complete Replay LOAD and PRELOAD each consume all 31 roots at the exact
350,156-byte EOF: six technique sets and 25 shader definitions, including
the unchanged empty pixel shader. All 24 nonempty programs change and
retain valid DXBC checksums and all unrelated chunks; only the eight
prepass output signatures are additionally edited. Final PSO identity
generation changes all 91 key records, produces 40 unique nonconflicting
identities, and is idempotent after a full native reread.

Seven negative cases pass: an unknown shader name, wrong stage, changed
program byte, emptied model-load plan, removed used scene mapping,
unmapped entity field 220 and an already-projected input are all rejected.
Independent review verifies the final program hashes/CRCs, unchanged CB3
operands, all 30 attachment/mask pairs, all 61 depth words, all 91 stored
key values and runtime-zero fields, and both full native trace hashes.
That review does not independently recompute PSO identities from resident
pointers; the native-reread idempotence check supplies that separate proof.

Reproduce from the project directory using the exact retained depth
baseline and a new output directory:

```powershell
py -3 tools\porting\build_island_vehicle_techsets.py `
  --renderer-baseline-dir build\vehicle-investigation-20261022\native-techset-candidate-depth-20261022 `
  --output-dir build\vehicle-investigation-20261022\native-techset-candidate-renderer-repeat
```

The tool requires the pinned depth fastfile `19ac07fb...baf36be`, its
decoded payload, receipt and source ownership plan, and the current Replay
executable hash. It writes a new candidate directory only. The primary
receipts are `techset-restoration.json`, `renderer-projection.json`,
`load.trace.json` and `preload.trace.json`. Compact handoff and rejection
evidence are `native-vehicle-renderer-summary.json` and
`native-vehicle-renderer-guard-checks.json`; the independent review is
`script-renderer-candidate-review-20261022T085259904393Z.json`.

This establishes an offline renderer ABI candidate. It does not supersede
the failed depth-only screenshots or the qualified functional results
above. Correctly shaded/textured vehicle bodies still require a fresh
matchmaking join and live observation with the matching deployed DLL and
companion fastfile.

## Renderer revision build and deployment

The matching `renderer-r1` DLL build completed successfully in 19.016 seconds.
`custom_fastfile_tests` passed all 2,869 checks. The ten protected source files
and the three existing map/image dependencies remained byte-identical to the
depth baseline; the admission manifest now requires the 350,376-byte renderer
companion and its exact hash. Build results and actual command exit codes are
retained in `vehicle-renderer-r1-build.json`, `vehicle-renderer-r1-checks.json`
and their build/test logs.

The installed DLL is 26,455,040 bytes with SHA-256
`eb3facbeedbe7d580ad9e4b6cf25507995afe01ece9a34055928fe86824c3ad0`.
The installed companion is the reviewed `57f920b9...84f3e950` candidate.
Both final hashes and both original backups were verified at
2026-10-22 09:01:31 UTC. The backup directory is
`backups/vehicle-renderer-r1-20261022-085836760Z` under the runtime root.

The authoritative completion receipt is
`vehicle-renderer-r1-deployment-completed.json`. The initial deployment stopped
after replacing the DLL: PowerShell 5.1 coerced the null backup argument of a
receipt-file `File.Replace` call into an invalid path, and the same bug prevented
its rollback. Read-only hashes established the exact new-DLL/old-fastfile state,
with the original DLL backup intact and no running game process. The guarded
completion changed only the still-pending fastfile, used a non-null backup path,
and verified the full pair and preserved dependencies. The original attempt's
stale `prepared` receipt and intermediate file are retained as failure evidence;
they must not be mistaken for the final installed state.

The previous client and recycled server closed normally. The old dedicated
process 12540 had already completed its match and recycled into process 31656;
its complete log was preserved from `.dedi.previous.log`. All three matching
crash logs were empty. The client `EXE/DISCONNECTED` event coincides with that
normal server recycle and is not attributed to a renderer crash.

A fresh Island server 10996 started at `2026-10-22T09:02:05.2974649Z`, followed
by client 21812 at `2026-10-22T09:02:06.8122707Z`. Both use the verified renderer
DLL. Matchmaking reached Island warmup at 09:10 UTC and the normal infil
sequence completed. The client and server both contain all 91 native pipeline
objects with the expected final keys and 20 modified depth/blend states.
Post-join logs contain no new asset, model, XAnim fatal or vehicle-owner script
errors, and both crash logs are empty.

At 09:15 UTC, `renderer-r1-atv-front.png` still shows a solid black ATV body.
The player was on foot with noclip off, facing entity 300, script object 13158;
the vehicle had 975/975 health, a valid physics instance and empty occupants.
Entering and driving it was not tested by the 09:23 attempt: its helper rejected
the legitimate pair of game-directory and System32 XInput DLLs before sending
any input. The associated 180-sample observer report consequently records only
the idle state and must not be counted as an entry/driving test.

`state-renderer-r1-atv-anchor-20261022T0928Z.json` records the material owners,
constant-buffer descriptors and native format rows. All four ATV main34
techniques have two render targets and format row `[22,32,1,0,0]`. The body
uses state `0xE01`, the wheels `0x801`, and glass/decal `0xC01`; the body color
write mask is already `ALL`. All four pixel constant-buffer descriptors are
present. These observations establish resident ownership, not the technique
actually selected at a draw. The next discriminator is a bounded observation
at native `R_SetPipelineState` RVA `0x1989850`, followed by a body-only depth
comparison through native-built pipeline variants with restoration after each
matching draw.

## Renderer revision live controls and draw evidence

The 09:33 ATV test is a separate successful run after correcting the input
helper's exact-module-path check and moving into the observed entry range.
`renderer-r1-atv-entry-range-20261022T093255690Z.json` records the normal
enter, forward, brake and exit inputs. The independent 180-sample observer
`state-vehicle-sample-renderer-r1-atv-entry-range-20261022T093330611622Z.json`
covers every action with noclip off, stable vehicle ownership and 975/975
health. Both player and vehicle links confirm entry and subsequently clear
on exit. Sampled path length is at least 171.115 units, with 169.112 units
of net movement. The occupied and exited screenshots still show a black body.
The log review found no new ERR lines or crash output, but did retain ten
INFO-wrapped vehicle-customization `GetPlayerData` track errors; those are
not silently counted as clean script output.

That match ended through the native timer and server recycle at 09:42 UTC.
`state-renderer-r1-retired-freeze-20261022T094646210344Z.json` preserves the
old server, recycled child and client logs and their empty crash logs. The
client disconnect coincides with the normal server end/recycle. The recycled
child closed normally before starting server 34684 at
`2026-10-22T09:48:07.3744840Z`; client 21812 was retained. After server readiness,
the ordinary console set `scr_br_timelimit` to 7200. The native dvar and script
watchdvar both read back 7200 before joining, and the fresh gameplay HUD showed
115:06 after infil. No DLL or fastfile changed for this rejoin.

The fresh registry at 10:03 UTC contains 18 vehicles at full health. The new
visible target is ATV entity 294, script object 13037, not the previous match's
entity 300. `vehicle-materials-20261022T100349643001Z.json` records its current
client ownership graph. After navigation, the player returned to normal
movement with noclip off. `renderer-r1-new-atv-comparison-view.png` shows the
whole black ATV body in a stable on-foot view.

The first bounded native-only draw probe completed at 10:10 UTC. Its receipt,
`native-vehicle-depth-probe/vehicle_depth_probe_20261022T101005180896Z.json`,
records 3,705 matching `ExecuteIndirect` submissions through caller RVA
`0x19875DD`, selecting the exact owned main34 technique and state `0xE01`.
There were no ownership guard rejections. Pixel-b3 CPU descriptor and final
hardware shadow both contain GPU address `0xC00699000`; this is not an
interception of the actual GPU root-binding call or proof of executed pixels.
Callbacks drained, hook entry bytes were restored, and the native asset state
remained unchanged.

This first probe is not an accepted visual comparison. All observed format
rows were zero and 7,461 format checks rejected; no controlled draw ran.
The capture launcher also encountered a sharing violation while reading the
native progress file, so it produced no phase-qualified screenshot. Both
diagnostic issues are being corrected before comparing `ALWAYS` or
`LESS_EQUAL`. The completed native receipt is retained rather than replayed
or counted as a successful comparator test.

## Qualified shader comparisons

The second native-only observation corrected the format reader and captured
the actual main34 binding: one active color target, declared count two, DSV
and color formats `[22,32,0,0,0]`, valid handles `[198,125,0,0,0]`. IDA and
shipping bytes show that `R_SetPipelineState` skips format matching when the
technique has one format row; when matching multiple rows it compares
`min(active,declared)+1` bytes. The original asset row `[22,32,1,0,0]` was kept.
The final probe gate admits only this observed binding with all handles and
read validity checked. The progress writer now permits shared reads, and the
launcher retries a transient sharing violation before its existing deadline.

Four phase-qualified comparisons completed with exact asset identities,
zero guard/format rejections, successful native PSO construction, drained
callbacks, matching controlled/restored counts and unchanged native state:

| Mode | Completed controlled/restored submissions | Visual result | Evidence stem |
| --- | ---: | --- | --- |
| `ALWAYS` (`0x201`) | 3,177 | Black body | `native-vehicle-depth-probe/vehicle_depth_probe_20261022T102659364416Z` |
| `LESS_EQUAL` (`0x601`) | 3,118 | Black body | `native-vehicle-depth-probe/vehicle_depth_probe_20261022T102751165766Z` |
| Forced RGB magenta, `0xE01` | 3,117 | Black body, no magenta | `native-vehicle-color-probe/vehicle_color_probe_20261022T102848317400Z` |
| Forced RGB magenta, `ALWAYS` | 3,162 | Black body, no magenta | `native-vehicle-color-probe/vehicle_color_probe_20261022T102957909383Z` |

Each stem has a native `.json` receipt, a `.capture.json` phase receipt and a
`.capture.png` screenshot. The independent review is
`state-completed-vehicle-probe-comparisons-20261022T1033Z.json`. The forced-color
program preserves the original instructions and sets RGB immediately before
the final return. Its failure does not distinguish vertex coverage, discard,
zero indirect work or later overwriting. Submission counts alone are not
counts of GPU-executed primitives or fragments.

The authenticated `techsets_mp_donetsk` and `techsets_common_base_mp` archives
completed unchanged native readers at exact EOF, with 23,987 and 754 roots.
For `mo/veh8_mil_lnd_atango_skin`, its material semantic header, four texture
records and names, four constants, 195-byte buffer selector, buffer definition
and 64-byte pixel payload match Replay. All 12 technique argument arrays and
counts also match. Native and current input layouts are the same zero-attribute
16-byte blob. The native main shader pair uses declaration slots 5 and 6;
the old conversion expanded the declaration table to all 24 slots. The native
and current techset flags differ by bit 36, but the successful pair test below
retains the current flags, so this bit is not a blocker for the tested body.

The separate frozen native-pair diagnostic has DLL SHA-256
`8b8a1417812f1c1ec029926439fc47f4077bb46158fc3f49139a16601243727a`.
Its shipping-constructor proof changes only vertex/pixel bytecode pointers and
lengths in the 656-byte descriptor. The actual test keeps depth `0xE01`, the
current material, arguments, input layout, formats and prepasses. It admits
only actual declaration 5 or 6 and restores the original PSO after each draw.

`native-vehicle-replay-shader-probe/vehicle_replay_shader_probe_20261022T104818806991Z.json`
completed with status zero, 3,093 controlled/restored submissions, declaration
5 in every recorded observation, zero rejections and complete cleanup. Its
capture at `2026-10-22T10:48:26.7135481Z` lies wholly inside the controlled phase
and shows textured body paint, seat, racks and detail on the previously black
ATV. Both wheels remain black. Image SHA-256 is
`f4ba82e3ff669cb99d421d1c81e145ed469b8b959134af6c295ffaee37f9812d`.
Independent review: `state-native-pair-success-review-20261022T1052Z.json`.

The subsequent read-only entity capture
`state-client-dobj-renderer-r1-atv294-20261022T105410829602Z.json` ties client
entity 294 to DObj index 6, its one `veh8_mil_lnd_atango_physics_mp` model and
the exact body material owner used by the successful test. All 61 material
slots resolve to the captured body, wheel, glass/light and decal owners.
Unclassified DObj fields are not evidence that material overrides are absent.

## Vertex-only isolation and complete shared-program scope

The subsequent diagnostic keeps the Source pixel program byte-identical. Its
native vertex program changes only output semantic `NORMAL` to `NORMAL_TRIID`
at the same register/type/mask; every native instruction remains exact. The
shipping `ECB4D0` constructor produces a 656-byte descriptor differing only in
the VS bytecode pointer/length. D3D11 WARP accepts the original Source, native
and adapted native programs. See
`native-vehicle-replay-vertex-probe/vertex-only-review.json` and
`vertex-native-descriptor-proof.json` in that directory.

`native-vehicle-replay-vertex-probe/vehicle_replay_vertex_probe_20261022T111333619786Z.json`
completed with status zero, 2,979 controlled/restored submissions, no guard,
format or declaration rejections, and full cleanup. The screenshot capture
interval `[159197296, 159199156]` lies wholly inside the controlled phase
`[159194109, 159206109]`, after its first completed controlled draw. It shows
the textured ATV body, seat and racks; both wheels remain black because the
diagnostic selects only the body material. Image SHA-256:
`ce3f4265b5965cacf4f37fc40756fcd5ba8f16bc4438c42fdaf19c31ea6476e7`.
Independent review: `state-vertex-only-live-review-20261022T112413370988Z.json`.

The converted vertex instructions still interpret CB0 as compact quaternion
plus translation/scale instances. Six companion programs declare 258 float4
rows and index two rows per instance; their Replay counterparts declare 514
and use four matrix rows per instance. The velocity variants similarly use
514/515 Source rows versus 1026/1027 Replay rows. This is a representation
mismatch, so changing only the index stride or depth comparison is insufficient.
Exact programs and disassembly are retained in
`native-cb0-inventory-20261022T111454313888Z/inventory.json`.

The authenticated native contracts cover all 19 current Source VS identities:
`script-vertex-restoration-contracts-20261022T111720930931Z/manifest.json`, SHA-256
`d40657dc9dca1c5f9ce60bf00d815d2536a338998d4a3fbbdbc6dd4bd3bae3d3`.
Seventeen have matching interfaces with only explicit normal-semantic renames.
The helicopter-interior and glass variants emit three Source SH-lighting
vectors where their native counterparts emit a light-grid-volume coordinate;
they require an audited instance-transform repair retaining Source lighting.

`script-vertex-all-user-bindings-20261022T112021983518Z.json` verifies the actual
typed binding arrays for all 966 map uses and 61 companion uses. The map scope
is 102 affected families and 122 material names, including 106 outside the
captured vehicle-material set. These are existing users of the same shader
definitions. All required native VS bindings are present; the two extra
Source lighting bindings stay untouched. The selected map pipelines have 655
uses of 85 complete pixel programs and 311 explicit null-pixel uses, with no
named unresolved shader imports and no hull/domain programs. See
`state-map-vehicle-shader-closure-20261022T112236249024Z.json`.

## Permanent instance-transform repair: offline validation

[restore_island_vehicle_vertex_programs.py](../../tools/porting/restore_island_vehicle_vertex_programs.py)
repairs the 19 exact vertex-program identities in both
`techsets_mp_sm_island_1.ff` and `techsets_magma_island_common_stream.ff`.
It requires the pinned native contracts and the complete shared-user binding
proof before generating replacements. This is a shared-program repair: the
966 map uses span all 102 affected families and 122 material names, including
106 names outside the captured vehicle set. The 61 companion uses are checked
separately. Visual acceptance must account for this wider scope.

| Programs | Repair | Preserved contract |
| --- | --- | --- |
| 17 matching interfaces | Use authenticated Replay instructions; adapt only reviewed `NORMAL` output names to `NORMAL_TRIID` where required | Exact native instruction chunks; current input/output registers, types and masks |
| Helicopter-interior `86247...` and glass `be81...` | Replace the compact instance transform with the Replay matrix calculation | Exact Source signatures, SH lighting calculations and remaining instruction bytes |

The two exceptional programs use
[island_vehicle_instance_transforms.py](../../tools/porting/island_vehicle_instance_transforms.py).
Its complete input/output fingerprints and exact instruction-span guards admit
only the reviewed revisions. It replaces 41 quaternion-transform instructions
with 39 matrix-transform instructions, changes the instance stride from two to
four rows and CB0 extent from 258 to 514, and adds four private temporaries.
Position, normal, tangent and self-visibility direction follow the native
matrix calculation; the independent self-visibility amplitude is preserved.
Source SH outputs remain intact, so their existing pixel programs and lighting
bindings remain usable. No material, pixel program, argument or depth/blend
change is part of this repair.

The rewriter passed eight negative guards and idempotence checks. Execution of
the actual emitted and authenticated native arithmetic in a scalar CPU model
matched 34,816 components across 2,048 cases, including changing origins,
rotations, scales and 512 general affine/reflected matrices. Path-aware register
liveness and the unchanged SH suffix were independently reviewed. This is
offline arithmetic evidence, not a capture of the game's GPU constant buffer.

The production tool's actual 19 outputs then passed WARP `CreateVertexShader`
with exit code zero and 19 successes. All current shader interfaces match;
every non-OSGN native chunk is exact for the 17 substitutions, and every
non-instruction Source chunk is exact for the two transform repairs. The
candidate's 19 emitted files also compare byte for byte with those reviewed
outputs, including hashes, sizes, CRCs and transformation proofs.

Archive staging requires complete native LOAD/PRELOAD at exact EOF and a
fastfile decode round trip. Its preservation gates compare registered asset
identities, typed allocation owners/order, native events and insertion owners.
Every packed reference must retain the same source allocation and field offset,
target allocation and offset, target block and indirection; pointer bytes may
change only to preserve that ownership after relocation. All other typed bytes
must match the declared shader edits. Only pipelines using the changed vertex
programs receive new keys, with complete coverage, collision rejection and
idempotence required. Staging and an independent audit passed these gates for
both final archives. The independent audit also checked untyped gaps, all
68,634 map packed references and 178 insertion identities, and confirmed that
all 5,078 unrelated map pipeline records remain byte for byte unchanged.
Archive validation does not establish complete live visual acceptance.

Evidence under `build/vehicle-investigation-20261022/`:

- [Two-program handoff and validation](../../build/vehicle-investigation-20261022/native-compact-transform-candidate-20261022T112254802159Z/handoff.json).
- [Independent transform review](../../build/vehicle-investigation-20261022/script-instance-transform-review-20261022T112713397315Z.json).
- [Final 19-program WARP/interface receipt](../../build/vehicle-investigation-20261022/native-final-vertex-validation-20261022T113200770485Z/final-program-validation.json).
- [Candidate program equality](../../build/vehicle-investigation-20261022/native-staged-vertex-equality-20261022T114206594841Z.json).

## Required Island build profile

Configure this Island revision explicitly with `--custom_content=n
--island_port_probe=y`. The general `custom_content` option enables additional
converted frontend and Night-map code; it is independent of the Island probe.
The working renderer-r1 DLL excluded that optional content. Preserve this
profile when rebuilding the DLL for the shader manifest changes.

The first renderer-r2 build mistakenly used `--custom_content=y`. Its DLL grew
393,216 bytes: the old binary contained none of the 1,791 Night manifest names,
while the new one contained all of them. The 172,048-byte TLS increase also
matches the optional script-link diagnostic event ring and its counters.
Resources, exports and the import set were unchanged. This proves a build-scope
change; it does not by itself attribute the observed startup delay to a
particular optional component. See the
[binary profile proof](../../build/vehicle-investigation-20261022/native-r2-custom-content-20261022T120025318671Z.json)
and [linker contribution comparison](../../build/vehicle-investigation-20261022/native-r2-dll-contributions-20261022T115829910790Z.json).

Both first-r2 processes were stopped, and the r1 DLL plus both r1 technique-set
archives were restored and hash-verified. The shader candidate and first-r2
artifacts remain retained. The corrected build uses separate
`renderer-r2-scope` helpers and passed all 2,869 checks at 12:06 UTC. Its DLL
returns to the r1 size of 26,455,040 bytes and the same TLS size, contains none
of the 1,791 Night manifest names, and embeds both corrected Island hashes.
Deployment completed at 12:12:28 UTC with all three installed hashes verified
and no rollback or cleanup errors. Complete live acceptance remains pending.
Recovery and corrected-build evidence:

- [Stopped process identities](../../build/vehicle-investigation-20261022/renderer-r2-scope-stopped-r2.json).
- [Three-file r1 restoration](../../build/vehicle-investigation-20261022/renderer-r2-scope-baseline-restoration.json).
- [Corrected helper scope review](../../build/vehicle-investigation-20261022/state-renderer-r2-scope-helper-review-20261022T120326095155Z.json).
- [Corrected build and test receipt](../../build/vehicle-investigation-20261022/vehicle-renderer-r2-scope-build.json).
- [Independent corrected binary scope check](../../build/vehicle-investigation-20261022/native-r2-scope-binary-review-20261022T120829522141Z.json).
- [Installed-file hashes and backups](../../build/vehicle-investigation-20261022/vehicle-renderer-r2-scope-deployment.json).
- [Independent final archive audit](../../build/vehicle-investigation-20261022/state-renderer-r2-bundle-audit-20261022T115323403499Z.json).

## Permanent-build live acceptance, 2026-10-22

The deployed `renderer-r2-scope` run uses server PID 38844, created at
`2026-10-22T12:12:51.9931251Z`, and client PID 19184, created at
`2026-10-22T12:13:06.2917788Z`. Both load the game-directory proxy with SHA-256
`035cd5541bebf424bdfb2807bd87612193304bac703a18e556cf04b86ec4c117`.
Independent module enumeration found no temporary shader probe modules.

The server ran the requested playlist/map/BR/minimum-player arguments. Its
native `scr_br_timelimit` record and script watcher both read back 7200 before
joining. The client entered Warzone and used `mm_start 104`, then
`xstartlobby`. The retained logs show native search HTTP 200, authenticated
Island assignment, server acceptance and human slot 0 spawning. The ordinary
prematch transition completed without an extra start command; the 12:24:22
image shows the normal 118:35 match HUD. This establishes local native
matchmaking, not a remote or multi-client session.

The post-infil registry replaced the prematch entity IDs. All 18 current
vehicles have valid physics IDs and full health. The client ownership graph
contains six models, 37 materials, 26 technique sets, 213 techniques and 101
images. The six restored companion families have 61 expected render states
and all 91 returned pipeline objects, with the exact staged key multiset.
These observations establish loaded state; screenshots and control evidence
are evaluated separately below.

| Class | Current entity / object | Permanent-build visual result | Control result |
| --- | --- | --- | --- |
| ATV | 296 / 13110 | Textured tan body, seat, racks, wheels and coherent rider pose | Entry, short forward drive, handbrake and exit verified; health 975/975 |
| SUV | 304 / 13208 | Textured blue body, windows, wheels and spare tire | Entry, short forward drive, handbrake and exit verified; health 1975/1975 |
| Little Bird | 308 / 12886 | Coherent fuselage, cabin framing, tinted windshield, skids and operating rotor appearance | Pilot entry, 118.411-unit vertical rise, controlled return to the same pad and later normal exit verified; health 3475/3475 |
| Tactical Rover | 305 / 13149 | Textured body, cabin, dashboard, seats, cage, suspension and visible tires/rims | Entry, short forward drive, handbrake and exit verified; health 1225/1225 |

The ATV's entire F/W/Space/F sequence fits inside the independent 180-sample
window. Both driver links agree during occupancy and clear on exit. It travels
at least 157.135 engine units; estimated XY speed peaks at 159.018 units/s,
then falls below 0.2 after handbrake and remains near zero before exit. Noclip
is disabled and health stays 975/975 throughout.

The SUV sample covers entry, throttle and handbrake, with at least 103.226
units of sampled travel and XY speed falling from 129.124 to 0.2144 units/s.
Its continuous window ends before the final exit. Separate guarded braked and
exited snapshots at 12:37:43.362 and 12:37:47.872 differ by only 0.32324 units;
the later snapshot confirms both links clear, normal movement, noclip off and
health 1975/1975. Speed estimates in both tests are finite differences of
position samples, not a newly assumed native velocity field.

The helicopter's six Space/Control input calls fit inside its 180-sample
window from 12:58:42.363 to 12:59:27.227. It rises 118.411 units with maximum
horizontal displacement of 3.374 units, then returns near its original pad
height. The final 69 samples span 17 seconds with Z between 676.947 and 677.684,
compared with baseline 678.030. Health remains 3475/3475, physics ID 18 and
bilateral pilot ownership remain intact, and noclip stays off. Raised and
later settled screenshots corroborate the return to the wooden pad. No native
ground-contact flag was interpreted.

The helicopter helper stopped on its conservative input-time budget after
descent, before its automatic stability/exit phase. Its error and false
automatic acceptance flags remain in the original receipt. A later ordinary
F input is checked separately: snapshots at 13:01:37.776 and 13:01:41.946 show
pilot ownership clearing, normal on-foot movement returning and full vehicle
health. This exit lies outside the continuous sample. The first helper attempt
had stopped before any input because of a Python-launcher argument mismatch;
the corrected helper resolves and launches the interpreter directly.

The rover wrapper starts the existing controls 0.119 seconds after the local
sampler reports READY. All four inputs and guarded snapshots fit its 45-second
window. Bilateral entry is observed at 13:04:31.622 and exit at 13:04:40.377.
Sampled travel is at least 121.598 units, with 120.701 units net displacement.
After handbrake, estimated XY speed falls from a peak of 131.014 to below
0.024 units/s before exit; the final 1.751 seconds before exit contain only
0.0317 units of movement. Health stays 1225/1225, physics ID 15/model 264 remain
stable, noclip stays off and normal movement resumes with both links cleared.
The sampler completes 180 observations but stores only its 41 changed states;
the last changed-state timestamp does not mean sampling stopped early.

The close helicopter views resolve the initially bright cabin regions as
openings with framing and sky beyond; the windshield has a dark reflective
surface. The rover's cage partly obscures the seated driver, so its screenshots
do not certify the complete hand and foot pose. The reviewed views show the
vehicle bodies and visible wheels restored, without establishing every angle,
LOD or animation cycle. Short forward driving does not establish steering,
damage or audio behavior, and vertical helicopter movement does not establish
horizontal flight handling. The shared shaders also serve non-vehicle map
materials; archive/interface preservation and the rendered test route do not
constitute an exhaustive visual tour of all 122 affected material names.

Evidence under `build/vehicle-investigation-20261022/`:

- [ATV correlated acceptance](../../build/vehicle-investigation-20261022/state-atv-r2-scope-acceptance-20261022T123508162095Z.json).
- [ATV body and wheels](../../build/vehicle-investigation-20261022/renderer-r2-scope-atv-before-controls-20261022T122921288Z.png).
- [SUV correlated acceptance](../../build/vehicle-investigation-20261022/state-suv-r2-scope-acceptance-20261022T124102991747Z.json).
- [SUV body, windows and wheels](../../build/vehicle-investigation-20261022/renderer-r2-scope-suv-before-controls-20261022T123604983Z.png).
- [Helicopter correlated flight and later exit](../../build/vehicle-investigation-20261022/state-heli-r2-scope-acceptance-20261022T130416206846Z.json).
- [Helicopter raised above the pad](../../build/vehicle-investigation-20261022/renderer-r2-scope-heli-vertical-20261022T125829542Z-raised.png).
- [Helicopter returned to the pad](../../build/vehicle-investigation-20261022/renderer-r2-scope-heli-after-bounded-flight-20261022T130010883Z.png).
- [Rover correlated acceptance](../../build/vehicle-investigation-20261022/state-rover-r2-scope-acceptance-20261022T130747178365Z.json).
- [Rover cabin and body](../../build/vehicle-investigation-20261022/renderer-r2-scope-rover-before-controls-20261022T130335073Z.png).
- [Rover occupied after braking](../../build/vehicle-investigation-20261022/renderer-r2-scope-rover-controls-20261022T130427999Z-braked.png).
- [Fresh client pipeline comparison](../../build/vehicle-investigation-20261022/state-vehicle-pipelines-renderer-r2-scope-client-20261022T122415052558Z.json).
- [Native matchmaking and join log freeze](../../build/vehicle-investigation-20261022/script-r2-scope-runtime-freeze-20261022T122106284814Z/receipt.json).

## Final logs and preservation

The final paired log freeze at 13:06:25 follows all four control tests. Both
exact-launch crash logs are empty. Neither full runtime log contains a matched
fatal/Com_Error, missing XAnim/XModel/door curve, fastfile failure or PSO-creation
failure. Server native GSC errors remain at 64, unchanged from the join freeze.
Each of the four passive control windows matches the frozen server bytes and
has no ERR-level or native GSC error. General GSC logging is bounded, so this
is an observation of recorded output rather than proof of no unlogged error.

Residual errors remain explicit. ATV, SUV and rover control windows each have
ten INFO-wrapped customization track lookups. Helicopter entry has the same
ten little_bird track lookups before its vertical-flight window. Persistent
br_dmz.objTime Get/SetPlayerData failures, other customization lookups and
render-target fallback warnings continue. The client adds nine ERR-level lines
after the join capture: known infil CLUT/audio-zone errors, DW-starved timing
variants and two localize misses, `space` and `^3[{common:+activate}]`, absent
from the selected earlier baselines. Two DLOG member messages, br_down.means_of_death
and life_death.victim_weapon_rarity, are also newly observed in that comparison.
Newly observed does not establish that the renderer repair caused them. No
additional sound-bank failures occur after join. These are not wholly clean
logs, and the test does not certify audio playback.

Final hash verification matches all three installed artifacts to the corrected
build and staged copies, all ten protected source files (including both MP
dvar registration files), all three preserved runtime assets and the Island
manifest. The client and server retain their exact original launch identities.
The last vehicle-control snapshot leaves the player on foot beside the rover,
with noclip disabled and both vehicle ownership links cleared. No further
gameplay input was sent during the final log and preservation review.

- [Final log and artifact summary](../../build/vehicle-investigation-20261022/script-r2-scope-runtime-freeze-20261022T130625363542Z/final-summary.json).
- [Full classified log evidence](../../build/vehicle-investigation-20261022/script-r2-scope-runtime-freeze-20261022T130625363542Z/final-classification.json).
- [Installed and preserved file hashes](../../build/vehicle-investigation-20261022/script-r2-scope-runtime-freeze-20261022T130625363542Z/installed-preservation.json).
