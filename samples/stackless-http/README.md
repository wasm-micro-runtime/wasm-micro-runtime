# Stackless HTTP component on a bounded host

This example serves an actual HTTP response from a validated component using
Canonical ABI streams, futures and stackless callbacks. The same C adapter and
guest are linked against ordinary `wasm.h` calls on WAMR and TinyWasm, with the
experimental stackful continuation feature **off**. The runtime cores, ordinary
import dispatch and disabled-feature layouts are unchanged.

The application world is [handler.wit](handler.wit). This is a deliberately
small Component Model async profile, **not** the `wasi:http@0.3.0` host world.
`handler.component.wasm` is the validated component; `handler.linked.wasm` is
its statically linked core image. The offline linker resolves the canonical
bindings, fuses a memory-only provider into its single consumer, and calls the
core start function after the C API has installed callback environments and
exported memory. There is no component parser, graph linker or file loader on
the board. Both artifacts can be regenerated from `guest.c`.

## Design and scope

The native transport accepts up to four connections. A guest callback receives
bytes in a bounded header buffer and resolves the lifted call with a response
`stream<u8>` and completion `future<u8>`. The producer keeps running after
`task.return`, driven by send capacity and waitable events. `/` serves a short
body and `/large` streams 64 KiB in 1 KiB chunks; the host does not allocate a
whole-body buffer. A request whose header exceeds the 2 KiB limit is rejected.
The demo serves one request per connection, uses plain HTTP, and has no request
body API, TLS, authentication, or general-purpose component loading.

- No interpreter stack is parked across I/O. The adapter distinguishes
  `WAIT`, which requires an event, from `YIELD`, which is ready to run again.
  Its ready lists coalesce notifications and use constant-time enqueue/dequeue.
- A disconnect before `task.return` delivers cooperative task cancellation and
  runs guest cleanup. After return, the native host closes the stream; the
  producer observes the drop and resolves its completion future. The adapter
  validates endpoint kind/ownership, transfer extent and event alignment and
  resolves guest offsets again when copying. Native buffers are only borrowed
  during a transfer; callbacks do not retain guest pointers.
- The host accepts only this built-in guest. A trap poisons the adapter
  instance. Supporting arbitrary untrusted components additionally requires
  instruction budgets, a general type/resource linker, task ownership rules
  and integration fuzzing. The profile does not implement stackful calls,
  cross-component linking, generic Canonical ABI payloads or resource lifting.
- The small WAMR binding accounts for its extra C vector fields and chooses
  an 8 KiB execution stack and no extra app heap. The shared source does not
  promise a binary ABI across the two `wasm.h` implementations.

See [RESULTS.md](RESULTS.md) for the hosted and emulator receipts and exact
limits of the qualification.

The byte transfer path is useful for networking and control messages. It is
not the required bulk path for CapsHost: retain native GPU buffers and command
batches, coalesce frame/fence notifications for GPU/XR, keep PCM in host-owned
rings and never enter Wasm from the audio render callback. Bluetooth events
need bounded copied payloads, ordered admission and explicit backpressure.
The component adapter should carry handles and readiness, with optional buffer
leases for consumers that can prove lifetime and alignment. Asynchronous GPU
and Bluetooth callbacks need owner-thread delivery with generation-checked
completion tokens and teardown fencing before pool slots can be reused. The
server's single-owner socket/PCB lifetime rules are not a general cross-thread
completion mechanism. No webgpu-caps runtime or interface was changed.

## Hosted validation

With CMake, Ninja, Python 3 and Rust available:

```sh
python3 run.py --runtime wamr --root /path/to/wamr --build /tmp/http-wamr \
    --component-tests /path/to/component-model
python3 run.py --runtime tinywasm --root /path/to/tinywasm --build /tmp/http-tiny \
    --component-tests /path/to/component-model
```

