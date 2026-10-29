# Island matchmaking client allocation

Reported on playlist 104, `mp_sm_island_1`: read access violation at Replay
RVA `0x15DF329`, `mov ecx,[rax+28h]`, with RAX zero. IDA MCP analysis used
`game_dx12_ship_replay_dump.exe`, image base `0x7FF7A95E0000`.
Imported function names in this database are not consistently aligned; the
following identification uses instructions, strings and callers.

- `0x15DF2F0`: ClActiveClientMP allocation (native diagnostic string identifies
  the allocation). At `0x15DF30E` it loads the dvar pointer at `0xEF2E350`.
- `0x15E8CC0`: MP GameAllocate; calls the allocator at `0x15E8DA4`.
- `0x15E7290`: CL_ConnectAndPreloadMap; calls GameAllocate at `0x15E7360`.
- `0x15EBA30`: MP dvar registration. At `0x15EC816` it registers enum
  `SLKLOSLLS`, described as the client snapshot throttling algorithm, and
  stores the result in `0xEF2E350` at `0x15EC81B`. The SDK maps this token to
  `cl_adjust_time_throttle_type`.
- `0x15EDCF0`: MP dvar deregistration. At `0x15EE29F` it deregisters the
  throttle dvar, then clears its pointer at `0x15EE2AB`.
- `0x15ED8D0`: CL_MainMP_ShutdownGameMode calls that deregistration routine.
  `0x15E9E40` (MP All Clients Initialization) normally calls registration.

The observed allocation therefore has a missing MP registration prerequisite.
The precise upstream reason the lifecycle reached this state has not been
reproduced. Earlier script-link failures occur in the same client log and
should not be conflated with this access violation.

The client component verifies both native entry signatures, then restores the
native MP dvar group before GameAllocate if the throttle pointer is absent.
Restoring only a synthetic enum would leave other deregistered globals missing.
Healthy allocations do not re-register anything. Failed restoration uses the
native ERR_DROP path before any client hunk is allocated. Dedicated processes
do not install this hook.

`production_runtime_tests` covers healthy registration, restoration, repeated
allocation, unsuccessful restoration and a subsequent teardown. This verifies
the registration gate; successful live matchmaking still requires an in-game
playlist 104 retest with the rebuilt DLL.

The later `Mismatching client count: 102 vs 20` rejection concerns gameplay
buffer capacity rather than missing MP dvar registration. Its investigation
and allocation-boundary correction are recorded in
[island_client_capacity.md](island_client_capacity.md).
