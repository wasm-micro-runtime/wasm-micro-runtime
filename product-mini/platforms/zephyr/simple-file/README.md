# File sample

This sample demonstrates the use of the WASI file system API from a WASM
module. [wasm-app/file.c](./wasm-app/file.c) creates a directory, writes a
string to a file, re-opens the file and reads it back, checks the size and
finally removes it. The Zephyr side mounts a littlefs volume on `/lfs` and
pre-opens it for the module.

> 🛠️ **Work in progress:** only a small part of the WASI file system API is
> exercised. The Zephyr calls behind it are `fs_mkdir`, `fs_open`, `fs_write`,
> `fs_seek`, `fs_read`, `fs_close` and `fs_unlink`.

See the [platform README](../README.md) for environment setup, workspace layout
and flashing. The sample needs a flash partition with a littlefs mount point;
`native_sim` provides one through its flash simulator, so it can be built and
run there without any hardware.

## Test status

The scenarios are declared in [sample.yaml](./sample.yaml); twister decides the
verdict from the console output. Last run with
[build_and_run.py](../build_and_run.py) on 2026-08-06:

| Scenario | Simulator | Result |
| --- | --- | --- |
| `sample.wamr.simple_file` | `native_sim` | passed |

`qemu_arc/qemu_arc_hs` is not in `platform_allow`: the board has no flash
partition for littlefs, so the build stops with
`'DT_N_NODELABEL_storage_partition_PARTITION_ID' undeclared`.

## Run Command
* **Zephyr Build**

    The runtime comes from the `wasm-micro-runtime` Zephyr module and is
    configured with the `CONFIG_WAMR_*` options in [prj.conf](./prj.conf).

    It builds and runs on `native_sim`, which provides a flash simulator that
    littlefs can be mounted on:

    ```bash
    python3 ../build_and_run.py simple-file
    ```

    For a real board, replace the board identifier and add a
    `boards/<board-identifier>.conf` with the board specific settings:

    ```bash
    west build . -b nucleo_h563zi -p always
    ```

* **WebAssembly Module**

    The module is not checked in: [wasm-app/file.c](./wasm-app/file.c) is
    compiled with the wasi-sdk during the build and the resulting `file.wasm`
    is turned into the `file.h` that the Zephyr application embeds. Editing
    `wasm-app/file.c` is enough, the next build picks it up.

    The wasi-sdk is looked up in `/opt/wasi-sdk` and `/opt/wasi-sdk-*` (the
    Docker image ships it there); set `WASISDK_ROOT` or `WASI_SDK_DIR` if it
    lives elsewhere.

    ❗ The wasi-libc of recent wasi-sdk releases uses the reference types
    proposal, so the runtime needs `CONFIG_WAMR_REF_TYPES=y`; otherwise loading
    fails with *"The module uses reference types feature which is disabled in
    the runtime"*.

## Output

Running on `native_sim` (`python3 ../build_and_run.py simple-file`):

```
*** Booting Zephyr OS build v3.7.0 ***
Area 4 at 0xfc000 on flash-controller@0 for 16384 bytes
<inf> littlefs: LittleFS version 2.8, disk version 2.1
<err> littlefs: Corrupted dir pair at {0x0, 0x1}
<wrn> littlefs: can't mount (LFS -84); formatting
<inf> littlefs: /lfs mounted
/lfs mount: 0
<inf> main: global heap size: 131072
<inf> main: Wasm file size: 237032
<inf> main: main found
Hello WebAssembly Module !
directory /lfs/folder ready
wrote 13 bytes
read 13 bytes: Hello, World!
file size on disk: 13 bytes
file removed
<inf> main: main executed
<inf> main: wasi exit code: 0
```

On failure the module prints an `ERROR: ...` line and exits with a code that
tells the host what went wrong (1 mkdir, 2 write, 3 read, 4 content mismatch,
5 stat, 6 remove). The Zephyr application checks the call result, the exception
and that exit code, then logs either

```
<inf> main: PASS: the file was written, read back and removed
```

or, for instance

```
ERROR: content mismatch, read "..." (13 bytes), expected "Hello, World!" (13 bytes)
<inf> main: wasi exit code: 4
<err> main: FAIL: the file operations reported error 4
```

and returns non-zero from `main`.

> The littlefs error and warning on the first lines are expected: the flash
> simulator of `native_sim` starts out blank, so there is no file system yet and
> littlefs formats the area. A real board shows the same on its first boot.
