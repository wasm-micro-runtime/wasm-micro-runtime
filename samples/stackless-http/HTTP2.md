# Bounded HTTP/2 fixture

This opt-in transport serves the same `wasi:http@0.3.2` component using
HTTP/2 prior knowledge over loopback TCP. nghttp2 owns framing, HPACK and
protocol flow control. The embedder maps each stream to an independent
WASI task and keeps one shared guest instance. A 2 KiB receive ring per
stream bounds pending uploads. The server returns window credit when the
guest consumes bytes or the transport discards them after cancellation.
Response production follows nghttp2's send windows. A blocked stream
defers its producer while siblings can continue.

The hosted fixture admits four connections, eight streams per connection,
and sixteen across the process. Each nghttp2 connection has a 64 KiB
allocation cap. The Cortex-M33 fixture uses two connections, four
aggregate streams, and a 28 KiB protocol allocation cap per connection.
Its libc heap and Wasm pool are separately bounded by the application
linker and allocator. The fixture covers cleartext prior knowledge,
request bodies, response trailers and stream resets. TLS/ALPN, upgrade
negotiation and inbound request trailers are outside this profile.

## Review scope

The shared C continuation contract is the runtime-facing proposal. A later
commit should move the WASI bindings, offline component linker, scheduler,
HTTP/1.1 and HTTP/2 transports, example guests, and test harnesses to an
out-of-tree consumer. Both drafts carry these pieces so reviewers can
trace the seam end to end and reproduce the evidence against either
runtime. The opt-in example targets own all HTTP dependencies.

## Build and run

Use the pinned WASI suite and hosted adapter build from
[WASI_HTTP.md](WASI_HTTP.md). The host fixture needs a sixteen-task guest
and nghttp2 v1.70.0 at `85e300c79fb6dbcfa9c1013215c8710c1c2cd3d2`.
The independent client uses `h2==4.3.0`.

```sh
python3 -m venv "$CLIENT"
"$CLIENT/bin/pip" install 'h2==4.3.0'
python3 build_wasi_http_guest.py "$SUITE" "$H2_GUEST" --tasks 16

cmake -S "$NGHTTP2" -B "$NG_HOST" -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_LIB_ONLY=ON -DBUILD_TESTING=OFF \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
cmake --build "$NG_HOST"
cmake -S . -B "$ADAPTER" -DP3_H2_HANDLER_INCLUDE="$H2_GUEST" \
  "-DP3_NGHTTP2_INCLUDE=$NGHTTP2/lib/includes;$NG_HOST/lib/includes" \
  -DP3_NGHTTP2_LIBRARY="$NG_HOST/lib/libnghttp2.a"
cmake --build "$ADAPTER" --target wasi_h2_server
"$CLIENT/bin/python" test_wasi_h2.py "$ADAPTER/wasi_h2_server" --rounds 24
```

Use the MSVC x64 developer environment on Windows. With the Ninja
generator, point `P3_NGHTTP2_LIBRARY` at `lib/nghttp2.lib` and run the
Python client against `wasi_h2_server.exe`. The overload checks distinguish
the peer SETTINGS stream limit, an on-wire `REFUSED_STREAM` at the global
limit, and TCP closure when all connection slots are reserved.

For Cortex-M33, build the regular four-task standard guest in
`PICO_GUEST` and generate the client prefaces. The newlib compatibility
header supplies byte-order conversions for the emulated bare-metal
build. Use an LTO-aware archive index with the embedded GCC toolchain.

```sh
python3 build_wasi_http_guest.py "$SUITE" "$PICO_GUEST"
"$CLIENT/bin/python" qemu/h2_inputs.py "$PICO_GUEST/h2_inputs.h"
cmake -G Ninja -S "$NGHTTP2" -B "$NG_ARM" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_C_COMPILER=arm-none-eabi-gcc \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DSIZEOF_SSIZE_T=4 \
  "-DCMAKE_C_FLAGS=-mcpu=cortex-m33 -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections -flto -include $PWD/qemu/nghttp2_newlib.h" \
  -DENABLE_LIB_ONLY=ON -DBUILD_TESTING=OFF \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON
cmake --build "$NG_ARM"
arm-none-eabi-gcc-ranlib "$NG_ARM/lib/libnghttp2.a"
cmake -G Ninja -S qemu -B "$M33_BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DWAMR_ROOT="$WAMR_ROOT" -DHANDLER_INCLUDE="$PICO_GUEST" -DP3_WASI_H2=ON \
  -DP3_NGHTTP2_INCLUDE="$NGHTTP2/lib/includes" \
  -DP3_NGHTTP2_CONFIG_INCLUDE="$NG_ARM/lib/includes" \
  -DP3_NGHTTP2_LIBRARY="$NG_ARM/lib/libnghttp2.a"
cmake --build "$M33_BUILD"
python3 qemu/run_h2.py "$M33_BUILD/stackless_m33.elf"
```

The QEMU workload passes HTTP/2 frames through the same nghttp2
transport. It verifies all response bytes, terminal trailer frames and stream
completion while holding one stream's output window closed. The
emulated binary accounts for the 64 KiB network reserve in its 520 KiB
SRAM region. This target uses scripted transport readiness. The Pico SDK
Wi-Fi firmware in the existing sample continues to use HTTP/1.1.
