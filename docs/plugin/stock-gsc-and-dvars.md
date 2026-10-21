# Stock GSC calls and typed dvars (Replay 1.20.4)

```cpp
api.game_thread([api] {
    auto gametype = api.gsc().call("getdvar", "g_gametype").as<std::string>();
    auto chat = api.dvars().get_string("cl_textChatEnabled");
    auto entities = api.gsc().call("getentarray", "player", "classname");
    for (const auto& item : entities.as_array()) {
        auto entity = item.as<iw8x_entity>();
        api.gsc().method(entity, "giveweapon", "iw8_ar_mike4");
    }
});
```

`IW8X_CAP_GSC_CALL`, `IW8X_CAP_RICH_VALUES`, and `IW8X_CAP_TYPED_DVARS`
advertise independently guarded support. Check capabilities before using them.
The example plugin is in `examples/gsc_gameplay`.

Calls use exact lowercase stock names. The generated Replay 1.20.4 MP catalog has
2,472 names backed by retail registration IDs/RVAs and named semantic-reference
counterparts. Lookup uses binary search; every call checks that the live stock
table still contains the expected callback. Unknown or unavailable names fail
with `IW8X_NOT_FOUND`; a replaced callback fails with `IW8X_UNSUPPORTED`.
This invokes stock builtins, not arbitrary functions defined in GSC scripts.

Stock `getcurrentweapon` is a global builtin method-table entry. Its caller's
script path or namespace does not participate in lookup; the player is its
`self`. Script exports instead use a normalized script asset path and canonical
function ID, resolved together into a program handle by the existing registry.

Run calls in `api.game_thread()`. A worker or unverified thread receives
`IW8X_WRONG_THREAD`. An active VM, pending native input/output parameters, or
reentrant generic invocation receives `IW8X_BUSY`, preserving that state.
Native plugin callbacks entered from GSC must schedule their stock calls.
Arguments retain stock order and stock type/domain requirements. Stock script
errors return `IW8X_CALLBACK_ERROR`. SDK failures throw `iw8x::api_error`.
Engine fatal errors and process faults are not recoverable script errors.

## Stock script exports

Use `IW8X_CAP_SCRIPT_CALL` for functions defined in stock GSC scripts. Declare
the export during plugin load or `IW8X_SCRIPTS_BEGIN_LOAD`:

```cpp
const auto build_weapon = api.gsc().script(
    "scripts/mp/class", "buildweapon", iw8x::gsc::value_type::weapon_);
const auto is_night_map = api.gsc().script(
    "scripts/cp_mp/utility/game_utility", "isnightmap", iw8x::gsc::value_type::boolean);

api.game_thread([api, build_weapon, is_night_map] {
    auto weapon = build_weapon(
        "iw8_sn_alpha50", iw8x::array{}, "none", "none", -1,
        iw8x::value{}, iw8x::value{}, iw8x::value{}, is_night_map().as<bool>()).as<iw8x::weapon>();
    // Pass weapon to stock methods such as giveweapon.
});
```

Once declared, the same export can also be invoked from a game-thread task as:

```cpp
auto weapon = api.gsc().call_script<iw8x::weapon>(
    "scripts/mp/class", "buildweapon",
    "iw8_sn_alpha50", iw8x::array{}, "none", "none", -1,
    iw8x::value{}, iw8x::value{}, iw8x::value{}, is_night_map().as<bool>());
```

`class::buildweapon` takes the weapon root (`iw8_sn_alpha50`), which keys the
stock default attachment map. An empty custom-attachment array keeps those
defaults; passing the `_mp` asset name bypasses that root lookup.

For `self scripts\path::function(args)` calls, register the export in the same
way and use `function.on(player, args...)` inside a game-thread task:

```cpp
const auto give_weapon = api.gsc().script(
    "scripts/cp_mp/utility/inventory_utility", "_giveweapon"); // At plugin load.
// Later, in game scope with a built Weapon and a valid player entity:
give_weapon.on(player, weapon);
```

`on` accepts an `iw8x_entity` or SDK entity. It binds that entity as GSC `self`,
without adding it to the argument list. It shares export registrations, cached
handles, return contracts, and rich-value marshalling with ordinary script calls.
Stale receivers return `IW8X_STALE`. The append-only `call_script_on` host entry
is size-checked; older hosts report `IW8X_UNSUPPORTED` for `on` calls.
See `examples/snipers_only` for the take/wait/give/switch/refill sequence.

