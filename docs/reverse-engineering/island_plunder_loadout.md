# Island playlist 104 Plunder loadout restoration

## Original failure

The original live Island DMZ/Blood Money session ran playlist 104 with
map mp_sm_island_1 and native mode br. A read-only GSC variable snapshot in
build/damage-investigation-20261022/gsc-damage-flags-before-plunder-deep-20261022T195141696845Z.json
showed the common BR flag level.debug_safehouse_regroup_start = 0, although
the current native scr_br_allowLoadout dvar was already string "1".
The server was using rankedloadouts and rankedmatch=1, yet spawned bots
showed class gamemode, primary iw8_fists_mp and no secondary weapon.

The original 1.64.5 scripts/mp/gametypes/br.gsc::main at lines 16-17 caches
scr_br_allowLoadout and scr_br_allowLoadoutOnlyInPreLobby BEFORE calling
br_gametypes::init at line55. Only later does the DMZ subclass
scripts/mp/gametypes/br_gametype_dmz.gsc::init set allowLoadout=1 (line40).
If the early cached flag is zero, br.gsc chooses nakeddrop instead of
the original custom restore callback _id_1195B at lines177-182, disables
class choice through br_weapons.gsc lines1018-1022, and overrides
self.class to the gamemode fists fallback at lines6103-6109.
This order differs from the 1.30 script which initialized DMZ first.

The original playlist104 global dvars set scr_br_allowLoadout=0 and its
playlist-specific DMZ rules do not override that value. The project's
unlisted Plunder startup preset had the required settings, but the
g_playlistId>=0 branch replaced that preset with genuine playlist rules.

## Minimal correction

src/dedicated/runtime/island_plunder_loadout_policy.hpp and lifecycle.cpp
append these native console commands AFTER authentic playlist104 rules and
BEFORE native devmap/script initialization:

    set scr_br_allowLoadout 1
    set scr_br_allowLoadoutOnlyInPreLobby 0

The policy is gated by island_port_probe, playlist104, map mp_sm_island_1,
and native mode br. The installed 1.64 DMZ scripts retain their own
scr_br_loadout_option="nothing"; native class selection, ranked DDL,
weapons, ammo and perks remain the authoritative source. No synthetic
giveweapon, global class bypass, or asset replacement is used.

## Test evidence

New island_plunder_loadout_tests passed 2437 scope checks. The same release
build passed production_runtime_tests (3726), gsc_core_tests (5778),
gsc_loader_tests, gsc_island_loader_tests and custom_fastfile_tests (2869).
The deployed proxy DLL SHA256 is
F4B939A4D009FED17F93FD56D87FE172CBC021C4BA368443228194DB26879BEB.
Rollback path: installed game root,
backups/island-plunder-loadouts-20261022-195714Z/XInput9_1_0.dll.
Build, test and deployment receipts are under build/plunder-loadout-20261022.

After the first fresh server launch, the read-only native VM capture in
build/damage-investigation-20261022/gsc-damage-flags-plunder-after-fix-20261022T200303793039Z.json
shows level.debug_safehouse_regroup_start=1 and rankedloadouts unchanged.
The runtime function-pointer distance between
level.bypassclasschoicefunc and level.parachuterestoreweaponscb became
1588 bytes, exactly 6722-5134, proving _id_1195B was selected. Before
the fix the distance was 1689 (6722-5033), identifying nakeddrop.
Thus startup flag/callback behavior is LIVE VERIFIED, not merely simulated.

Live human custom class selection, first deploy/landing and normal
death/respawn must still be independently confirmed by watching real
primary/secondary/equipment or selected class GSC fields. The previous
client experienced an unrelated EXE/ERR_SERVER_TIMEOUT after the dedicated
server automatically recycled without Island's explicit map override.
Do not claim that timeout proves a class-loadout regression.

## Additional source validation native-ABI collision and matching fatal

The old dedicated `mw120rproxy.dedi.previous.log` recorded the concrete
`Could not find 'commonOption attachmentRestricted iw8_ar_mcharlie' field
in 'ddl/mp/recipes.ddl'` immediately before `MENU/MATCHRULES_DATAREADERROR`
(`Com_Error` caller RVA `0x159A9FD`, through RVA `0x12681A8`). Native and
installed GSC review established that Source 1.64 module `1922`
(`scripts/mp/validation`) passes an actual weapon name to native builtin
`0x401`, meaning **weapon restricted** in Source. Replay 1.20.4 interprets
that ID as **attachment restricted**; its correct weapon-restriction ID
is `0x400`. Consequently Replay interprets the weapon name as an attachment
DDL field and raises the fatal when the field is absent. This is a
version-specific *method/builtin ID collision*, not a missing general
`recipes.ddl` file or a reason to bypass stock restrictions.

