# Optional guest and async diagnostics

The continuation drafts now provide bounded logical guest snapshots, original
Wasm source offsets, terminal asynchronous host failures, and consumer-owned
async ancestry. `diagnostic_run.py` checks these alongside native C → C++ →
Rust → Swift → C unwinding. These are logical Wasm/async segments attached to
a native trace; suspended interpreter frames are not native machine frames.

Enable `WAMR_BUILD_C_API_DIAGNOSTICS=1` with continuations, or TinyWasm's
`diagnostics` Cargo feature. Both are excluded from default builds. The
experimental `wasm_ext_debug.h` observer ABI is source-compatible between the
runtimes and separate from the standard Wasm C API. It does not promise a
stable cross-runtime binary ABI for the underlying `wasm.h` objects.

`wasm_ext_call_observe` registers before first resume. Safe-boundary events
cover start, host entry, pending, resume, guest trap, cancellation, completion,
and asynchronous failure. Each snapshot is 528 bytes: a versioned header and
at most 32 youngest-first frames, with explicit truncation and mapping flags.
No locals, memory bytes, runtime pointers, or managed references escape.
An observer must copy the borrowed record, remain on the owner thread, and
never reenter the runtime, throw, or longjmp. Capture allocates no memory.

`wasm_ext_call_fail` terminates a pending host operation with an owned trap.
Duplicate/late failure is rejected; the producer already completed, so its
cancellation callback is not invoked. C++ and Swift exceptions are caught
inside their bridges; Rust `Result` errors are translated there. Rust panics
under `panic=abort` remain native crashes. This API does not inject a catchable
Wasm exception or unwind a language exception through C. TinyWasm also tests
source capture before an uncaught guest exception unwinds its frames.

## Consumer pieces to move out of tree

`recorder.[ch]`, `debugger.py`, `symbolize.py`, the language bridges, source
manifest, and all runners are review/reproduction fixtures, not runtime
policy. The recorder has one writer and two publication banks (12,264 bytes
in total). Readers stop the process in a debugger or inspect it after death.
They never read a concurrently mutating bank. Interrupted publication leaves
the preceding committed bank available. This is a process-crash transport,
not a power-loss journal.

Each task has a monotonically increasing, non-reused generation ID. Spawn
copies up to four parent segments; propagation copies up to four failure
causes. Parent destruction or slot reuse cannot retarget those records.
Both limits and the 128-byte error-message limit report truncation. The
scheduler must check generations before using a call pointer: these APIs
cannot make a deleted C pointer safe. Cancellation remains terminal core
cancellation, distinct from Component Model cooperative task cancellation.

The sample loads one original guest module into separate stores. Its immutable
manifest associates store-local instance ordinals with the exact Wasm and
source hashes. A multi-module consumer must maintain that association for
each task's store/module graph; ordinals alone are not global module identity.
A launch nonce and native executable hash identify the manifest. Published
records contain its SHA-256 and process ID. Symbolization rejects a mismatched
manifest, module, or source rather than guessing a plausible line.

Apple's ordinary `.ips` report supplies native PCs. The runner checks the
crashed process ID and executable/dSYM UUID, symbolicates with `atos`, and
joins the mapped sidecar's guest source/async segments into a combined report.
It installs no signal handler and invokes no runtime code during fatal signal
handling. The standard Apple report itself is not rewritten. Raw machine
crash reports are not committed. `wasm-bt` in GDB/LLDB reads the stopped
process's published POD through debugger memory APIs, without target calls.

## Source coordinates and costs

The loaders retain original Wasm **file offsets**, not internal IR offsets.
WAMR maps the fast interpreter's compiled bytes; TinyWasm tracks instruction
selection, fusion, checkpoints and caller call sites. Fused instructions map
to their final operation. Both check source-call lines in a Clang DWARF guest.
Unmapped offsets stay explicit. Completed tail-eliminated callers are not
invented. The guest fixture disables compiler sibling-call optimization to
provide a known three-frame oracle; production builds need not do that.

Enabled source maps cost four bytes per compiled byte in WAMR and four bytes
per IR instruction in TinyWasm, plus bounded snapshot/observer state. TinyWasm
stores module-local function indices at instantiation, keeping capture work
bounded by stack depth rather than module function count. Diagnostic metadata
is deliberately omitted from TinyWasm archives: a cached archive is unmapped
unless the consumer restores matching debug metadata. Load original Wasm for
this source-qualified configuration. WAMR AOT stack suspension is still outside
this contract; the older `run.py --ordinary` checks ordinary/AOT native
unwinding, not this interpreter source-snapshot API. JIT, arbitrary sampling
of actively executing Wasm, and physical Watch/Pico qualification are not
claimed by these fixtures.

