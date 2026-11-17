# WASI 0.3 HTTP service fixture

The optional adapter hosts a component exporting
`wasi:http/handler@0.3.2`. It keeps the HTTP resources, scheduling,
socket readiness, and memory budget in the embedder. The runtime uses the
same core Wasm C API for both backends. Each connection has its own task
context, input stream, response stream, and completion futures. The firmware
uses one Wasm instance and a fixed table of four connections.

The offline linker validates the input component and resolves its canonical
lifts, lowers, and wit-component shim table. The two runtimes receive a core
image whose native imports are resolved once at instantiation. Existing
canonical imports keep their callback path. HTTP methods use a separate
callback with signatures checked during binding. The linker accepts the
single-memory, single-instance graph used by these guests.

Resources use checked type, ownership, and generation. Host headers and
URI data live in bounded native storage. The adapter checks guest-memory
ranges, reacquires memory after guest allocation, and rejects HTTP imports
reentered by `cabi_realloc`. Native body transfers borrow buffers only for the
duration of a copy. The guest returns HTTP response metadata independently
of body production and trailer completion. The demo echoes a POST in
bounded chunks and returns an `x-upload-check` trailer. Closing a transport
releases the body endpoints and wakes the guest's pending work.

The HTTP/1.1 transport serves one request per TCP connection. It bounds
headers to 2 KiB and accepts up to 64 KiB of content-length-framed request
body. It rejects duplicate content lengths and transfer-encoded requests.
Responses use chunked framing for streaming bodies. Extra connections are
closed on admission when four slots are in use. The firmware also uses the
existing lwIP connection deadline. The host example binds to loopback.
A guest trap terminates this fixture's shared instance. Outbound
`wasi:http/client.send`, inbound chunked uploads, and HTTP/2 and HTTP/3
multiplexing are outside this profile.

## Reproduce

