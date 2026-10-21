# Island 1.64 → Replay 1.20 renderer compatibility adapter

The Island-only adapter is compiled only with `--island_port_probe=y`. It is
**off by default** and is enabled only by setting the process environment
variable `IW8X_ISLAND_RENDER_ADAPTER=1` before game startup.

## Hook-free default (performance baseline)

The Island-specific technique/PSO setup and D3D12 ExecuteIndirect hooks are
**not installed by default**, including in an `--island_port_probe=y` build.
The native Replay rendering path is used directly. This is a performance
isolation control, **not** a completed geometry/depth/lighting backport.

To reproduce old shader capture behavior, both flags must be set **before
launch**: `IW8X_ISLAND_RUNTIME_HOOKS=1` and, for the separate depth
workaround, `IW8X_ISLAND_DEPTH_QUICKFIX=1` (or `always`). Without the
first flag neither Island renderer hook is installed, regardless of the
other options. Production conversion should instead correct the serialized
techset/mesh contract so no per-frame hook is required.

Its current implementation is an **observation and policy gate**, not an
automatic depth-state replacement or a completed visual fix. The hook is
shared with the existing native smoke format variant. It checks the *active*
`maps/mp/mp_sm_island_1.d3dbsp` world, and only considers imported `tmo/`
and `tmco/` model materials using main/depth technique families. If the
active world changes, the adapter fails closed. With no proven same-instance
prepass coverage, depth translation remains disabled even when opted in.

## Why not globally change the comparison function?

The 2026-10-16 GPU audit in
`evidence/island-backport-20261013/render_audit/LIVE_MATCH_20261016.md`
established that a temporary `ALWAYS` comparison restores much of the
blackened building textures, `GREATER_EQUAL` partially restores some
parts, and `LESS_EQUAL` can reveal windows without fixing the building.
`ALWAYS` introduces overdraw and leaves stretched geometry.

The strongest evidence for a broken render contract is in
`client-18188/prepass-main-coverage-20261016.json`: 128 matched
prepass/main sampled command slots, with a large fraction of main-colour
geometry lacking sampled prepass coverage. In two separate epochs,
`tmo/building_wall_corner_brick_01` has zero prepass IA primitives at its
matched command slot and more than 56,000 main-colour primitives. A shared
material/command slot does **not** conclusively identify the same physical
mesh across passes: the GPU argument producer must be audited before an
automatic repair is justified.

## Implementation stages

1. **Safety boundary — implemented.** Compile-time Island-only hook, opt-in
   runtime policy, active-map verification, imported-material classification,
   no mutation without an exact verified prepass identity. Other maps retain
   native behavior.
2. **Surface identity and generation — unresolved.** Decode original 1.64
   and Replay 1.20 GPU static-model metadata; correlate the same physical
   instance and packed draw/surface ID across depth and main-colour
   `ExecuteIndirect` commands *at execution time*. Compare prepass
   eligibility, generated command counts and expanded-index ranges.
3. **Actual compatibility correction — pending evidence.** Translate
   only the mismatched Island surface/technique metadata or prepass draw
   command generation. Keep the original native `EQUAL` depth semantics
   wherever matching depth coverage exists; do not blanket-disable depth.
4. **Validation.** Repeat GPU paired statistics and matching frame
   screenshots; ensure black windows/walls, overexposure, and stretched
   polygons are all corrected. Verify ordinary 1.20 maps, lobby transitions,
   game restart, and performance against native behavior.

## Build and tests

From `iw8x-production/project`:

```powershell
xmake f -m release -a x64 --custom_content=y --island_port_probe=y -y
xmake build -y mw120rproxy
```

The standalone policy regression source is
`src/diagnostics/island/tests/island_renderer_adapter_tests.cpp`.
**Do not install the candidate DLL just because it builds:** the current
adapter intentionally leaves the visual behavior unchanged until the draw
generation discrepancy has a proven correction.
