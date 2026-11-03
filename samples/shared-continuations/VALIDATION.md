# Shared C continuation validation

For the later async-semantics audit, explicit conformance gaps, Windows CI and
instrumentation coverage, read [READINESS.md](READINESS.md) and
[audit_results.md](audit_results.md). The measurements below are the original
primitive validation, not a WASIp3 conformance result.

Measurements taken 2026-11-02. These qualify a numeric, thread-confined core-module
prototype, not a complete WASI 0.3/component runtime or a Pico deployment.
The TinyWasm changes are independent of PR #81.

## Builds and fixtures

The unmodified snapshots were TinyWasm `ef5ded81f1fb0d01393d7ef55b8d258f71c5c216`
and WAMR `48381c35ebebc5349b52b362f464874672cd38bb`. Compare each runtime to its own
snapshot with the same source fixture, allocator, panic strategy, toolchain, and
link flags. `base` means unmodified; `off` is this change with the feature omitted;
`on` enables the feature but the benchmark client never references the extension.
The feature-used conformance executable is built separately.

macOS arm64 used Clang/LLD 23.1.1, Rust nightly
`1.100.0-nightly (e457a7b0d 2026-08-27)`, release `panic=abort`, Rust fat LTO and
one codegen unit; WAMR used thin LTO, `-O3 -DNDEBUG -fvisibility=hidden`, function
and data sections. The C client used `-O3 -flto=thin -fuse-ld=lld`, hidden visibility,
`-Wl,-dead_strip`, then `llvm-strip -S -x`. All variants used the filename `probe`.
Use `-Wl,-object_path_lto,<existing-directory>` with the tested macOS LLD for its
intermediate object. Final C LTO does not imply Rust/C cross-language inlining.

Windows x64 used an Intel Core i7-8650U, Windows 11 Pro build 26300,
VS 2022 MSVC 19.44.35229.0 and Rust 1.98.1 (48a229cea 2026-09-01).
Runtime C compilation used CMake IPO; the client used `/O2 /GL /Gy /Gw`,
`/LTCG /OPT:REF /OPT:ICF /Brepro /INCREMENTAL:NO`. Rust used the same fat-LTO,
one-codegen-unit, abort strategy. No whole-archive retention was used.
WAMR used the fast interpreter with software bounds checks and no AOT/JIT,
SIMD, WASI libc, or builtin libc. MSVC and stable Rust use switch/match dispatch;
these numbers do not compare the fastest dispatch configuration of either runtime.

