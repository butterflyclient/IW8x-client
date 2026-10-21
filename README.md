# IW8x Replay 1.20

`restruct-120/project` is the primary merged codebase. It incorporates the
`src-xaratti` snapshot while retaining Replay's native plugin/GSC runtime,
dedicated server architecture, offline playlists and XInput lifecycle.
The source snapshot and original production trees were not modified.
Read [the migration report](docs/xaratti-merge.md) for features, compatibility,
validation evidence, file disposition and rollback information.
[ORGANIZATION.md](../ORGANIZATION.md) and [AUDIT.md](../AUDIT.md) are historical
reports describing the project before this merge.

Application sources are under `src`; runtime inputs remain under `data`.
Plugin SDK/loader source stays under `src/plugin`, with its documentation in
[docs/plugin](docs/plugin/README.md). Eight shared examples live under
`examples` and build against the Replay SDK through `replay_plugin_*` targets.
Their historical MW1.44 feature descriptions do not establish Replay runtime
support; check host capabilities as before.

The XInput DLL entry now lives under `src/bootstrap`; native forwarding stays
under `src/proxy`. This preserves Replay's lifecycle instead of adopting IW7's
component loader or separate TLS DLL. Public APIs, RVA values, ABI layouts,
resource identifiers, data formats and GSC names are unchanged. Extracted runtime
helpers use private `detail` namespaces; their state stays with its owning module.
Native addresses and Replay symbol metadata live in `src/engine/addresses.hpp`.
The standalone backend shares protocol/model/provider implementations with the
DLL, and new frontend, weapon, social, commerce and port features have focused
owners. External contract spellings and generated catalogs remain intact.

## Build and validation

Converted frontends, Rebirth and night map ports are compiled out by default.
Their INI settings are ignored in this build; the native frontend stays active.
Keep them disabled with `xmake f --custom_content=n -y`. To build the optional
ports again, use `xmake f --custom_content=y -y` and rebuild; their converted
asset bundles and development settings are still required.

Run from this directory using XMake and the Visual Studio C++ toolchain:

```powershell
xmake f -m release -a x64 -y
xmake build -j 4 mw120rproxy
xmake build -j 4 magma_backend120
xmake build -j 4 mw120rproxy_radmin_host
xmake build -j 4 mw120rproxy_radmin_client
```

Products are under `xmake-out/x64/Release`: the standard `XInput9_1_0.dll`,
variants in `radmin-host` and `radmin-client`, and `backend/MagmaBackend120.exe`.
From `restruct-120`, run `python validate_xaratti.py` for all four products,
plugin examples/support tools and eight offline regression suites.
[Recorded evidence](docs/merge/summary.json)
and [build/test logs](docs/merge/validation.json) describe what was exercised.
Game startup, matches, live networking and plugins inside the game have not
been tested in a running game or deployed to production.

`audit_organization.py` is a historical preservation check whose expected
inputs predate this merge. One-time merge/extraction scripts must not be rerun
on the merged result. Missing historical test fixtures are documented in the
migration report. Do not run the root MW1.44 orchestrator against this project.

`tools/compile_plugin_gsc.py` still needs explicit `--bindings`, `--compiler`,
`--source-root` and `--output` arguments. The compiler executable and runtime
binding database are external. `tools/generate_gsc_tokens.py` now reads the
preserved canonical source in `thirdparty/xensik-gsc/iw8_token.cpp`. That source
is regeneration input, not a compiled dependency. MinHook's license and JSON's
embedded MIT notices are unchanged; the canonical source's GPL license is
included beside it.

Binary evidence and historical porting scripts stay in the original workspace;
the copied port overview labels those references explicitly. Game executables,
build caches/products, backups, private profiles and compiled/disassembled
archives are excluded.
