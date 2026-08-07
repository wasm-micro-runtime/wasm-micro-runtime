# Simple sample

The minimal WAMR application on Zephyr: the runtime is initialized, a WASM
module embedded in the image as a C array is instantiated, and its `main` is
called. Only the built-in libc is used, so the sample runs on boards without a
file system or network stack.

The module is not checked in: [wasm-app/main.c](./wasm-app/main.c) is compiled
with the wasi-sdk during the build and the resulting `test_wasm.wasm` is turned
into `test_wasm.h` under the build directory. Building the sample therefore
requires a wasi-sdk; it is looked up in `/opt/wasi-sdk` and `/opt/wasi-sdk-*`,
set `WASISDK_ROOT` (or `WASI_SDK_DIR`) if it lives elsewhere.

See the [platform README](../README.md) for environment setup, workspace
layout, `native_sim` / QEMU usage and the `CONFIG_WAMR_*` options.

## What to look at

- [prj.conf](./prj.conf) — the `CONFIG_WAMR_*` selections: interpreter, AOT,
  built-in libc and a 128 KB global heap pool.
- [CMakeLists.txt](./CMakeLists.txt) — the application only adds its own
  sources; the runtime comes from the `wasm-micro-runtime` Zephyr module.
- [boards/](./boards) — per-board Kconfig fragments. Add
  `boards/<board-identifier>.conf` for a new board, or put the settings in
  `prj.conf` if they are board independent.
- [wasm-app/](./wasm-app) — the source of the embedded module and the
  standalone CMake project that compiles it with the wasi-sdk.

The module of this sample only prints, so the failures that can occur are on
the Zephyr side (loading, instantiating, an exception). They are reported as
`ERROR: ...`, and a complete run ends with `PASS: ...`, see
[Reporting failures](../README.md#reporting-failures).

## Build and run

```shell
west build . -b qemu_x86 -p always
west build -t run
```

[build_and_run.sh](./build_and_run.sh) wraps the same commands for a handful of
boards, including the ones that need a non-default `WAMR_BUILD_TARGET` or
`-DWAMR_BUILD_AOT=0`:

```shell
./build_and_run.sh qemu_arc
```

## Expected output

```
*** Booting Zephyr OS build v3.7.0 ***
Hello world!
buf ptr: 0x1458
buf: 1234
PASS: the wasm module ran to completion
elapsed: 10
```
