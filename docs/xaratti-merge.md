# src-xaratti integration into Replay 1.20

This report records the full-content merge validation. Subsequent default
builds compile converted frontends, Rebirth and night ports out using
`--custom_content=n`; see the build option in [README.md](../README.md).
The recorded merge product hashes describe the build before that switch.

## Scope and evidence

The primary implementation is `restruct-120/project`. The source snapshot at
`C:\Users\james\Documents\iw8-ds-prod\src-xaratti` was read without changes.
This is an implemented source merge with compiled products and offline tests.
It does not establish verified game runtime behavior.

The comparison covered 585 source files excluding Git and generated build/cache
directories: 383 new, 113 requiring integration, 58 identical, and 31 initial
exclusions. Source build settings were then integrated into target XMake.
The remaining exclusions are 17 temporary research files and 13 files from an
installed Python package environment. Inspection tools requiring `pefile`
still require that external package.

Every runtime/config/data/tool/vendor file has a final owner in
[inventory.json](merge/inventory.json). Final source hashes match the pre-merge
snapshot. The static audit accounts for 1,911 source function definitions,
including the dedicated script observer adaptation. Name/body matching cannot
prove runtime correctness; [the audit](merge/function-parity.json) records
individual matches and adaptations.

## Migrated functionality

| Area | Integrated features and updates | Primary owners |
| --- | --- | --- |
| Profile/auth | Local settings/synchronization, XP tokens, progression/catalog/challenge fixes, resident string tables and profile initialization | `src/auth`, `src/network/profile` |
| Frontend | Scene/lifecycle updates, online frontend, native commerce, menu/render/animtree adapters and Lua interfaces | `src/client/frontend`, `src/client/online_frontend.cpp`, `data/lua` |
| Weapons | Ammo/capacity, loadouts, low ammo, inspection playback/policy, recoil, spread, hit locations, perk pairs and rules | `src/client/weapons` |
| Network services | Backend agents/latency, endpoint/auth fixes, publisher variables, policy/MOTD, native matchmaking, tournaments and playlist transfer/catalogs | `src/network/backend` |
| Social/communications | Friends/requests/invites, presence, user lists, party/roster relay, chat/voice, Discord and account/lease authorization | `src/network/social` |
| Commerce | Store/catalog, custom mods, camo, unlock and name-validation policy | `src/network/commerce` |
| Dedicated/assets | Startup/restart/bot updates, BAP collision/pool improvements, asset marking, infiltration transients and headless D3D handling | `src/dedicated` |
| Ports/diagnostics | Night/escape map support, script ranges/builtins, physics/geometry/lighting/loot/animtree changes, script-link and weapon diagnostics | `src/diagnostics`, `src/gsc/compiled/compat` |
| Engine/hooks | Layouts/symbols, signatures, image identity, shared address catalog and transactional typed hook batches | `src/engine` |
| Backend/builds | Standalone GUI, HTTP/UDP listeners, shared services, persistent stores, TU19/TU27 catalogs and Radmin variants | `src/backend`, `xmake.lua` |
| Runtime inputs | Configuration, catalogs, Lua/GSC, developer tools, Discord RPC/RapidJSON and combined resource inventory | `config`, `data`, `tools`, `thirdparty` |

## Architecture and compatibility

- Retained the target's split dedicated, BAP, online-service, plugin and GSC
  modules. Source monolith updates were applied to their owning submodules.
- Kept the XInput bootstrap and pinned worker lifecycle. Ordered port/auth/UI
  installation precedes native plugin discovery; plugin failures publish a
  failed startup state. Dedicated splash suppression, renderer interception
  and critical patch checks retain their lifecycle boundaries.
- Kept one GSC end-load hook owner. The shared GSC hook calls the dedicated
  observer, integrating source behavior without competing detours. Source
  script-link hooks use distinct sites.
- Added an internal port-table bridge. Valid port callbacks are preserved in
  plugin-owned tables; new plugin bindings skip reserved IDs. The plugin ABI,
  capability checks, persistent binding store, SDK/examples and worker pumping
  remain in the target architecture.
- Preserved guarded native playlist inflation, embedded offline TU19 resource
  102 and `playlist_load [path]`. Backend HTTP snapshots remain preferred and
  replace local catalogs when received. `playlists.require_backend=1` enables
  the source's backend-required mode and disables fallback/local overrides;
  the default `0` preserves offline operation.
- Split the social driver into native tasks, worker and provider modules,
  preserving all 80 function bodies in the extraction audit. Provider includes
  are shared with the standalone backend. Discord/authorization model methods
  were extracted without changing their function or field tokens.
- Kept game/backend playlist implementations distinct. Added the missing
  backend resource manifest for TU19/TU27 catalogs (6000/6001) and payload
  dependency tracking for incremental resource builds.
- Applied tabs, Allman braces, focused directories and private snake_case
  helpers while preserving engine/plugin/GSC/external names and generated
  catalogs. No production `.cpp` exceeds 900 lines. Removed empty matchmaking
  and social headers, folded user-list logic, and 17 optional targets whose
  referenced test files were absent.