Use the pinned Component Model revision `fed10b0b823b82553dd8363f16a640816f5eac65`.
The runner validates the original supported component definitions, executes
their assertions through both cores, and records unsupported assertions.
It also runs real TCP tests on loopback, idle measurement, and seven samples
of a checked 10-million-import `context.get` loop. That metric includes the
guest loop and canonical callback, and is separate from the ordinary-import
control in `ordinary-native.log`. `--sanitize` instruments WAMR runtime and
client or TinyWasm Rust/std/C on a supported Unix host. `--client-asan` is
explicitly client-only for TinyWasm on Windows. Use a new build directory for
instrumented runs. With MSVC, run from the x64 developer prompt.

The executed upstream assertions are in `future-completion-order.wast` (2),
`same-component-stream-future.wast` (12), and `futures-must-write.wast` (5 of 7;
2 require cross-component linking). These are **Component Model async** tests.
The WASI testsuite's `http-service`, `http-service-echo` and `http-service-uri`
target the standard HTTP resource world. The stdio roundtrip targets WASI CLI
streams and futures. None can execute against this application's world; no
official WASI 0.3 interface conformance results are claimed. See the earlier
WASI inventory next to the continuation fixture.

To regenerate the artifacts, use a Clang with the `wasm32` backend and `wasm-ld`,
the `wasm-tools print` CLI, and the pinned local validator:

```sh
cargo build --locked --release --manifest-path tools/Cargo.toml --target-dir /tmp/stackless-tools
python3 build_guest.py /tmp/stackless-guest \
    --validator /tmp/stackless-tools/release/stackless-component-tool
```

## Cortex-M33 simulation and Pico 2 W firmware

The `qemu` and `pico` projects build WAMR with the same fast interpreter profile,
the same statically linked guest, and the same C adapter. TinyWasm's current C
library needs Rust `std`; its tests are hosted, not bare metal.

Set `HANDLER_INCLUDE` to the directory containing `handler.h` from either
`run.py` or `build_guest.py`. With an Arm embedded GCC toolchain:

```sh
cmake -G Ninja -S qemu -B /tmp/http-m33 -DCMAKE_BUILD_TYPE=Release \
    -DWAMR_ROOT=/path/to/wamr -DHANDLER_INCLUDE=/tmp/stackless-guest
cmake --build /tmp/http-m33
python3 qemu/run.py /tmp/http-m33/stackless_m33.elf --output /tmp/http-m33-results
```

QEMU executes the app with a 520 KiB linker RAM region and a 64 KiB allowance
for network buffers. It drives the same HTTP adapter with simulated input and
send capacity, verifies all bytes of the streamed response, exercises
cancellation and resource reuse, and waits for a real emulated timer interrupt
using `WFI`. Its trace counts executed Cortex-M33 instructions. QEMU AN505 is
not an RP2350/CYW43 Wi-Fi emulation; the hosted tests cover real TCP and the
Pico link covers the actual board networking configuration.

For Pico SDK 2.2.0 with `lib/lwip`, `lib/cyw43-driver` and `lib/tinyusb`
initialized:

```sh
cmake -G Ninja -S pico -B /tmp/http-pico -DCMAKE_BUILD_TYPE=Release \
    -DPICO_SDK_PATH=/path/to/pico-sdk -DWAMR_ROOT=/path/to/wamr \
    -DHANDLER_INCLUDE=/tmp/stackless-guest
cmake --build /tmp/http-pico
python3 qemu/run.py /tmp/http-m33/stackless_m33.elf --output /tmp/http-m33-results \
    --pico-elf /tmp/http-pico/stackless_http.elf
```

The Pico firmware uses lwIP RAW callbacks with the SDK's background IRQ mode.
They queue network work; only the foreground owner enters the runtime. The
owner sleeps until a network event or deadline. Wi-Fi credentials are read
at boot from the serial console and are not compiled into the image. The
server listens on port 8080. There is a 10-second connection deadline, a fixed
128 KiB WAMR pool including linear memory, fixed lwIP pools, a capped 16 KiB
libc heap and a 4 KiB native stack. With no picotool, `-DPICO_NO_PICOTOOL=1`
builds an ELF for a debug probe. The serial counters report allocator high
water, foreground service time, I/O wakes and import-loop wall time on a board.
Foreground time excludes sleeping time and is not total CPU or power.