The separately validated Island-only private-compiler-copy repair is in
`src/gsc/compiled/island_plunder_validation_native_abi.hpp/.cpp` and the
`gsc.cpp` linker. It fingerprint-guards numeric Source module `1922`,
code length 6,672, and translates exactly 17 original restriction calls
(`0x401` -> `0x400`, `0x402` -> `0x401`, `0x403` -> `0x402`,
`0x404` -> `0x403`, `0x405` -> `0x404`). It separately guards numeric
perks module `2378`, length 10,175, and translates its two perk-restriction
calls (`0x403` -> `0x402`). Argument opcodes and all other script bytes
remain unchanged. Unexpected original bytecode is rejected; original
registered assets and unrelated maps are not modified.

This was built, tested and deployed on 2026-10-22. Installed and built
`XInput9_1_0.dll` SHA256:

`7B05687CF31858C25DD7B0D4B3645E70414CCCFE7FA21721716523774075722B`

The previous working loadout DLL is hash-verified in
`backups/island-plunder-validation-20261022-204717Z/XInput9_1_0.dll`.
The deployment receipt and six successful regression logs live under
`build/plunder-loadout-20261022/validation-*`:

- 2,437 Island loadout policy scope checks;
- 2,869 custom fastfile checks;
- 3,726 production runtime checks and 5,778 GSC API checks;
- both standard and Island GSC loader suites, including the new
  native-validation private-copy, reload, scope and stock-exclusion checks.

## Live human initial spawn and respawn, corrected build

The freshly launched server PID `19792` started
`2026-10-22T20:47:57.0277764Z` with explicit
`--dedi -bap --playlist 104 --map mp_sm_island_1 --mode br --min-players 1`.
Client PID `38080` started at `20:48:16.0775622Z`, joined playlist 104,
and loaded the same DLL. Server startup logs confirm the early Plunder
loadout policy, plus **17** validation and **2** perk translations:

`Island Plunder class loadouts enabled before BR script init: playlist=104`

`Island Plunder validation ABI: translated 17 restriction calls in 1922 private compiler copy`

`Island Plunder validation ABI: translated 2 restriction calls in 2378 private compiler copy`

Before the human join the native dvar reader confirmed `scr_br_allowLoadout`
`"1"`, `scr_br_allowLoadoutOnlyInPreLobby` `"0"`,
`scr_br_loadout_option` `"nothing"`, and DMZ selection with a stable
registry. The post-init native VM reader confirmed the cached flag **1**,
the correct `parachuterestoreweaponscb` `_id_1195B` pointer difference
of **1588**, and ranked class data enabled (`rankedmatch=1`,
`loadoutsgroup="rankedloadouts"`). The genuine human post-infil capture
`build/damage-investigation-20261022/gsc-damage-flags-human-post-infil-20261022T210334583198Z.json`
confirmed `game.flags.prematch_done=1`, `inLiveLobby=0`, `hasspawned=1`
and `waitingtoselectclass=0` with primary **`iw8_ar_mcharlie`** and
secondary **`iw8_sn_hdromeo`**, including their complete native weapon
configuration names. The client screenshot
`build/plunder-loadout-20261022/validation-client-infil-small.jpg` and
later scoped capture `class-choice-loadouts-targeted-small.jpg` show the
configured primary weapon rendered and equipped with ammunition.

For a controlled respawn, the client was sent the normal local `kill`
command at approximately `21:07Z`, with no process or asset edits. The
dedicated log recorded `death (0)` at line `14206` and native client
spawn for player slot 0 at lines `14230` and `14248`. The subsequent
read-only native VM report
`build/damage-investigation-20261022/gsc-damage-flags-human-post-suicide-respawn-20261022T210901367120Z.json`
confirmed the same primary and secondary configured weapon names,
`hasspawned=1`, and `waitingtoselectclass=0`. The screenshot
`build/plunder-loadout-20261022/validation-post-respawn-small.jpg`
shows the primary with **30 loaded rounds** after the spawn.

