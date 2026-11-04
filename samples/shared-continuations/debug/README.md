# Mixed native and guest stack audit

Full mixed guest/native crash reporting is a readiness requirement for this RFC,
not a capability established by the continuation and HTTP tests.

The fixture calls Wasm `root -> middle -> leaf -> example:native.call`, then C,
C++, Rust and optionally Swift. It stops inside the native provider, parks the
continuation, and traps after resumption. An early-trap case checks failure
before the first yield. The ordinary-call mode also accepts a WAMR AOT module.
Provider functions use `noinline` so this test can assert native ABI unwind order;
this is not a requirement to disable inlining or tail dispatch in the runtime.

## Observed behavior

On macOS arm64, optimized builds recover the native C/C++/Rust/Swift chain in
LLDB and actual Apple `.ips` reports. Matching dSYM UUIDs are checked. After
yield, the native backtrace contains the scheduler/driver: the guest continuation
has no native stack. A physical backtrace must not invent the old frames.

WAMR previously returned an empty C trap trace for continuation failures even
with `WAMR_BUILD_DUMP_CALL_STACK=1`. The initial error path discarded the trace;
the resumed error path also released frames before capturing them. Both now
reuse the ordinary trap mechanism. Tests require all three guest function
indices for initial, resumed and ordinary failures. This requires the existing
opt-in call-stack feature; the shared C header has not changed.

TinyWasm still returns an empty `wasm_trap_trace`. Its optimized C adapter invokes
external callbacks after returning from the interpreter, so the native backtrace
does not contain an active interpreter frame either. The fixture records this
limitation, not a successful guest-stack qualification.

WAMR ordinary AOT calls preserve native unwinding and the opt-in logical trap
trace, but the tested default AOT build shows an unnamed generated-code address
in LLDB. It does not provide named guest source frames. The separate experimental
`WAMR_BUILD_DEBUG_AOT` compiler configuration requires LLDB development libraries,
which were unavailable in the local LLVM 21 installation. Its DWARF registration
path remains unqualified by this audit.

The fast interpreter's reported offsets are internal compiled-bytecode offsets;
module offsets may be zero. They must not be presented to a Wasm DWARF symbolizer
as original Wasm instruction offsets. Function identity is useful; full source
locations require a retained mapping to the exact original module.

The [recorded observations](observed.json) include native backtraces, minimal
Apple crash receipts, source hashes and the unchanged disabled artifacts.

## Required out-of-tree diagnostic contract

1. Associate each loaded module with its content hash, component/instance identity,
   original Wasm/debug sidecar, and native image UUID or build ID. Preserve the
   component linker's mapping from lowered core functions to original modules.
2. Provide a bounded, versioned, read-only guest-frame snapshot at host-call and
   suspension boundaries, with explicit offset coordinate systems and truncation.
   TinyWasm needs a public capture seam; its current `guest-debug` by-index APIs
   are insufficient. WAMR's allocating trap-trace API is not a fatal-signal API.
3. Keep async ancestry in the external scheduler: task/call generation, parent,
   import identity, and active/pending/completed/cancelled state. Distinguish
   concurrent streams and reused slots. Show an explicit async boundary rather
   than treating a resumed callback as a continuous native stack.
4. Capture no borrowed guest/native pointers across a yield. Publish immutable
   or coherently versioned records for debugger/crash readers; invalidate them on
   cancellation and teardown. Do not allocate, acquire runtime locks, invoke
   callbacks, format strings or re-enter the engine from a fatal-signal handler.
5. Preserve native unwind metadata and archive matching dSYMs/ELF debug files.
   Keep inline/tail-call semantics honest. C++ exceptions, Rust panics and Swift
   errors must be caught/translated before crossing the C ABI; stack inspection
   does not authorize exception propagation through it.
6. Compile diagnostic state and capture work out of embedded builds. Use existing
   runtime frames rather than a second stack updated on every instruction. Bound
   enabled memory and measure capture cost at native-call boundaries, including
   high-frequency GPU/audio imports. Disabling diagnostics must retain the LTO
   binary/RSS/native-call baseline; don't sacrifice tail dispatch for prettier
   native traces.

Native Apple reports alone do not automatically include interpreter/async
records. An out-of-tree reporting integration must attach or reconstruct them.
Apple's [CrashReportExtension](https://developer.apple.com/documentation/crashreportextension)
can inspect a crashed process out of process on its supported platforms; its
documented availability is iOS/iPadOS/macOS 27, not watchOS. It must not become a
runtime dependency or the only diagnostic path. GDB/LLDB readers and an external
crash-context attachment remain necessary for other targets.

These requirements are intentionally not a new public diagnostics ABI in this
patch. Named guest frames, source mappings and async ancestry remain gates before
claiming a complete `Wasm -> component/module -> native provider` crash stack.

## Reproduce

Build the shared runtime fixture with `run.py --runtime wamr --root /path/to/wamr
--build /tmp/runtime --call-stacks`, or the equivalent TinyWasm command without
`--call-stacks`. Use one compiler family for native LTO objects and the final
linker. For WAMR source-level runtime debugging, additionally build its archive
with `RelWithDebInfo` and `-fno-omit-frame-pointer`. The default build flags stay
unchanged.

Run this directory's `run.py` with `--runtime`, `--include`, `--library`,
`--build` and `--debugger lldb` or `--debugger gdb`. WAMR's include directory must
contain its C API header under the shared name `wasm.h`, as the parent runner
arranges. Add `--swift` on macOS. Without it, a C bridge connects Rust to the
checkpoint; this is the Linux GDB CI configuration.

`--apple-crash` deliberately traps the standalone fixture, checks a new Apple
`.ips` report for the native frame order and matching dSYM UUID, and symbolicates
its image addresses with `atos`. Run it only when intentionally producing a test
crash. The script records a minimal receipt, not the report's machine identifiers.
`--ordinary --guest /path/to/guest.aot` checks ordinary AOT calls; compile
`guest.wasm` with `--enable-dump-call-stack` and matching GC/bounds-check settings.

The runner writes `debugger.txt`, `guest_trace.txt`, and `receipt.json`; Apple
crash tests also write `apple_symbolicated.txt`. It explicitly records
`full_mixed_guest_stack_qualified: false`, even when native unwinding passes.

References: [Apple symbolication](https://developer.apple.com/documentation/xcode/adding-identifiable-symbol-names-to-a-crash-report),
[GDB JIT interface](https://sourceware.org/gdb/current/onlinedocs/gdb.html/JIT-Interface.html),
[LLVM debugging generated code](https://llvm.org/docs/DebuggingJITedCode.html).
