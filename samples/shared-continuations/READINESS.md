# Async/component readiness audit

Status: core-continuation prototype only. Do not propose the current header as a
WASIp3-capable API or freeze it on the strength of the numeric C fixture.

## What was and was not run

The original evidence in VALIDATION.md is core C fixture testing and hosted
measurements. It did not include WASIp3 conformance, streams, Canonical ABI futures,
or Windows sanitizers. Miri and macOS WAMR sanitizers were separate checks.

This audit pins the Component Model reference suite at
`fed10b0b823b82553dd8363f16a640816f5eac65`. All 45 `test/async/*.wast` files are
inventoried with content hashes in `conformance_inventory.csv`. Every runtime
result is explicitly NOT_RUN: neither draft has a component-WAST driver or an
external Canonical ABI async implementation. Some upstream cases also cover
features beyond a released WASI subset; `upstream_nyi` records the suite's own
exclusions. Inventory and reading the tests are not executing them. No subset of
these component assertions has been passed by either draft.

The suite's [test instructions](https://github.com/WebAssembly/component-model/blob/fed10b0b823b82553dd8363f16a640816f5eac65/test/README.md)
use Wasmtime as a runner. Running that implementation alone would not qualify
TinyWasm or WAMR. Extracting only a core module and stubbing the canonical imports
would remove exactly the assertions that need verification, so it is not counted
as conformance either.