The test suite and WIT are pinned to artifact commit
`9d84631e427729802085c3d172d0a63cd360a1fa` from
[WebAssembly/wasi-testsuite](https://github.com/WebAssembly/wasi-testsuite/tree/9d84631e427729802085c3d172d0a63cd360a1fa).
The artifact commit references source commit
`de7fbe267210b52f63893270403177239b4f4a9b`. Use Python 3.10 or newer, a Clang
toolchain with wasm32 and wasm-ld, and wasm-tools 1.247.0.

From this sample directory, set `SUITE` to the suite checkout and
`GUEST` to an absolute build-output directory.

```sh
git -C "$SUITE" fetch origin prod/testsuite-base
git -C "$SUITE" checkout --detach 9d84631e427729802085c3d172d0a63cd360a1fa
python3 build_wasi_http_guest.py "$SUITE" "$GUEST"
```

Use the adapter build directory from the hosted instructions in
[README.md](README.md) as `ADAPTER`. Run the same steps with each runtime,
including its instrumented build if available.

```sh
cmake -S . -B "$ADAPTER" -DP3_WASI_HANDLER_INCLUDE="$GUEST"
cmake --build "$ADAPTER" --target wasi_http_server wasi_http_check
python3 wasi_http_tests.py "$SUITE" "$ADAPTER"
python3 test_wasi_http_load.py "$ADAPTER/wasi_http_server"
"$ADAPTER/wasi_http_server" 0 3
"$ADAPTER/wasi_http_server" --native
```

The suite runner validates the original components, exercises the upstream
service JSON assertions and CLI resource guests, and lists outbound cases
as unsupported when the linker finds the client import. The official
guests start with 17 Wasm memory pages. The freestanding workload uses
the same upstream service world and one page. The load test validates exact
4,096-byte JSON and PNG downloads and 16,384-byte PNG uploads with echo.
It runs one through four concurrent connections for each request mix, then
holds four connections open and probes overload in a burst of four. It also
checks recovery after disconnect and rejects ambiguous framing, excessive
body/header size, and invalid UTF-8 paths.

For WAMR on Cortex-M33, use the same generated `GUEST` directory and set
`WAMR_ROOT` and `PICO_SDK_PATH` to absolute paths. The QEMU run uses the
AN505 memory map, a 520 KiB application region, and scripted transport
readiness. The separate firmware link includes the Pico SDK, Wi-Fi and lwIP.

```sh
cmake -G Ninja -S qemu -B build/m33 -DCMAKE_BUILD_TYPE=Release \
  -DWAMR_ROOT="$WAMR_ROOT" -DHANDLER_INCLUDE="$GUEST" -DP3_WASI_HTTP=ON
cmake --build build/m33
cmake -G Ninja -S pico -B build/pico -DCMAKE_BUILD_TYPE=Release \
  -DWAMR_ROOT="$WAMR_ROOT" -DHANDLER_INCLUDE="$GUEST" -DP3_WASI_HTTP=ON \
  -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPICO_NO_PICOTOOL=1
cmake --build build/pico
python3 qemu/run.py build/m33/stackless_m33.elf \
  --pico-elf build/pico/stackless_http.elf --wasi-http --output build/m33-report
```

The regular adapter targets and core runtime libraries can be built without
the HTTP feature. Neither runtime core needs an HTTP dependency. Firmware
without `P3_WASI_HTTP` continues to use the original fixture.

## Validation

| Configuration | Official service and resource cases | Outbound cases |
|---|---:|---:|
| WAMR macOS arm64 release and runtime/client ASan | 7 passed in each build | 11 unsupported |
| TinyWasm macOS arm64 release and Rust/std/C ASan | 7 passed in each build | 11 unsupported |

The three service cases exercise the standard handler over TCP. The four
resource guests exercise fields, request, response, and request-options.
An additional run closed a partial upload while the official echo handler
was waiting. Both runtimes cancelled the task, then served a fresh request
and shut down with zero live resources and endpoints.

The separate load test held four connections at a time. It verified GET
bodies, POST echo bodies, and recovery after overload and disconnects.
Each release completed 36 responses including five expected protocol
rejections, released 13 disconnected sockets and rejected 12 overflow
connections. Overflow probes observed TCP establishment followed by EOF
or reset. Peak endpoint occupancy was 49 of 64 slots. The suite runner
and workload finished with zero live handles and resources in all four
hosted builds.

| Memory accounting | Bytes | Scope |
|---|---:|---|
| Cortex-M33 QEMU application reservations | 300,128 | Includes a 64 KiB networking allowance, 128 KiB runtime pool, 16 KiB libc heap, and 8 KiB stack |
| Pico 2 W Wi-Fi firmware reservations | 291,072 | Includes the linked lwIP and Wi-Fi build, with the 8 KiB stack in main SRAM |
| WAMR pool high-water mark in QEMU | 111,712 | Within the 128 KiB pool, including one 64 KiB Wasm page |
| libc heap high-water mark in QEMU | 2,968 | Within the 16 KiB heap |

QEMU completed two GETs and two POST echoes with short transport windows
and a delayed reader. It verified the response bytes and upload trailers,
then disconnected a pending upload and reaped all handles. The QEMU
execution uses scripted readiness and the Pico SDK result is a firmware
link. No physical Pico or radio was available. Both Cortex-M33 binaries
used LTO and the standard profile's larger stack for nested
`cabi_realloc` calls.

| Hosted release | Idle process CPU over 3 s | Median CPU ns per `context.get` |
|---|---:|---:|
| WAMR | 17 µs | 22.34 |
| TinyWasm | 20 µs | 81.28 |

Both idle processes waited once and saw zero I/O wakeups. The native
samples each execute ten million calls through the Wasm C API and include
the interpreter loop. The corresponding QEMU trace executed 465,016
instructions for 1,000 calls, and 22 instructions across the simulated
`WFI` interval and timer wake. These are emulator instruction counts.
The Windows SSH host timed out during this follow-up. Earlier Windows
results in [RESULTS.md](RESULTS.md) cover the original numeric fixture.

The per-case outcomes, workload counters, timing samples, and binary
hashes are in [wasi-http-receipts.json](wasi-http-receipts.json).