When diagnostics are compiled out, source maps, snapshot fields and capture
calls are absent. The disabled-build comparison covers both continuation-on
and continuation-off builds with LTO and whole-program dead stripping. See
`diagnostic_receipts.json`, `disabled_profile.json`, and
`qemu_diagnostics_disabled.json` for current evidence, configuration and limits.
Timing/RSS measurements are process samples, not mathematical proof of zero
noise. Code and loadable-section comparisons establish whether instrumentation
or storage was retained. Runtime source compatibility across feature sets and
maintenance cost of the loader hooks remain RFC feedback topics.

## Disabled-build measurements (2026-11-04)

Both continuation-on and continuation-off builds keep diagnostics disabled.
LTO, whole-program dead stripping, executable sizes and every loadable-section
size match the pre-diagnostics baseline. The complete ordinary-native-call and
continuation-resume disassembly hashes match. Both WAMR executables are
byte-identical; TinyWasm has metadata/cold-layout differences, so no whole-file
identity is claimed there.

| Runtime | Continuations | Executable bytes, before = after | Native call ns, median before → after |
| --- | --- | ---: | ---: |
| WAMR | on | 228,104 | 14.741 → 14.748 |
| WAMR | off | 210,456 | 14.371 → 14.422 |
| TinyWasm | on | 2,318,552 | 81.360 → 77.725 |
| TinyWasm | off | 2,277,256 | 71.229 → 70.676 |

Nine paired, alternating process samples have overlapping RSS ranges. WAMR's
RSS medians match; TinyWasm's move by +48 KiB (continuations on) and +16 KiB
(off). These are not evidence of exact zero process-RSS variation or a speedup.
A separate instrumented fixture samples macOS allocator state after the short
semantic workload, before teardown, using the same executable path for both
builds. All five before/after samples have identical live block counts, live
heap bytes and allocator-reserved bytes in all four configurations. This is a
live-heap check, not a peak-heap measurement. Together with the absent compiled
fields/hooks and matching code/section sizes, it checks that diagnostics add no
retained storage or native dispatch work in these disabled configurations.

The rebuilt WAMR HTTP/2 Cortex-M33 image remains byte-identical and reserves
339,104 of 532,480 bytes (including the existing network allowance). This is
scripted QEMU readiness, not a physical Pico or a diagnostics-enabled image.

## Reproduce

Build a runtime with `../run.py --diagnostics` (add `--gc`, `--call-stacks` for
WAMR where desired), then run, with `wasm-tools` 1.247.0 on PATH:

```sh
python3 diagnostic_run.py --runtime wamr --include BUILD/include \
  --library BUILD/runtime/libiwasm.a --build BUILD/debug \
  --debugger lldb --swift --apple-crash
# TinyWasm: --runtime tinywasm --include ROOT/crates/c-api/include
#           --library BUILD/runtime/release/libtinywasm.a
# Linux: --debugger gdb, omit --swift and --apple-crash.
python3 test_symbolize.py
```

The runner deliberately crashes child fixture processes for `--apple-crash`.
It checks host-entry, parked, failed-await and parent source locations, native
frame order, and actual Apple report production. Separate runtime/C ASan runs
use `../run.py --diagnostics --sanitize`; `diagnostic_run.py --sanitize
--skip-debugger` also exercises the native bridge/error driver. Windows shared
C CI exercises bounded capture/failure without claiming Swift or Apple support.

Regenerate the source fixture with Clang targeting wasm32, `-g -O1
-fno-optimize-sibling-calls -nostdlib -Wl,--no-entry -Wl,--export=guest_root
-Wl,--allow-undefined`, mapping the source directory and compilation directory
to `/guest`. Keep the exact original Wasm, source hash and native dSYM together.
`disabled_profile.py` compares paired macOS baseline/current LTO builds and
records all timing/RSS samples. `heap_profile.py BUILDS --fixture
../fixture.c --tiny-include TINY_ROOT/crates/c-api/include --output heap.json`
rebuilds separate allocator probes from those libraries. `observed.json` retains the earlier native-only
audit as historical evidence; its missing-feature conclusions are superseded
by the optional diagnostics implemented here.