- Retained target queue diagnostics and WARP bot-compute selection alongside
  source command-list/device behavior. Native submission, copies, barriers and
  fences remain real operations. Replaced target `ReadTable` with the resident
  index reader.

[Extraction](merge/social-split-audit.json),
[model](merge/social-model-split.json), [naming](merge/style-merge-audit.json)
and [cleanup](merge/cleanup-audit.json) audits record structural changes.

## Build and validation

All commands and exit codes are recorded in [validation.json](merge/validation.json),
with copied logs in `docs/merge/validation`. Product hashes and sizes are in
[summary.json](merge/summary.json).

| Release build | Product under `xmake-out/x64/Release` |
| --- | --- |
| `mw120rproxy` | `XInput9_1_0.dll` |
| `mw120rproxy_radmin_host` | `radmin-host/XInput9_1_0.dll` |
| `mw120rproxy_radmin_client` | `radmin-client/XInput9_1_0.dll` |
| `magma_backend120` | `backend/MagmaBackend120.exe` |

The eight plugin examples, GSC runtime library, bot benchmark and live-probe
utility also compile. The benchmark and live probe were not executed as game
runtime tests. The software test's source-root include path was corrected
after its first post-merge build failure. That existing test also exposed two
target extensions removed by the source snapshot: queue diagnostics and WARP
bot-compute selection. Both were restored in their original modules; the
regression test was kept unchanged.

Eight regression suites: `merge_feature_tests`, `backend_merge_tests`,
`gsc_core_tests`, `gsc_invocation_tests`, `gsc_loader_tests`,
`gsc_bootstrap_tests`, `gsc_restart_tests` and `headless_software_tests`.

Final result: all 24 build/run commands passed.

| Suite | Result |
| --- | --- |
| Merge features | 1,323 checks passed |
| Standalone backend | 19 checks passed |
| GSC API/plugin DLL loading | 5,767 checks passed |
| Invocation/script/value/dvar/table/entity | 5,500 checks passed |
| GSC loader | Load/postload, growth, reload, teardown, generations, restart and error gates passed |
| Bootstrap | 15 checks passed |
| Restart plugin | 33 checks passed |
| Headless software | WARP, concurrent probes, failure propagation, DIRECT/COMPUTE/COPY transfers, fences, filtering and bot BFS passed |

The feature fixture checks hook transactions, port ownership/reservations,
tokens, latency, low-ammo edge cases, perk pairs, settings ownership/persistence/
conflicts, names, and embedded TU19/TU27 transfer integrity. The backend fixture
starts HTTP/UDP listeners in an isolated temporary directory, exercises HTTP
health, catalog switching, rejection/authorization contracts, stop/restart
and retained selection. It does not run the game. The headless fixture exercises
the offline D3D software path.

An initial backend fixture expected malformed unauthenticated frontend input
to return 400. The source authenticates before parsing and returns 401; the
fixture was corrected and transport/provider size-limit cases added. Authorization
was not relaxed to pass the test.

## Remaining limitations

- Actual game startup/matches, dedicated gameplay/bots, live remote/Radmin
  networking, in-game plugins, rendering, Discord and converted map assets
  need game runtime testing. Offline tests and linking do not verify these.
- A persistent plugin binding using an ID newly reserved by a selected port
  cannot be silently remapped without changing compiled scripts. Startup
  reports the conflict and retains the database. Resolve that binding and
  recompile affected scripts before enabling the conflicting port combination.
- Source-only test targets lacked their `tests/` inputs. Those historical
  tests were not run. Missing target fixtures are in the cleanup audit.
- The preserved vendored JSON header emits nonfatal MSVC C4828 warnings for
  an invalid UTF-8 character. Vendor bytes/notices were retained.
- Validation used Windows x64 Release; Debug and alternate toolchains were
  not separately validated. External GSC compiler/binding inputs and optional
  converted map/scene assets remain deployment prerequisites.

## Rollback and local build environment

Original target source/config/data/vendor/docs snapshot:

`C:\Users\james\Documents\iw8-ds-prod\backups\xaratti-merge-20261009-232514\target-before.zip`

Preserve the merged directory, extract the snapshot into a fresh directory,
configure XMake there and rebuild. Extracting over the merge leaves new files
behind. Caches/products are not in the snapshot. No deployment was performed.

The first host link failed when C: ran out of storage. Generated objects were
moved to `D:\CodexBuildCache\iw8x-xaratti-merge-20261009-232514\obj`, with a
local junction at `project/.xmake/obj`; subsequent linking succeeded. Build
settings do not require D: on another machine. Compiler/debug settings were
not weakened to work around storage.
A later incremental host link reported a corrupt generated PDB (LNK1285).
That PDB was removed and regenerated; the subsequent host link and final
validation succeeded.

The outer merge/extraction scripts are historical one-time mutators. Use
`validate_xaratti.py`, `audit_xaratti.py` and `finalize_xaratti.py` for final
validation/evidence; do not rerun the historical transforms on this result.
