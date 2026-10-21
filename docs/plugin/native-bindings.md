# Native Binding IDs

Compiled GSC needs stable builtin IDs for plugin-provided functions and methods. IW8x persists those assignments instead of deriving them from plugin load order every run.

## Generated files

The plugin root contains:

```text
plugins/
├── native-bindings.json
└── iw8x_native_bindings.gsh
```

`native-bindings.json` stores durable binding identity:

```json
{
  "api": 1,
  "target": "replay-1.20.4",
  "bindings": [
    {
      "method": false,
      "slot": 0,
      "plugin": "Native Example",
      "name": "native_add"
    }
  ]
}
```

`iw8x_native_bindings.gsh` is generated for the script compiler and contains native function/method defines corresponding to those persistent slots.

## Stability rules

Once a name has a persisted identity, IW8x expects the same:

- plugin name;
- function-vs-method kind;
- slot;
- native name.

The host rejects conflicting reuse rather than silently changing a compiled script's builtin ID.

## Capacity

ABI v1's Replay 1.20.4 table extension reserves:

- 256 native function slots;
- 256 native method slots.

## Keep bindings with compiled scripts

Treat the generated binding database/header as part of the script build inputs. Deleting or replacing them while compiled scripts still use the old native IDs can break native dispatch consistency.

IW8x writes these files atomically and uses an interprocess file lock while changing the binding database.


Replay rejects unmarked and 1.44 binding databases. Use a separate plugin root
for each build. The first Replay custom function is `0x419`, the first custom
method is `0x8787`, and `0x519` belongs to the protected host VM trampoline.