An independent nested script-object capture
`build/plunder-loadout-20261022/player-class-attachments-nested-class-20261022T211152144334Z.json`
confirmed the actual selected class token **`custom1`**, a live
`classstruct` with `iw8_ar_mcharlie`, `iw8_sn_hdromeo`,
`equip_throwing_knife`, `equip_adrenaline`, and camo `camo_11c`.
The live weapon was separately observed as a picked-up SMG while the
classstruct continued to preserve the original selected loadout;
the subsequent respawn reapplied its primary weapon. This makes the
weapon-restoration result stronger than merely seeing a class menu.

At the final inspected log cutoff, both processes remained alive and
responsive and **zero** `MENU/MATCHRULES_DATAREADERROR` entries appeared
in the new client and server logs. Do not extrapolate that to untested
later match-end recycle paths.

### Bounded remaining coverage

Changing *to another custom class* through the interactive Pause >
Loadouts editor remains unverified. The guarded window-message click
did not produce a new selection; there is no evidence of a successful
different-index class event. Do not conflate this UI-test limitation
with the verified selected `custom1` initial loadout and respawn.

The imported class script also queries attachment slots 5 through 9,
which the Replay 1.20 ranked DDL lacks; `GetPlayerData` logs warnings
for those keys, but the observed `classstruct` weapons/equipment and
full weapon grant were valid. No broad DDL schema mutation or unverified
attachment rewrite was made. Multiple later deaths and respawns appear
in the logs; their causes were not classified in this scoped test.

## Final Plunder attachment and human-menu compatibility, 22 October 2026

The later live review established two additional narrow compatibility
differences. Source 1.64 `scripts/mp/menus.gsc` (compiled numeric module
`1877`) calls its human/test-client predicate at native ID `0x3C3`, while
Replay's equivalent is `0x3C2`. The wrong Replay ID invokes a pathfinder
initialization rather than a Boolean, allowing the class handler to store
raw menu index `0` rather than `custom1`. The Island-only private compiler
copy remaps exactly two verified `0x3C3 -> 0x3C2` callsites, retaining the
native human class-selection path.

Source 1.64 `scripts/mp/class.gsc::loadout_updateclasscustom` loops over
ten selected attachments for both weapons. The installed Replay 1.20
`rankedloadouts.ddl` has only five attachment-setup entries (`0` through
`4`). Replay's invalid DDL state can return a static empty string, which
is not `"none"`: the Source GSC code counts it as an extra attachment and
may erase the valid selected ones. The installed, fingerprint-verified
numeric ScriptFile `1827` has 32,325 code bytes and its exact two DDL
read-loop literal operands at byte offsets `6131` and `6308` are now
translated `10 -> 5` in the isolated private compiler view. Its original
ten-slot class-structure arrays and two ten-entry cleanup loops are
unchanged. All 221 exports and 289 imports remain structurally intact.
Source FNV64 `CEF89C68ABD6B6AD`, corrected FNV64
`E70752727D406927`. The source bytecode and installed fastfile are not
mutated.

Separately, Source's genuine `mp/statstable.csv` stores five as the
attachment limit in column 18 for all 57 authentic MW19 weapon names
shared by this Replay profile. Replay's stock column is empty. The
scoped `script_table_lookup` hook in `src/gsc/compiled/gsc.cpp` always
invokes the original table lookup first, substitutes the authentic
`"5"` only for an empty lookup of the exact table, column and one of
these 57 names, and changes neither the table nor any DDL data. The
return ABI and source whitelist were verified independently against
the retail executables and original Source table. Source-defined or
nonempty native results are preserved.

### Final installed build and live evidence

The final Release/x64 proxy and installed `XInput9_1_0.dll` both have
SHA-256 `703722679E53F993C27A94CE02D943691BCB26380831AFB805D6F4C914F38D0F`.
The earlier DLL SHA256 `639E647EEB6A48BD0F316E574E2A78BB1B4C2DF627EF99080DAA28E8EE5A7143`
was backed up at `backups/island-plunder-attachment-final-20261022-221443Z/XInput9_1_0.dll`
under the game directory; see
`build/plunder-loadout-20261022/attachment-cap-final-deployment.json`.
No native map fastfile or persisted player DDL was changed. The combined
tests under `build/plunder-loadout-20261022/class-ddl-five-slots-*.log`
pass: Island and standard GSC loader suites, 5,778 GSC API checks,
3,726 production runtime checks, 2,869 fastfile checks, and 12,165
Plunder policy checks.