The host resolves names using the supplied xensik IW8 canonical token table.
These IDs are separate from builtin function/method IDs. Script paths accept
`/` or `\`, with or without `.gsc`; paths and function names normalize to ASCII
lowercase. No compiler checkout or token file is needed at runtime. The checked-in
canonical catalog shares the IW8 compiler identifiers; native builtin tables
and dvar identities are recovered separately for Replay.

For known stock module paths, the resolver first uses the decimal canonical
module token as the compiled asset identity, matching Replay 1.20.4's far-call linker.
For example, `scripts/cp_mp/utility/game_utility` has module token `0x08C4` and
asset name `2244`; its own `isnightmap` export has function token `0x72B6`.
Readable paths and `.gsc` paths remain fallbacks for literal-name/custom assets.
`Scr_LoadScript` reuses loaded modules and links their stock far-call
dependencies before `Scr_GetFunctionHandle` selects the requested export.
Calling a function that a module references does not make it an export of that
module: qualified calls keep their module scope, rather than searching all
loaded scripts for the same function name.

A failed lookup logs the requested path, readable function name and token,
module token, every attempted asset name, each native load/handle result, and
the final zero handle. Missing exports are cached, so this diagnostic appears
once per export per script generation instead of on every plugin call.

Equivalent owner/path/function/return-type registrations reuse an ID. Different
owners and return contracts share the resolved export, including missing-export
results. Only the existing `Scr_EndLoadScripts` window loads assets and resolves
engine handles. Calling a newly declared export after that window reports
`IW8X_STALE` until the next script load; the convenience call does not open a
new lookup window. Retain the registration across maps: the host invalidates
old handles and resolves it again on each script load. Unloading a plugin
removes its registrations.

`script` defaults to an untyped owned `value` result. Supply a `value_type`
to enforce a return contract. `call_script<T>` infers that contract for int,
float, bool, `std::string`, `iw8x::vec3`, `iw8x_entity`, `iw8x::array`,
`iw8x::struct_value`, and `iw8x::weapon`; its default returns `iw8x::value`.
Use owned strings, not `std::string_view`, with the typed convenience call.
Boolean returns accept only integer 0 or 1. Weapon is detected automatically
from the VM object's entity class (10), including inside arrays and structs.
It is distinct from a vector and from a world entity. `weapon_` and
`call_script<iw8x::weapon>` enforce a Weapon return contract; untyped calls
also return an owned Weapon. The bridge copies all 60 native descriptor bytes
and alternate mode before releasing the VM value, then uses stock
`GScr_Weapon_AddParam` to pass it back. No plugin module context is needed.

Weapon snapshots belong to the map/script generation in which they were
captured. Passing an old snapshot after a reload returns `IW8X_STALE`; obtain
a fresh value from a builtin or a retained script registration. The ABI adds
`IW8X_WEAPON_SNAPSHOT` with a borrowed snapshot pointer while preserving the
24-byte value layout and existing tag numbers. Rebuild Weapon-using plugins
with this SDK; the old 12-byte/vector Weapon tag is rejected.

Errors throw `iw8x::api_error` with the requested path and function:

| Status | Meaning |
| --- | --- |
| `IW8X_NOT_FOUND` | Unknown canonical name, missing script/export, or registration owned by another plugin. |
| `IW8X_STALE` | No resolved handle for the current script/map generation, stale entity receiver, or old Weapon snapshot. |
| `IW8X_TYPE_MISMATCH` | Return value differs from the requested contract. |
| `IW8X_INVALID_ARGUMENT` | Invalid path/name, return type, or value encoding. |
| `IW8X_WRONG_THREAD` / `IW8X_BUSY` | Invocation is outside game scope or the VM cannot accept a call. |
| `IW8X_STOPPING` | The owning plugin is stopping or unloaded. |

The lower-level `script(path, uint32_t token, result_type)` and C ABI
`register_script` / `call_script` remain available. Readable registration uses
the same registry and invocation machinery. Replay 1.20.4 is the sole target.

## Values and ownership

`iw8x::value` aliases the existing owning `iw8x::gsc::value`. It supports
undefined, int32, float32, bool, string, vector, entity, dense array, struct and Weapon.
GSC represents booleans as integers; use `.as<bool>()` on integer results.
`iw8x::vec3`, `iw8x::array` (a vector with `push_back`), and
`iw8x::struct_value` (an ordered map) are convenience aliases.

```cpp
iw8x::array values;
values.push_back(123);
values.push_back("hello");
values.push_back(iw8x::vec3{1, 2, 3});
auto keys = api.gsc().call("getarraykeys", values).as_array();
```

Collections are deep snapshots: strings and nested values belong to the C++
value. No array/struct handle or engine object survives the call. Copies can
outlive the plugin owner's registration state; their destructors must still
execute in their owning DLL before that DLL is actually unloaded. Entity
leaves retain generation-checked identities, not engine references. Deleting
an entity or changing script/map epochs makes those identities stale.

Retail struct fields use **canonical numeric IDs**, a different namespace
from VM string tokens. Struct keys are lossless `@hhhhhh` strings, created by
`iw8x::canonical_field(id)` and decoded by `iw8x::canonical_field_id(key)`.
For example, the verified stock `origin` field is `canonical_field(0x2c5)`.
Returned structs use this same representation. Arbitrary readable field-name
resolution is not implemented; supplying an ordinary name returns
`IW8X_UNSUPPORTED`. This does not enable the separate entity field API.

```cpp
iw8x::struct_value data{
    {iw8x::canonical_field(0x2c5), iw8x::vec3{1, 2, 3}}
};
bool is_struct = api.gsc().call("isstruct", data).as<bool>();
```

Limits: 256 call arguments, depth 32, 65,536 total value nodes, 16 MiB total
string/field bytes, and 65,535 bytes per NUL-free string. Sparse, associative,
negative-index and cyclic arrays/objects, localized strings, function/code
pointers, threads, animation objects and non-entity engine objects are
unsupported. Sparse arrays are never silently compacted.

Rich inputs and outputs are supported by the protected generic caller. Native
callbacks registered by plugins can read rich GSC arguments after the mapping
guards pass. **Rich registered-callback returns and notify payloads remain
unsupported**: those paths lack the separate interpreter error boundary needed
to marshal allocations without jumping across plugin C++ owners. Existing
scalar returns and notifications retain their previous behavior.

## Dvars

`api.dvars().get_string/get_bool/get_int/get_float(name)` resolves readable
names through `plugin/sdk/dvars.hpp` and its generated
`dvar_names_replay.hpp`. The table imports 6,724 unique mappings from the existing
Replay `dvar_names.h` reference, including `cl_textChatEnabled` -> `NLPSQMQRTP`.
Lookup is exact and case-sensitive, allocation-free, and accepts known hashed
names as a fallback. `iw8x::dvar_names::resolve(name, false)` disables fallback.
Unknown mappings and known but unregistered dvars return `IW8X_NOT_FOUND`.
The existing `api.dvar_string(hashed_name)` remains available for low-level use.

Bool, int/enum and float reads require the matching native dvar type; other
types return `IW8X_TYPE_MISMATCH`. String reads use the native formatted variant
accessor and copy its result. Typed reads use verified engine accessors,
including the retail value decoding, instead of reading the value union.
All reads require the verified game/server scope and an active plugin owner.

Verify the checked-in Replay catalog and port mappings from `restruct-120/project`:

```powershell
# Historical binary audit: ../../prod-120/ida-reports/gsc_port/audit_port.py
```

The audit compares every sorted mapping against the Replay reference, rejects
conflicting duplicates, and records its SHA-256 in `coverage.json`. The root
MW 1.44 dvar generator has a different input format and generated namespace;
it must not be used unchanged to overwrite the Replay catalog.

## ABI

ABI version remains 1. `iw8x_value` remains 24 bytes and existing host offsets
are unchanged. Host entries `call_builtin` and `get_dvar` append at x64 offsets
256 and 264. `register_script` / `call_script` occupy offsets 272 / 280;
`register_script_name` appends at 288 and `call_script_on` at 296, making the host size 304 bytes. Receivers
borrow C ABI result trees only during their callback; the SDK
copies them before returning. The owner lease spans engine execution and
result delivery. New SDK accessors size-check the appended entries and fail
with `IW8X_UNSUPPORTED` when the relevant entry is absent on an older host.

From `restruct-120/project`, build `mw120rproxy`, `replay_plugins`, and the focused
`gsc_core_tests`, `gsc_invocation_tests`, `gsc_loader_tests`,
`gsc_bootstrap_tests`, and `gsc_restart_tests` targets. Run their executables
under `xmake-out/tests/`. These are offline fixtures; they do not launch a game.
Replay binary evidence and the port coverage audit are recorded in
[the port notes](../reverse-engineering/gsc-port.md).
Live Replay acceptance has not been performed. The opt-in `gsc_live_probe`
target is compiled separately and is never installed automatically.
