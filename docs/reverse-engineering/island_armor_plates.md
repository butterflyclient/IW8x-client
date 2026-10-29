# Island Plunder armor-plate compatibility — Replay 1.20.4

## Report and scope

In playlist 104 (`mp_sm_island_1`, `br` / Plunder) the armor deploy weapon can animate repeatedly without raising the player's armor. Source 1.64.5 GSC calls native builtins whose numeric IDs differ from Replay 1.20.4. Both corrections in this change operate only on exact pinned Source bytecode in **private compiler copies** when Island dedicated BR is loading. They do not update any fastfile, shared ScriptFile, global native builtin, armor health directly, ammo directly, or non-Island mode.

## Root cause: monitored switch never reports current weapon

`scripts/cp_mp/utility/inventory_utility.gsc::domonitoredweaponswitch` loops until `iscurrentweapon(deploy_weapon)` is true, then returns 1. `scripts/cp_mp/killstreaks/killstreakdeploy.gsc::switchtodeployweapon` does not schedule its completion/cleanup callback before that monitored switch returns. The armor module (`scripts/mp/equipment/armor_plate.gsc`) supplies `delay_end_common_combat` as completion callback: it waits approximately 1860 ms, performs the first armor application, decreases plate ammo and sends `armor_plate_inserted` and `br_armor_plate_done` for cleanup. Thus a broken monitored current-weapon test produces the observed animation without the armor commit.

- Installed ScriptFile **numeric2245**, source `scripts/cp_mp/utility/inventory_utility.gsc`, root65. Installed bytecode length **1379**, SHA-256 `86a4570a5c7d71521f237a5a9698f8984133720a5383ca755d162db8c5d0226f`, FNV1a64 `0xFB5F560D3E566486`.
- `iscurrentweapon` export token `0x7197` begins at bytecode offset1290, length27. Native opcode at **1312** is `20 64 03`: a **three-argument** Source 1.64.5 IsSameWeapon (ID `0x364`). In Replay ID `0x364` instead tests *IsNullWeapon*, so it does not compare the current and requested weapon. The correct Replay IsSameWeapon ID is `0x363`; patching exactly the low operand byte at **1313** from `64` to `63` fixes this condition.
- Source1.64 native callback RVA `0x2C611D0` and Replay1.20 callback RVA `0xE965F0` both inspect two weapons; Replay ID0x364 callback RVA `0xE96750` reads one weapon's null status instead. Original Replay GSC `iscurrentweapon` also calls `issameweapon(current,target,1)`.
- Crucially, other installed numeric2245 one-argument `0x364` instructions at offsets **172, 237, 813** are legitimate Replay IsNullWeapon calls converted from Source `0x365`. The zero-argument Source `0x366` at 751 was already converted correctly to Replay `0x365`. These remain untouched.
- Corrected private-copy SHA-256 `7969331de44f631169fce522226f7c81fc134a76c3ffc22d4bdd3747cee1b93a`, FNV1a64 `0x70BA4A39E4B08B2D`.

## Independent repeat-plate input mismatch

- Installed ScriptFile **numeric2300**, source `scripts/mp/equipment/armor_plate.gsc`, root285. Bytecode length **1557**, SHA-256 `6fe10af264d09db022b8f294b5a09648ca8998a9e6cdfe510e6f8c04b64ed1cd`, FNV1a64 `0xBA44D570AC52A923`.
- The Source1.64 repeat-plate helper at offset1239 has method call `7E 46 87` at offset1248. It intends to check the player's repeat-plate button bit `0x40000000000`; native Source method0x8746 RVA0x345E970 and native Replay method **0x8743**, RVA0x11FBB40, execute the corresponding input bit query and return a Boolean. Replay 0x8746 instead checks unrelated client state; Replay 0x8744 is not equivalent, despite misleading static function labels.
- Original Replay armor-plate GSC helper calls `7E 43 87` at offset1158. The private source opcode's operand at **1249** changes from `46` to `43`. This helper runs **after** successful first-plate application, so the repeat-loop fix alone is insufficient for the original no-armor problem.
- Corrected private-copy SHA-256 `c4758bab0b47b35faacd98ab9f84f2d1942779b545822118c01c8cb094e750cb`, FNV1a64 `0xEB3F037FE72B77A2`.

## Already-correct paths and remaining runtime discriminator

- Installed numeric2300 weapon creation call at431 has **already** been translated to Replay MakeWeapon ID0x341 (`1E 41 03`), so no duplicate or global builtin remapping is performed.
- `scripts/mp/equipment.gsc::getequipmentslotammo` and `decrementequipmentslotammo` have stable Replay native method IDs. The actual `br_armorhealth` increase is a GSC player property update; `setclientomnvar("ui_br_armor_amount")` merely mirrors it to the HUD.
- An independent conditional path exists in `bandage.gsc::usequickslothealitem`: on zero health-equipment ammo and armor plate in the inventory, it may call `takeequipment("health")` after transferring counts, clearing `self.equipment["health"]` before the first-plate delayed commit. This branch also exists in stock 1.30. It is **not** patched without proving its reachability in live gameplay. If a live plate fails after the switch comparison fix, capture `equipment["health"]`, ammo, `br_armorhealth`, `br_maxarmorhealth`, current/deploy weapon, and `isparachuting` at the 1860 ms commit.

## Source implementation and tests

- `src/gsc/compiled/island_armor_switch_native_abi.hpp/.cpp`
- `src/gsc/compiled/island_armor_native_abi.hpp/.cpp`
- Private-copy selected-script scope, per-map state and idempotence: `src/gsc/compiled/gsc.cpp`
- Full authentic bytecode test fixtures and negative/rollback controls: `src/gsc/compiled/tests/island_armor_switch.hpp`, `island_armor.hpp`, `island_armor_link.hpp`.
- `xmake.lua` links both native ABI sources into Release and Island GSC test target.

The tests reject altered module names, lengths, fingerprints and call-site bytes. They verify exactly one changed byte per module, legitimate null checks unchanged, transactional rollback, original registered assets untouched, Island-only linker scope, repeat link, epoch reload and zone reset. Build uses x64 Release, `--custom_content=n --island_port_probe=y`.

## Deployment and live verification

`build/armor-plates-20261022/armor-deployment.json` is the deployment receipt. The installation has a verified rollback under the game's `backups/armor-plating-20261022-20261023T000912Z/` folder. Game runtime tests and live first-plate acceptance should be recorded separately from static bytecode tests.
