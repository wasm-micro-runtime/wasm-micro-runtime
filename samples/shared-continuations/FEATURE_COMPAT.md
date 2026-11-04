# Optional continuation feature compatibility

Validated on macOS arm64 on 2026-11-03. This increment supports guest GC and SIMD
across interpreter suspension and allows ordinary AOT modules in the same WAMR
build. The shared numeric C header is unchanged. AOT functions still cannot be
suspended through `wasm_ext_call_new`; the stackless component adapter uses
ordinary calls and works with AOT.

WAMR resumes the GC local-reference boundary along with the parked frame. The
regression first reads a live reference after resumption, allocates 2 MiB of
garbage, then checks the reference again. Without restoring that boundary it
fails the result assertion. Parent and leaf frames, repeated suspension,
indirect host calls, cancellation followed by collection, and traps are covered.
The same source fixture passes against TinyWasm's existing rooted execution API.
SIMD values stay inside the guest; reference/vector-valued C signatures remain
outside this prototype.

WAMR now uses the instance's existing stack budget for each continuation.
`wasm_instance_new_with_args` can choose it without a new extension ABI. Tests
check that a 4 KiB budget traps on deep recursion and a 64 KiB budget completes.
The ordinary C API constructor supplies 32 KiB, replacing the prototype's
additional fixed 64 KiB per invocation.

The AOT HTTP tests exposed an independent runtime bug: `call_indirect` treated
C API imports as native-ABI functions and could pass an invalid callback
environment. It now uses the existing C API import dispatcher. A separate base
C API regression covers callbacks with and without environments and host traps,
including AOT builds without the continuation extension or GC.

| Check | Result |
|---|---|
| WAMR GC/SIMD interpreter continuations | Release and runtime/client ASan pass |
| WAMR ordinary AOT GC/SIMD guests, suspension rejected | Release and runtime/client ASan pass |
| TinyWasm C GC/SIMD continuations | Release and Rust/std/C ASan pass |
| WAMR base C API tests in AOT mode | 56 passed |
| WAMR continuation CTest suite with GC | 3 passed |
| TinyWasm Rust host-execution tests | 7 passed, including explicit GC while pending |
| Official WASIp3 HTTP subset through WAMR AOT | 7 passed in release and ASan, 11 outbound cases unsupported |
| Stackless HTTP/2 through WAMR AOT | Release and ASan pass, 16 streams across 4 connections |
| Windows x64 | Not rerun; SSH timed out. CI now includes GC and applicable SIMD checks |
| Apple Watch | Not deployed or qualified; hosted results do not establish watchOS execution permissions or memory limits |

The HTTP/2 run verified 4 KiB JSON/PNG GETs, 16 KiB PNG POSTs, closed flow-control
windows, resets, stream refusal, TCP admission limits and recovery. It completed
4,828 responses and shut down with zero live handles, resources and protocol
allocations. The release process reported 3,047,424 bytes peak RSS. These are
bounded example workloads, not a budget for an arbitrary component application.
ASan instruments the runtime and C adapter, not generated AOT code; nghttp2 is
the existing release archive. Guest memory uses software bounds checks.

## Embedded and disabled-feature footprint

The WAMR Cortex-M33 HTTP/2 ELF is byte-identical to the previous RFC build:
`b8889f994b926dfb91799e00d74a5251b215e01b8a4759e33eb41f719762f495`.
It still passes QEMU with four streams over two connections and reserves
339,104 of 532,480 SRAM bytes, including a 64 KiB networking allowance. This
configuration leaves the continuation extension, GC, SIMD and AOT disabled.
It uses the external stackless adapter and scripted I/O; it is not Pico hardware.

The hosted feature-off WAMR client is also byte-identical before/after this
increment: 227,096 bytes, including identical code, data, BSS and TLS sections.
Seven alternating measurements gave overlapping peak RSS ranges of
6,193,152–6,225,920 and 6,176,768–6,225,920 bytes. Native calls measured
14.39/14.26 ns and compute iterations 2.94/2.94 ns before/after. The identical
artifact establishes unchanged instructions and layouts; noisy RSS samples do
not prove exact process-memory equality. TinyWasm runtime sources and default
features are unchanged in this increment.

For the larger WAMR build with GC/SIMD/AOT and continuations enabled, the same
ordinary-call client measured 14.61 ns/native import with interpreted code and
9.60 ns/import with AOT, medians of seven alternating runs. This isolates an
ordinary scalar import, not a pending callback or GPU/audio transfer. Both use
the existing native-call path. The HTTP copy transport is not a bulk GPU or
real-time audio interface.

Raw data: [receipts](feature_receipts.json), [sections](feature_off_sections.csv),
[disabled measurements](feature_off_measurements.csv),
[native-call measurements](feature_native_measurements.csv).

## Reproduce

Use the shared `run.py` in this directory with the runtime's native compiler.
Add `--gc --simd` to test both guest features. For WAMR, also pass
`--aot-compiler /path/to/wamrc`; `--aot` alone only builds the AOT backend.
Use `--sanitize` for the separate instrumented build. On Windows use `--gc`
for WAMR; its fast interpreter has no Windows SIMD implementation. TinyWasm
Windows uses `--client-asan`, which does not instrument Rust.

The checked receipts use Clang 23.1.1, LLVM 21 for wamrc, and Rust nightly
1.100.0 (2026-08-27). LLVM 23 cannot build this revision of wamrc. The runner
passes `--bounds-checks=1 --stack-bounds-checks=1` when compiling AOT, matching
the runtime's software-check configuration, and `--enable-gc` for a GC build.

Build the external HTTP adapter as described in its WASI_HTTP.md with the mixed
runtime archive. Its `wasi_http_tests.py` accepts
`--aot-compiler /path/to/wamrc --aot-gc` to compile each linked official guest
and run the same assertions. For HTTP/2, compile the linked demo core with the
same flags, embed its bytes as `handler_wasm` in `standard_handler.h`, and use
that directory for `P3_H2_HANDLER_INCLUDE`. Run the unchanged `test_wasi_h2.py`.

JIT, hardware-trap bounds checking and WAMR multi-module continuations remain
excluded. The shared extension still has one live invocation per store, one
numeric result, thread confinement, destructive cancellation and no exposed
fairness budget. AOT stack suspension, general reference-valued C calls, shared
memory, arbitrary component graphs and watchOS deployment need separate work.
All schedulers, component bindings, transport and fixtures remain consumer code
intended for later out-of-tree extraction.

See the [mixed-stack audit](debug/README.md) for native debugger/crash-report
coverage and the remaining guest-frame and async-ancestry requirements.