The fresh server, PID `33844`, started
`2026-10-22T22:14:59.1905620Z` using playlist 104, explicit
`mp_sm_island_1` / `br`, and the final DLL. Client PID `19040`
started `22:17:07.9694808Z`. Server startup accepted numeric module
`1827` two-loop translation, numeric `1877` human class selection,
numeric `1922` (17 restriction calls), and numeric `2378` (2 perk
restriction calls). The scoped first-spawn and post-infil native GSC
observations are saved in:

- `build/plunder-loadout-20261022/player-class-attachments-five-slot-first-spawn-20261022T222552873073Z.json`
- `build/damage-investigation-20261022/gsc-damage-flags-five-slot-post-infil-20261022T222732287227Z.json`
- `build/plunder-loadout-20261022/player-class-attachments-five-slot-final-equip-20261022T223516713980Z.json`

These independent read-only captures establish `class=custom1`,
`lastclass=custom1`, `wam_sequence=custom1`, `hasspawned=1`,
`waitingtoselectclass=0`, `br_infilstarted=1` and actual equipped
`primaryweapon`/`secondaryweapon` strings for `iw8_ar_mcharlie` and
`iw8_sn_hdromeo`. The primary selected attachment array contains
`pistolgrip02`, `barlong`, `silencer3`, `reflexmini`, `vertmags`; the
secondary array contains `laserbalanced`, `xmagslrg`, `stockh`,
`silencer3`, `barlong`. Indices 5-9 are exactly `none` for both; equipment
is `equip_throwing_knife` and `equip_adrenaline`. The native weapon
strings preserve the full selected attachments/camo. First-person
screenshots `class-five-slots-first-infil-small.jpg` and
`class-five-slots-pause-menu2-small.jpg` in the Plunder evidence folder
show the equipped camouflaged rifle and the scoped sniper respectively,
each with ammunition. The latter filename refers to the capture attempt,
not a successfully displayed pause menu.

The captured post-test client/server logs are
`build/plunder-loadout-20261022/class-five-slots-final-client.log`
and `class-five-slots-final-server.log`. Neither snapshot contains a
`MENU/MATCHRULES_DATAREADERROR`, native `Com_Error`, fatal XAnim, or
generic fatal-error entry. Other existing render-target, sound and
`GetPlayerData(modeStats.br_dmz.objTime)` warnings remain.

### Newer armor-build compatibility check

The newer deployed DLL from the separate armor-plate compatibility task has
SHA-256 `C37751E2E6F92D94217848142129F88978D9D4C4434202BE462E6A9452F5CE9C`.
On its live Island server PID `35564`, started
`2026-10-23T00:10:01.0518010Z`, a read-only, identity-checked script VM
snapshot
`build/plunder-loadout-20261022/player-class-attachments-post-armor-plunder-live-20261023T013532474898Z.json`
confirmed the human player remained `class=custom1`,
`lastclass=custom1`, `wam_sequence=custom1`, `hasspawned=1`, with
`iw8_ar_mcharlie` and `iw8_sn_hdromeo` actual equipped native weapon
strings, their existing camouflages, and the same five primary and secondary
attachments in each class array. Slots 5–9 remained `none`. The server DLL
and current built Release proxy hash matched. This verifies the loadout
correction survived the subsequent armor DLL update. It is a current-spawn
snapshot, not a separately classified current-build death/respawn test.

The same server log already shows two script linker cycles retaining
`Island Plunder validation ABI: translated 17 restriction calls in 1922`
and the class/menu converters. Neither current client nor current server log
contains `MENU/MATCHRULES_DATAREADERROR` at that inspection cutoff.
That supports the validation-builtin correction without asserting that every
possible future match-end recycle has been tested.

**Remaining live coverage:** The selected `custom1` loadout and both
actual weapons were verified on this DLL's first match spawn after infil.
Two client console commands, `kill` and `cmd kill`, did not produce a
server death notification in this run; they do **not** constitute a
respawn test. A genuine controlled death/respawn restoring the same
custom1 weapons was recorded on the prior validation DLL in the earlier
section above, but that result must not be attributed to this final
attachment fix without a fresh controlled native death. Changing to
`custom2` through the interactive loadout UI also remains unverified;
the menu clicks and Escape attempts did not deliver a distinguishable
class-selection event. No synthetic class or weapon was forced during
the final test. The separate optional `custgamemode` class path uses
other ten-slot reads and was not exercised here.
