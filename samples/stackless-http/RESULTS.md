# Stackless component validation (2026-11-02)

The Cortex-M33 simulation completed 34 HTTP responses, one cancellation before
return, and one disconnect after return. It streamed and verified every byte of
one 64 KiB response while three other tasks were live, then recycled the pool
through 32 additional requests. No handles remained live. This executes the
real guest and adapter using simulated network readiness, without a Pico Wi-Fi
peripheral model. Both hosted backends additionally ran real TCP loopback.

| ARM build | SRAM reserved | Detail |
|---|---:|---|
| Pico 2 W firmware with Wi-Fi | 220,264 bytes (215.1 KiB) | Includes 128 KiB WAMR pool, lwIP/driver/USB data, bounded 16 KiB libc heap and 4 KiB stack |
| Cortex-M33 AN505 with 520 KiB linker cap | 229,312 bytes (223.9 KiB) | Includes 64 KiB placeholder for networking, the same WAMR pool and stack/heap bounds |
| WAMR allocation high water in simulator | 95,024 bytes | Already inside the reserved pool; includes the 64 KiB guest memory |

The Pico link uses 41.4% of its SRAM budget. Reservations
are not a measured physical high water. No Pico board was available. The
simulator ran with QEMU 11.1.3 `mps2-an505`, Arm embedded GCC 9.2.1 and LTO;
Pico SDK 2.2.0 is `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779`. Both runs
used the same component and core image. The ELF section counts and hashes
are in [receipts.json](receipts.json).

With one instruction per QEMU translation block, the traced loop executed
464,982 guest instructions for 1,000
`context.get` calls, including the Wasm interpreter loop and call setup.
The `WFI` interval, including its timer wake and interrupt service, executed
22 instructions and had one wake. These are
emulated instruction counts; QEMU TCG is not a cycle-accurate CPU or power model.

| Hosted release | Runtime | Peak process RSS (bytes) | Idle process CPU / interval | Canonical import median ns (range) |
|---|---|---:|---:|---:|
| macOS arm64 | wamr | 2,244,608 | 12 µs / 3.001 s | 13.81 (13.62–15.78) |
| macOS arm64 | tinywasm | 7,241,728 | 12 µs / 3.001 s | 53.55 (50.34–56.34) |
| Windows x64 | wamr | 4,812,800 | 0 µs / 3.031 s | 50.00 (45.31–134.38) |
| Windows x64 | tinywasm | 5,844,992 | 0 µs / 3.016 s | 207.81 (143.75–598.44) |

Each hosted idle run waited once and received no I/O wakeups. Windows CPU
values of zero mean below the resolution of the process CPU clock. RSS includes
host libraries and OS accounting; it is not MCU SRAM. Native-call samples are
seven runs of ten million checked `context.get` imports per runtime. Windows
timings vary substantially; these are measurements of this workload and
configuration, not limits on relative performance. The disabled-feature
controls are separate in the earlier continuation validation. No core
production code changed in this follow-up.

On macOS arm64 and physical Windows x64, both releases completed 25 verified
TCP responses, cancelled three unfinished requests, handled one disconnect
while streaming, and reaped all handles. All eight release/instrumented
combinations in [receipts.json](receipts.json) passed the same 19 pinned
Component Model assertions and the TCP workload. WAMR was instrumented with
runtime and client ASan on both hosts. TinyWasm used Rust/dependencies/rebuilt
std and C ASan on macOS; on Windows only its C clients were instrumented.
The Mac instrumented libraries were reused from the earlier sanitizer audit;
the hosted release and Windows builds used the feature-off runtime.

[conformance.csv](conformance.csv) records the original assertion, pinned
source hash, expected value or trap and results. Two assertions in the
selected files require cross-component linking and were explicitly unsupported.
These are Component Model async results. The original fixture had no standard HTTP or CLI resource bindings.
The separate standard service adapter and its suite coverage are described in
[WASI_HTTP.md](WASI_HTTP.md).

The physical Windows build used MSVC 19.44.35229.0 on a Core i7-8650U with
Windows 11 build 26300 and stable Rust 1.98.1. The base runtime snapshots on the
machine were WAMR `82ef5a7daec8936e454436858154f9585a49bd2f` and TinyWasm
`f9205d275fbc1ca1cc005471f2c6171d3c332d66`, with the adapter compiled separately.
The macOS release link used Clang/LLD 23.1.1. Full raw reports and emulator
trace are retained separately from these portable summaries.