The distinct official [WASI testsuite](https://github.com/WebAssembly/wasi-testsuite/tree/de7fbe267210b52f63893270403177239b4f4a9b)
is pinned at `de7fbe267210b52f63893270403177239b4f4a9b`. Its P3 Rust source programs
are separately inventoried by Git blob ID in `wasi_p3_inventory.csv`, all NOT_RUN.
These are WASI interface/component tests, whereas the preceding inventory tests
Component Model async semantics. The official suite's WAMR adapter advertises only `wasm32-wasip1`; a successful default
run through that adapter would not establish P3 coverage. The draft fixture imports `host.wait`/`host.inc`,
not WASI 0.3 interfaces. No working external P3 adapter currently connects either
runtime to the official suite, so there is no defensible passed P3 subset here.

## Shape that remains useful

A small runtime-neutral C surface, opaque core execution handles, and external
resource tables, futures, streams and scheduling remain sensible boundaries.
Canonical async values can cross core boundaries as numeric handles; the runtime
need not acquire language-specific futures or a mandatory event loop. Ordinary
native calls must keep their existing path. Compiled-out state/dispatch overhead
continues to be an independent acceptance gate.

This does not establish that the current handle contract is sufficient. A
stackless callback adapter can use ordinary core calls and own task lifetime
outside each completed core invocation. A stackful adapter needs multiple parked
execution contexts to make progress in the same component/store, with precise
rooting and reentry rules. The current store-wide reservation cannot establish
that capability; cross-store interleaving is a much weaker property.

## Concrete semantic gaps

| Requirement | Current primitive / missing proof | Pinned reference examples |
|---|---|---|
| Distinct ready-to-run yield and wait-for-event states | PENDING is one host-call state; C resume loops through fuel suspension, with no budget/yield outcome | `switch-to-ready-callback.wast`, `empty-wait.wast` |
| Same-instance scheduling and exclusive-lock eligibility | A pending core call reserves its entire store; it cannot establish required task progress | `during-sync-scheduling-candidates.wast`, `same-component-stream-future.wast` |
| Task returns a stream but its producer keeps running | Core invocation completion is not component task completion; external task/end ownership is missing | `partial-stream-copies.wast`, `idle-drop.wast` |
| Cooperative cancellation and completion winning a race | Current cancel discards frames and notifies a host; it neither delivers a guest event nor permits guest cleanup | `cancel-delivery.wast`, `cancel-subtask.wast`, `future-completion-order.wast` |
| Partial transfer, backpressure, zero-length operations, closed ends | No stream/future endpoint or waitable-set implementation exists | `partial-stream-copies.wast`, `zero-length.wast`, `closed-stream.wast`, `futures-must-write.wast` |
| Borrow/resource lifetime, trap poisoning, post-return and reentry | Numeric core tests do not check these canonical rules | `drop-cross-task-borrow.wast`, `passing-resources.wast`, `builtin-trap-poisons-instance.wast`, `reentrance.wast` |

These are requirements from the upstream [concurrency semantics](https://github.com/WebAssembly/component-model/blob/fed10b0b823b82553dd8363f16a640816f5eac65/design/mvp/Concurrency.md)
and test assertions, not additional built-ins proposed for the core. They should
primarily live in an external component adapter, but its runtime seams must permit
the necessary scheduling, memory access, synchronous realloc/post-return, and
safe ownership of parked stacks. Merely naming a destructive operation cancel
must not imply canonical cancellation compatibility.

## Additional core regression tests

`edges.c` covers empty completions versus NULL re-polls, zero-result roots,
repeated polls without callback replay, invalid completion while pending,
destructive cancellation before start and at each host boundary, completion after
cancellation on a still-live handle, memory growth with callback pointers resolved
again, distinct-store interleaving, a trap after resume and subsequent store reuse.
It repeats the cancellation boundary matrix 32 times. Its same-store exclusion
assertion records an architectural limitation, not a passing component concurrency
test. No C test calls a deleted invocation pointer; a late wakeup requires adapter
ownership/generation checks before touching such a handle.

These tests cover additional primitive cases. They do not claim all cooperative
scheduling interleavings, stale asynchronous completion safety in a real scheduler,
all numeric layouts, component memory/table imports, or allocation-failure paths.
Destructive cancellation also does not certify native I/O quiescence: an adapter
using guest buffers must keep them stable and fence late completion before it
permits memory growth, releases memory, or frees callback state.

## Sanitizer coverage and Windows

`--sanitize` instruments WAMR and its C clients on Windows/Unix. For TinyWasm on
supported Unix targets it uses nightly Rust, `-Zsanitizer=address`,
`-Zexternal-clangrt`, and `-Zbuild-std`, then links C clients with the same external
ASan runtime. This includes Rust dependencies and rebuilt std, rather than merely
sanitizing a C harness. Sanitizer builds are separate from LTO size measurements.

Native Rust ASan does not currently list Windows MSVC as a supported target.
`--sanitize --runtime tinywasm` therefore rejects that configuration. Windows
`--client-asan` explicitly instruments only C clients with an uninstrumented Rust
library; passing it is not a whole-runtime sanitizer result. MSVC runtime
interception does not close that instrumentation gap. See the official
[Rust sanitizer target list](https://doc.rust-lang.org/unstable-book/compiler-flags/sanitizer.html#addresssanitizer)
and [MSVC ASan documentation](https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170).

Every sanitizer mode first runs a separate intentional heap-overflow canary and
requires ASan to detect it. The canary's expected failure is distinct from fixture
results. Windows CI explicitly runs release/C ABI tests, C-client ASan, optional
Rust host-execution tests, and Rust C API unit tests. Linux CI additionally runs
Rust+std+C ASan and Miri. WAMR CI runs C runtime/client ASan on Windows and Linux.
Current receipts, including the physical machine's exact source provenance, are
in `audit_results.md`; a configured CI job is not a passing receipt.

## Gates before an upstream API proposal

1. Choose the supported stackless/stackful scope and model task/stream lifetime
   separately from core invocation lifetime. Define cancellation request/delivery
   separately from destructive execution teardown.
2. Implement an out-of-tree adapter with futures, streams, waitable sets and
   eligible-task scheduling. Prove same-instance progress, buffer/resource
   ownership, memory growth, reentry and late-completion teardown ordering.
3. Run named, pinned upstream assertions through that adapter on both runtimes.
   Report pass/fail/unsupported and the exact supported subset; no silent skips.
4. Run the native Windows matrix plus full supported-host instrumentation, fuzz
   legal and adversarial operation sequences, and repeat disabled-feature artifact
   checks whenever production layouts, allocation or dispatch change.

Until those gates are met, these fork drafts are suitable for prototype review,
not evidence that a common WASIp3 runtime ABI is ready.
