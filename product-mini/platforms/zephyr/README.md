# How to use WAMR with Zephyr

[Zephyr](https://www.zephyrproject.org/) is an open source real-time operating
system (RTOS) with a focus on security and broad hardware support. WAMR is
compatible with Zephyr via the [Zephyr WAMR
port](../../../core/shared/platform/zephyr), and is packaged as a [Zephyr
module](../../../zephyr) so that an application only has to enable a few
Kconfig options to get the runtime linked into its image.

## Samples

| Sample                       | What it demonstrates                                          |
| ---------------------------- | ------------------------------------------------------------- |
| [simple](./simple)           | Minimal application: run a WASM module with the built-in libc |
| [simple-file](./simple-file) | WASI file system API on top of Zephyr `fs_*`                  |
| [simple-http](./simple-http) | WASI socket API on top of Zephyr `zsock_*`                    |
| [user-mode](./user-mode)     | Running the runtime inside a Zephyr user-mode thread          |

## Setup

Using WAMR with Zephyr can be accomplished by either using the provided Docker
image, or by installing Zephyr locally. Both approaches are described below.

### Docker

The provided [Dockerfile](./Dockerfile) sets up the Zephyr SDK, `west`, a
Zephyr workspace matching the CI layout, and the wasi-sdk in `/opt/wasi-sdk`
(`$WASI_SDK_PATH`) for recompiling the samples' WASM applications. Only the ARC and x86 toolchains are
installed to keep the image reasonably small (~5 GB); add more `-t
<toolchain>` options to `setup.sh` in the Dockerfile if you need other
architectures.

The helper script [build_and_run.py](./build_and_run.py) builds
the image and runs a sample inside a container against your local checkout. It
only needs Python 3 and `docker` on the host, so it works on Linux, macOS and
Windows alike:

```shell
# build the image (only needed once)
python3 build_and_run.py --build

# build and run the simple sample on native_sim (the default simulator)
python3 build_and_run.py simple

# another simulator / another sample
python3 build_and_run.py --sim qemu_arc user-mode
```

The console only carries progress; the full `docker build`, CMake and emulator
output goes to `build/logs/`, and the tail of the relevant log is printed if a
step fails. See `--help` for details.

WAMR itself is not baked into the image. The script bind mounts the repository
at `/root/zephyrproject/modules/wasm-micro-runtime`, so the sources being built
are always the ones in your working tree and the image does not have to be
rebuilt when you change them.

To work inside the container interactively instead — the mount is required, the
module directory is empty otherwise:

```shell
docker build -t wamr-zephyr .
docker run -it --rm \
  -v "$(git rev-parse --show-toplevel)":/root/zephyrproject/modules/wasm-micro-runtime \
  wamr-zephyr
```

If you are planning to flash a device from the container, pass the device with
[`--device`](https://docs.docker.com/engine/reference/run/#runtime-privilege-and-linux-capabilities),
e.g. `--device=/dev/ttyUSB0`.

### Local Environment

Zephyr can also be set up locally. This gives you more control over which
modules and tools are installed, which can drastically reduce the required
storage compared to the Docker image. Follow the [Zephyr Getting Started
guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html),
then install the Zephyr SDK toolchains for the architectures you target.

### Workspace

WAMR is consumed as a Zephyr module, so the repository has to be visible to
`west`. The layout used by CI and by the Docker image is a
[T2 star topology](https://docs.zephyrproject.org/latest/develop/west/workspaces.html)
workspace:

```
zephyrproject/                     <- topdir
├── .west/config
├── zephyr/                        <- Zephyr source code
├── zephyr-sdk/
├── modules/
│   └── wasm-micro-runtime         <- this repository
│       ├── zephyr/module.yml      <- declares the Zephyr module
│       ├── zephyr/Kconfig         <- CONFIG_WAMR_* options
│       ├── zephyr/CMakeLists.txt  <- builds the runtime as a Zephyr library
│       └── product-mini/platforms/zephyr/<sample>/
│           ├── CMakeLists.txt     <- the application only adds its own sources
│           ├── prj.conf           <- CONFIG_WAMR_* selections for the sample
│           └── src/main.c
└── application/                   <- dummy manifest repo, holds west_lite.yml
```

Create it with the minimal manifest shipped in this tree:

```shell
export ZWS=~/zephyrproject
mkdir -p $ZWS/application $ZWS/modules
git clone https://github.com/bytecodealliance/wasm-micro-runtime.git \
  $ZWS/modules/wasm-micro-runtime
cp $ZWS/modules/wasm-micro-runtime/product-mini/platforms/zephyr/west_lite.yml \
  $ZWS/application/west_lite.yml

cd $ZWS
west init -l --mf west_lite.yml application
west update --stats
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
```

If your checkout lives outside the workspace, keep it where it is and point
the build at it instead of moving it:

```shell
west build . -b <board> -p always -- \
  -DEXTRA_ZEPHYR_MODULES=/path/to/wasm-micro-runtime
```

## Building

With the environment set up, build any of the samples with
[`west`](https://docs.zephyrproject.org/latest/develop/west/index.html) from
the sample directory:

```shell
west build . -b <board-identifier> -p always
```

The `<board-identifier>` can be found in the [Zephyr supported boards
documentation](https://docs.zephyrproject.org/latest/boards/index.html). Board
specific Kconfig fragments go into the sample's `boards/<board>.conf`.

`WAMR_BUILD_TARGET` is derived from the board architecture by
[zephyr/CMakeLists.txt](../../../zephyr/CMakeLists.txt), so it normally does
not have to be passed. Override it to select a sub-variant (e.g. `THUMBV7`
instead of the generic `THUMB`):

```shell
west build . -b <board-identifier> -p always -- -DWAMR_BUILD_TARGET=THUMBV7
```

The list of supported targets is in the main project
[README.md](../../../README.md#supported-architectures-and-platforms).

### Running under QEMU

Emulated boards are built the same way and run with `west`:

```shell
west build . -b qemu_x86 -p always
west build -t run
```

> Press `CTRL+a, x` to exit QEMU.

Boards that are regularly exercised:

| Board                           | Arch    | Notes                                          |
| ------------------------------- | ------- | ---------------------------------------------- |
| `qemu_x86`                      | X86_32  | Default smoke test target                      |
| `qemu_arc/qemu_arc_hs`          | ARC     | Needs `arc-zephyr-elf` and `arc64-zephyr-elf`  |
| `qemu_cortex_a53`               | AARCH64 | 64-bit ARM                                     |
| `qemu_riscv32` / `qemu_riscv64` | RISCV   | AOT is not supported, add `-DWAMR_BUILD_AOT=0` |
| `qemu_xtensa`                   | XTENSA  |                                                |

AOT is not available on every architecture. Where it is not, disable it with
`-DWAMR_BUILD_AOT=0` or `CONFIG_WAMR_AOT=n`.

### Running with native_sim on Linux

[`native_sim`](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)
compiles Zephyr and the application into a native Linux executable. There is
no emulation involved, so builds and runs are fast, which makes it the
quickest way to smoke test a change to the runtime.

```shell
# 32-bit host build, WAMR_BUILD_TARGET is derived as X86_32
west build . -b native_sim -p always
./build/zephyr/zephyr.exe

# 64-bit host build, WAMR_BUILD_TARGET is derived as X86_64
west build . -b native_sim/native/64 -p always
./build/zephyr/zephyr.exe
```

`west build -t run` works as well. The 32-bit variant needs the multilib host
compiler (`gcc-multilib g++-multilib` on Debian/Ubuntu); the Docker image
already has it.

Note that `native_sim` runs with the host libc and host memory sizes, so it
will not catch problems that only show up under the tight memory constraints
of a real target.

### Flashing a device

```shell
west flash
```

`west` automatically identifies the board if it is connected to the host
machine.

## Reporting results

Every layer reports what happened, so a failure is visible both in the output
and in the exit status:

- The WASM application returns `0` on success and a distinct non-zero code per
  failure (see the `EXIT_*` defines in its source), after printing
  `ERROR: <what went wrong>`. The runtime hands that code to the Zephyr
  application, as the WASI exit code for the WASI samples and as the return
  value of the entry point for the others.
- The Zephyr application checks the call result, the exception and the module
  exit code, prints `ERROR: ...` for anything unexpected and
  `PASS: <what was verified>` once everything completed, then returns:

  | Code | Meaning |
  | --- | --- |
  | 0 | the module ran to completion and reported success |
  | 1 | the host failed: runtime init, load, instantiate, missing entry point |
  | 2 | the module faulted or returned a non-zero code |

- Each sample declares in its `sample.yaml` which `PASS:` line a successful run
  must print, so [twister](https://docs.zephyrproject.org/latest/develop/test/twister.html)
  turns that into a test verdict.

## Testing with twister

The samples are twister test cases: `sample.yaml` lists the scenarios, the
platforms they may run on and the expected console output.
[build_and_run.py](./build_and_run.py) is a thin wrapper that runs twister for
one sample on one simulator, either in the Docker image or, with `--no-docker`,
in the current environment — which is exactly what CI does:

```shell
python3 build_and_run.py --sim qemu_arc user-mode
```

To run twister directly, from the workspace:

```shell
west twister -T modules/wasm-micro-runtime/product-mini/platforms/zephyr/simple \
  -p native_sim -x EXTRA_ZEPHYR_MODULES=$PWD/modules/wasm-micro-runtime \
  --disable-warnings-as-errors
```

`--disable-warnings-as-errors` is needed because twister compiles with
`-Werror`, which the runtime is not built with in any other configuration.

## Adding a new sample

1. Create a directory next to the existing samples with the usual Zephyr
   application layout: `CMakeLists.txt`, `prj.conf`, `src/`, and optionally
   `boards/<board-identifier>.conf`. Keep `CMakeLists.txt` to
   `find_package(Zephyr ...)`, `project(...)` and `target_sources(app ...)`;
   the runtime comes from the module, so nothing WAMR specific belongs there.
2. Select the runtime features with `CONFIG_WAMR_*` in `prj.conf`, as described
   in [Configuring the runtime](#configuring-the-runtime).
3. If the sample needs a Zephyr module that the workspace does not have yet —
   littlefs, mbedTLS, an HAL for a new SoC — add it to
   [west_lite.yml](./west_lite.yml). That manifest is deliberately minimal: it
   pulls Zephyr and only the modules the samples actually use, which keeps both
   the CI setup and the Docker image small. Copy the `name`, `revision` and
   `path` of the project from Zephyr's own `west.yml` so that the versions
   match:

   ```yaml
   - name: littlefs
     url: https://github.com/zephyrproject-rtos/littlefs
     revision: 408c16a909dd6cf128874a76f21c793798c9e423
     path: modules/fs/littlefs
   ```

   Existing workspaces need a `west update` afterwards, and the Docker image
   has to be rebuilt (`python3 build_and_run.py --build`).
4. Add a `sample.yaml` declaring the twister scenarios: the platforms the
   sample may run on and the `PASS:` line its console output must carry. Follow
   the exit code convention in
   [Reporting results](#reporting-results) so that a failure is visible in the
   exit status too.
5. Add a row to the [Samples](#samples) table and a `README.md` in the sample
   directory covering only what is specific to it.
6. If the sample runs on `native_sim` or QEMU, add it to the matrix in
   [.github/workflows/compilation_on_zephyr.yml](../../../.github/workflows/compilation_on_zephyr.yml)
   so that it is built and run by CI.

## Configuring the runtime

The runtime is configured through the `CONFIG_WAMR_*` Kconfig options defined
in [zephyr/Kconfig](../../../zephyr/Kconfig). Set them in the sample's
`prj.conf`:

```conf
CONFIG_WAMR=y
CONFIG_WAMR_INTERP=y
CONFIG_WAMR_AOT=y
CONFIG_WAMR_LIBC_BUILTIN=y
CONFIG_WAMR_GLOBAL_HEAP_POOL=y
CONFIG_WAMR_GLOBAL_HEAP_SIZE=131072
```

`CONFIG_WAMR=n` (the default) leaves the runtime out of the image entirely.

Each option maps onto the corresponding `WAMR_BUILD_*` CMake variable that the
regular WAMR build scripts use. Options can still be overridden on the CMake
command line (`-DWAMR_BUILD_AOT=0`), which is handy for one-off builds, but
`prj.conf` is the place to record a configuration.

### Exposing a new WAMR_BUILD_XYZ as CONFIG_WAMR_XYZ

Two edits are needed.

1. Declare the option in [zephyr/Kconfig](../../../zephyr/Kconfig), inside the
   `if WAMR` block. Mirror the runtime default and add the dependencies that
   the runtime itself requires:

   ```kconfig
   config WAMR_LIB_WASI_THREADS
	bool "wasi-threads library"
	depends on WAMR_LIBC_WASI
	help
	  Provide the wasi-threads library to WASM modules.
   ```

2. Map it in [zephyr/CMakeLists.txt](../../../zephyr/CMakeLists.txt) with the
   `wamr_option_from_kconfig` macro, which translates the undefined-when-off
   Kconfig boolean into the plain `0`/`1` the runtime expects:

   ```cmake
   wamr_option_from_kconfig (LIB_WASI_THREADS)
   ```

Non-boolean options are copied over directly, guarded by the boolean that
enables them — see how `CONFIG_WAMR_GLOBAL_HEAP_SIZE` becomes
`WAMR_BUILD_GLOBAL_HEAP_SIZE`. Keep the Kconfig name equal to the
`WAMR_BUILD_*` suffix so the mapping stays mechanical.