`run.py` records reproducible build commands for either backend. It compiles the C fixture
with warnings as errors separately from the LTO link, where existing backend warnings
(such as GCC's WAMR table-type warning) remain visible without becoming client errors. WAMR's public
`wasm_c_api.h` was copied as `wasm.h` for the unchanged C client. Define
`LIBWASM_STATIC` and `WASM_API_EXTERN=` for static MSVC clients. `measure.py`
alternates the executable order for seven rounds and emits all samples. Use
`sections.py variant=/path/to/image` for Mach-O64/PE section hashes; use
`llvm-size -A`/`readelf -S` for ELF. CSV receipts accompany this report.
The shared C fixture SHA-256 is
`d38be8465b16cfbdf09e752637e5750738b461a8899795ad7f4c2667a6006cdd`.

## Disabled-feature artifacts

| Host / runtime | Baseline bytes | Disabled bytes | Enabled, unused bytes |
|---|---:|---:|---:|
| macos / wamr | 219328 | 219328 | 219328 |
| macos / tiny | 1633552 | 1633552 | 1633632 |
| windows / wamr | 355840 | 355840 | 360960 |
| windows / tiny | 1736704 | 1736704 | 1737728 |

Baseline and disabled code section SHA-256 hashes match exactly on both hosts.
All loadable section sizes, including writable data, BSS and TLS, also match.
Windows `.data` hashes match; macOS TinyWasm has equal-sized relocation/constant
metadata with some different bytes, so complete file identity is not claimed.
Disabled static libraries retain no `wasm_ext_*`, `HostExecution`, or
`PendingHostCall` symbols. Opt-in store/environment fields preprocess out; ordinary
interpreter, executor and native callback dispatch retain no continuation hooks.
Identical code gives stronger evidence of no added instructions than noisy timing
ratios. This is a result for these static-link configurations; exported shared
libraries, other feature combinations and other toolchains need their own audit.

| Host / variant | Compute ns / iteration | Native ns / import | Windows cycles / import | Peak RSS bytes, min–max |
|---|---:|---:|---:|---:|
| macos-arm64 / wamr-base | 4.6825 | 22.7907 | — | 2179072–2211840 |
| macos-arm64 / wamr-off | 4.6521 | 22.8135 | — | 2162688–2211840 |
| macos-arm64 / wamr-on | 4.6756 | 23.0202 | — | 2179072–2211840 |
| macos-arm64 / tiny-base | 5.4229 | 105.5251 | — | 6848512–6914048 |
| macos-arm64 / tiny-off | 5.3573 | 118.2820 | — | 6897664–6946816 |
| macos-arm64 / tiny-on | 5.3012 | 125.8884 | — | 6832128–6864896 |
| windows-x64 / wamr-base | 10.1562 | 92.1875 | 199.3785 | 4595712–4599808 |
| windows-x64 / wamr-off | 9.0625 | 62.5000 | 130.0800 | 4595712–4603904 |
| windows-x64 / wamr-on | 8.1250 | 67.1875 | 144.7130 | 4599808–4603904 |
| windows-x64 / tiny-base | 12.6562 | 525.0000 | 1111.8623 | 4276224–4276224 |
| windows-x64 / tiny-off | 11.5625 | 475.0000 | 1002.2335 | 4272128–4276224 |
| windows-x64 / tiny-on | 14.8438 | 453.1250 | 951.3873 | 4427776–4427776 |

Each compute sample performs 100 million loop iterations; the native sample makes
10 million ordinary C API host imports. Results are checked deterministically.
Values above are medians of seven alternating rounds; all samples and peak process
RSS/Windows commit values are in `measurements.csv`. Windows uses process CPU time
(`GetProcessTimes`) and main-thread cycles (`QueryThreadCycleTime`), rather than
MSVC's elapsed-time `clock()`. Windows children were pinned to logical CPU 2;
macOS was unpinned. Frequency changes, allocation placement, and hosted-process
noise still affect the results. The timing variation does not establish tight percentage bounds on runtime overhead.
The matching disabled code hashes establish the absence of additional compiled instructions;
these hosted timings and RSS samples do not prove an exact zero difference in elapsed time or RAM.

Process RSS and committed memory are high-water marks, not isolated store or live
heap measurements. Baseline/off static RAM and instructions match, while measured
process-memory values vary in pages. No claim of exactly equal runtime RSS is made.
The TinyWasm C wrapper currently requires std. No physical Pico SRAM, stack, heap,
or cycle qualification has been performed; cross-host figures are not MCU budgets.
The enabled WAMR invocation separately allocates a 64 KiB interpreter stack.

## Safety and continuation coverage

The identical C source passed on macOS arm64 and physical Windows x64, including
pending/resume/cancel/delete, cancellation before start, exactly-once notification,
no callback replay, repeated suspension, indirect calls, invalid initial/completion
types and counts, store-call exclusion, immediate completion, malformed host result
rejection, host and guest traps, and mixed i32/i64 arguments with high result bits.
The malformed-result case ensures a numeric adapter rejects reference-kind union
bits before attempting reference ownership or conversion. A prior synchronous trap
must not poison a later continuation. The feature-used fixture also exercises
ordinary native imports through the opt-in driver and prints that cost separately.

TinyWasm's C adapter unit tests, seven Rust host-execution integration tests,
fallback dispatch tests, no-default-feature check, formatting and strict Clippy
passed. Miri passed the owned C adapter's pending/resume/cancel, actual guest
execution and ordinary native-import forwarding. The exclusive store reservation
keeps the owned execution borrow valid; it is dropped before inspecting result
storage. Pending numeric values are checked before committing the continuation.

WAMR's new CTest suite reuses the shared C source and generates the main module
from WAT. AddressSanitizer found no address errors in the exercised paths.
UBSan alignment warnings occur in the baseline fast loader/interpreter/table
implementation as well; this is not a clean all-UBSan result. New extension packing
uses memcpy for potentially unaligned mixed-width arguments. Repository naming,
license and staged formatting checks passed. The full preset clang-tidy script
could not configure `all-on-fast-jit` on this host; that check remains unqualified. A targeted core-analyzer scan of the enabled C API
translation unit reproduces the baseline warning about `func_import` in import setup;
no new adapter diagnostics remain in that scan. This is not a clean full static-analysis claim.

## Windows profiling and remaining work

VTune 2024.2 hardware event-based sampling completed for both feature-off clients.
The workload included both compute and native imports. Summary and hotspot receipts
are preserved separately from unprofiled timing rounds. WAMR retired about 20.27
billion instructions with sampled CPI 0.256; TinyWasm about 34.99 billion with CPI
0.331 in that collection. These are whole-fixture counts, not per-call attribution.
The optimized images lack internal PDB symbols: internal addresses are unsymbolized.
TinyWasm's `RtlFreeHeap` samples account for 8.7% of sampled CPU time, making native
callback allocation a useful next investigation, not a proven sole bottleneck.

The ordinary native-import gap remains significant. Selected async imports must
pay for owned pending state; unselected synchronous imports use TinyWasm's existing
stack callback API before argument-vector materialization. Scheduling, wakeups,
futures and resources stay out of the core. Further optimization can change the
ordinary interpreter and native callback implementation independently of this API.

`wasm_val_t` was 16 bytes in both builds, but `wasm_val_vec_t` was 16 bytes in
TinyWasm and 40 in WAMR. This fixture establishes shared C source behavior, not a
binary plugin ABI. Normalize or adapt the base vector ABI before freezing a common
header. C caller-context access, nested canonical realloc/post-return operations,
imported memory/table parity, reference signatures, full component task concurrency,
AOT/JIT and additional targets remain separate acceptance gates. Only pending host
boundaries suspend; cancellation cannot preempt guest loops or blocking callbacks.
The component-parser branch review is documented in the fixture README.

Reproduce the Windows collection with VTune 2024.2:

```text
vtune -collect hotspots -knob sampling-mode=hw -knob enable-stack-collection=false -result-dir profile -- fixture.exe fixture.wasm
vtune -report summary -r profile -format csv
vtune -report hotspots -r profile -format csv
```

`windows_profile.csv` preserves summary metrics and the collected per-function CPU samples;
internal addresses remain unsymbolized. Full VTune result directories remain on the test device.
