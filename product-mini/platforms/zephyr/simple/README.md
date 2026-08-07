# Simple sample

The minimal WAMR application on Zephyr: the runtime is initialized, a WASM
module embedded in the image as a C array ([src/test_wasm.h](./src/test_wasm.h))
is instantiated, and its `main` is called. Only the built-in libc is used, so
the sample runs on boards without a file system or network stack.

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
- [src/wasm-app-riscv64/build.sh](./src/wasm-app-riscv64/build.sh) — how the
  embedded module is compiled and turned into a header.

The module allocates a buffer, formats a string into it and checks the result,
returning 0 on success and 1 (allocation) or 2 (content mismatch) otherwise.
The Zephyr application turns that, an exception or its own errors into an exit
code and a `PASS:` / `ERROR:` line, see
[Reporting results](../README.md#reporting-results).

## Test status

The scenarios are declared in [sample.yaml](./sample.yaml); twister decides the
verdict from the console output. Last run with
[build_and_run.py](../build_and_run.py) on 2026-08-06:

| Scenario | Simulator | Result |
| --- | --- | --- |
| `sample.wamr.simple` | `native_sim` | passed |
| `sample.wamr.simple` | `qemu_arc/qemu_arc_hs` | passed |

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
