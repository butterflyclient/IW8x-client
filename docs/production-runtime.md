# IW8x production runtime

## Organization

The loader follows the component organization and registration style in
`references/iw7-mod`. The XInput proxy entry point lives in `src/tlsdll`;
`src/client/loader` owns component dispatch, and `src/client/component` contains
the lifecycle adapters. Existing engine, GSC, networking, client and dedicated
implementations retain their domain boundaries and native contracts.

Initialization is serialized by the bootstrap stage mutex:

1. `post_start`: verify the pinned executable and all string signatures, then
   start the shared scheduler.
2. `post_load`: prepare the local identity and install cooperative host exit.
3. `post_unpack`: install the existing feature owners in priority order at the
   Replay splash boundary. `NotReady` retries do not repeat successful stages.
4. `pre_destroy`: stop components in reverse order, cancel submissions and join
   workers outside `DllMain`.

Replay metadata, GDI compatibility, asset pools and the splash trigger still
require their original early bootstrap boundaries. Runtime string scanning
does not replace the final live-byte checks on native detours.

## Scheduler

`src/common/utils/thread_pool` provides bounded admission, reusable threads,
condition-variable waits, delayed/repeating callbacks, cancellation and explicit
shutdown. A callback executes outside the pool mutex and returns an optional
delay; no delay retires it. One retained owner cannot overlap itself.

Set these optional values in **mw120rproxy.ini beside the executable**:

```ini
[scheduler]
worker_threads=0
queue_capacity=128
```

`0` chooses logical hardware threads divided by eight, clamped to 1–4 workers.
Explicit worker counts are clamped to 1–8. Queue capacity is clamped to
16–4096 and counts queued plus active work, including retained repeating
owners. Submission returns a handle: `accepted()` is the admission receipt;
its boolean value reports whether the work is still live. Cancellation waits
only from outside that pool, avoiding worker-to-worker join deadlocks.

Publisher variables, playlist downloads, social HTTP, chat HTTP and party HTTP
use finite callbacks with retained owner state. New requests and state changes
wake them; protocol sync intervals and retry deadlines still apply. Plugin
worker callbacks use the next real task deadline and enqueue notifications.
Their execution remains serial, although consecutive callbacks may run on
different shared threads. Dedicated socket loops and separately owned durable
queues retain their existing thread ownership.

The pool cancels queued work and prevents repeats on shutdown, then waits for
active callbacks. An in-flight WinHTTP operation retains its existing timeout;
the scheduler does not forcibly cancel it. If every worker is waiting on HTTP,
another owner waits for a worker. Increase `worker_threads` only when that
behavior is observed in the actual deployment.

## Native completions

Native APIs stay on their existing engine pumps. Each migrated service drain
handles at most 16 completions or one millisecond per invocation, whichever
comes first. Pending results retain their order for a later pump. Callbacks run
outside the owner queue mutex.

The general scheduler completion queue has capacity 256 and processes at most
64 callbacks or one millisecond through the existing online-services pump.
Its idle path checks an atomic flag before taking the queue mutex. Time limits
are cooperative: a callback already running is allowed to finish and can
exceed the nominal limit. These limits are per pump, not a global frame cap.

## Signatures and compatibility

`data/signatures/runtime_registry.inc` contains 1,182 hex string patterns with
wildcards and resolution metadata. Startup reads an immutable file snapshot,
verifies SHA256, scans the selected PE sections and requires each pattern to
resolve uniquely to its expected RVA. It batches searches by a rare exact byte
instead of scanning the whole image separately for every pattern.

The supported Replay executable SHA256 remains:

```text
68fb1cbcb2924182724004039de55a4c50152bb6561803c4898b7930b38132f0
```

Regenerate after adding a supported binding:

```powershell
python tools/generate_runtime_signatures.py --image '<path to game_dx12_ship_replay.exe>' --report '<coverage.json>'
```

The generator covers the original 357 catalog rows plus 825 source additions.
Its syntax-based inventory found 1,140 executable positions without an
uncovered position. Data offsets, ABI layouts and computed addresses outside
that inventory remain independently maintained contracts. A successful scan
does not establish compatibility with another game build.

## Build and validation

```powershell
xmake build -y iw8x_client
xmake build -y mw120rproxy
xmake build -y magma_backend120
```

Existing tests remain available. `production_runtime_tests` checks pool bounds,
cancellation, restart epochs, idle scheduling, signatures, components and
completion budgets. `production_exit_tests` runs isolated child processes
against the real exit adapter, checking IAT protection restoration, active
worker joins, single cleanup, recursive exit and exit-code forwarding.

The host `ExitProcess` import performs normal cleanup. Forced process
termination and crashes cannot guarantee cooperative cleanup. Full gameplay,
network reconnect and frame-time stability still require a manual runtime
session; test benchmarks do not measure FPS.
