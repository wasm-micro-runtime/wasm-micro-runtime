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
Zephyr workspace matching the CI layout (with WAMR marked inactive, see
[Keeping west update off your checkout](#keeping-west-update-off-your-checkout)),
and the wasi-sdk in `/opt/wasi-sdk`
(`$WASI_SDK_PATH`) for recompiling the samples' WASM applications. Only the ARC and x86 toolchains are
installed to keep the image reasonably small (~5 GB); add more `-t
<toolchain>` options to `setup.sh` in the Dockerfile if you need other
architectures.

The helper script [build_and_run.py](./build_and_run.py) builds
the image and runs the samples inside a container against your local checkout.
It only needs Python 3 and `docker` on the host, so it works on Linux, macOS
and Windows alike:

```shell
# build the image (only needed once)
python3 build_and_run.py --build

# every sample, on every simulator it declares
python3 build_and_run.py

# narrow it down while working on one sample
python3 build_and_run.py simple
python3 build_and_run.py --sim qemu_arc user-mode
```

The console only carries progress; the full `docker build`, CMake and emulator
output goes to `build/logs/`, and the tail of the relevant log is printed if a
step fails. See `--help` for details.

WAMR itself is not baked into the image. The script bind mounts the repository
at `/root/zephyrproject/modules/wasm-micro-runtime` and mirrors this directory
into the workspace's `application/` on every run, so both the runtime and the
applications being built are always the ones in your working tree and the image
does not have to be rebuilt when you change them.

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
`west`. This directory is a standalone Zephyr application repository: it holds
the applications and, in [`west.yml`](./west.yml), everything they need, WAMR
included. So it is the manifest repository of a
[T2 star topology](https://docs.zephyrproject.org/latest/develop/west/workspaces.html)
workspace, and the runtime is just another module `west` clones:

```
zephyrproject/                     <- topdir
├── .west/config
├── zephyr/                        <- fetched by west update
├── zephyr-sdk/
├── modules/
│   ├── fs/littlefs                <- fetched by west update
│   └── wasm-micro-runtime         <- fetched by west update, from west.yml
│       ├── zephyr/module.yml      <- declares the Zephyr module
│       ├── zephyr/Kconfig         <- CONFIG_WAMR_* options
│       └── zephyr/CMakeLists.txt  <- builds the runtime as a Zephyr library
└── application/                   <- a copy of THIS directory
    ├── west.yml                   <- the manifest above was read from here
    └── <sample>/
        ├── CMakeLists.txt         <- the application only adds its own sources
        ├── prj.conf               <- CONFIG_WAMR_* selections for the sample
        └── src/main.c
```

Copy this directory out anywhere and build from it:

```shell
export ZWS=~/zephyrproject
mkdir -p $ZWS
cp -r /path/to/wasm-micro-runtime/product-mini/platforms/zephyr $ZWS/application

cd $ZWS
west init -l --mf west.yml application
west update --stats
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
west build application/<sample> -b <board> -p always
```

The applications find the runtime through
`${ZEPHYR_WASM_MICRO_RUNTIME_MODULE_DIR}`, never through a relative path, so
nothing here depends on sitting inside the WAMR repository.

Note that `application/` has to be a copy, not a symlink to this directory:
`west init -l` resolves the manifest directory to its real path and would put
the topdir inside the WAMR checkout.

### Keeping west update off your checkout

Working on the runtime itself is the same workspace with one change. `west.yml`
lists WAMR, so `west update` would clone it from GitHub straight over the
checkout under test — the bind mount in the container, the pull request in CI.
Mark the project inactive and `west update` skips it:

```shell
west config --global manifest.project-filter -- -wasm-micro-runtime
```

The runtime then comes from the working tree instead, attached to the build
with `EXTRA_ZEPHYR_MODULES`:

```shell
west build application/<sample> -b <board> -p always -- \
  -DEXTRA_ZEPHYR_MODULES=/path/to/wasm-micro-runtime
```

That is all the Dockerfile, CI and [build_and_run.py](./build_and_run.py) do
differently; the layout, the manifest and the build commands are the ones
above. Set the config before `west init`, and prefer `--global`: the CI action
that creates the workspace runs `west init` and `west update` as one step,
leaving no moment in between to configure the workspace itself.

A `file:///` URL in the manifest is not a substitute for any of this: `west
update` clones and checks out a fixed revision, so uncommitted changes — the
whole point of a local build — would not be there.

The one thing this arrangement never exercises is the WAMR entry in `west.yml`
itself, since every automated build skips it. After changing that entry, check
it by hand in a throwaway workspace, without the config:

```shell
workspace=$(mktemp -d)
cp -r . $workspace/application
cd $workspace && west init -l --mf west.yml application && west update --stats
west zephyr-export && west twister -T application -p native_sim \
  --disable-warnings-as-errors --jobs 1
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
[build_and_run.py](./build_and_run.py) is a thin wrapper around twister, either
in the Docker image or, with `--no-docker`, in the current environment — which
is exactly what CI does. Given no sample and no `--sim`, it hands twister every
sample and every simulator and lets the `sample.yaml` files decide what runs
where, so nothing keeps a second copy of that mapping:

```shell
python3 build_and_run.py                        # what CI runs
python3 build_and_run.py --sim qemu_arc user-mode
```

To run twister directly, from the workspace:

```shell
west twister -T application/simple -p native_sim \
  -x EXTRA_ZEPHYR_MODULES=$PWD/modules/wasm-micro-runtime \
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
   If the application does need a path into the WAMR tree — a CMake module
   under `build-scripts/`, a source file under `core/` — take it from
   `${ZEPHYR_WASM_MICRO_RUNTIME_MODULE_DIR}`, never from a relative path that
   leaves this directory, which would only work inside the WAMR repository.
2. Select the runtime features with `CONFIG_WAMR_*` in `prj.conf`, as described
   in [Configuring the runtime](#configuring-the-runtime).
3. If the sample needs a Zephyr module that the workspace does not have yet —
   littlefs, mbedTLS, an HAL for a new SoC — add it to
   [west.yml](./west.yml). It is deliberately minimal: Zephyr and only the
   modules the samples actually use, which keeps both the CI setup and the
   Docker image small. Copy the `name`, `revision` and `path` of the
   project from Zephyr's own `west.yml` so that the versions match:

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

CI needs no change: it runs twister over this whole directory, so a sample is
picked up as soon as it has a `sample.yaml`, on the platforms that file allows.

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
