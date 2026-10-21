# IW8x Plugin API acceptance test

This plugin exercises the public native plugin SDK against a live MW 1.44 server.
It deliberately keeps connected-player/chat checks separate from the server-only
suite so an empty dedicated server reports `SKIP` instead of a false failure.

Commands:

- `iw8x_test_api` — ABI/capabilities, GSC primitive/rich values, arrays, structs,
  entity methods/staleness, VM error recovery, typed/readable/hashed dvars,
  player enumeration, host handles, task cancellation, and async completion.
- `iw8x_test_players` — connected player handles, names/entities/XUID availability,
  plus broadcast/tell transport. Text-chat rendering still needs visual confirmation.
- `iw8x_test_stress` — 2,000 primitive calls, 1,000 nested collection calls,
  2,000 pairs of typed dvar reads, then a final VM sanity check.
- `iw8x_test_results` — current PASS/FAIL/SKIP totals and lifecycle event counters.

The plugin also registers `acceptance_echo` and `acceptance_self_index` native GSC
builtins. Calling those from a script tests the GSC -> native-plugin callback side.

Build from the staged project root:

`xmake build -r replay_plugin_plugin_api_acceptance`

Output is written to `xmake-out/examples/plugin_api_acceptance`.

