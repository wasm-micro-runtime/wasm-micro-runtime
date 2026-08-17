# Samples

Standalone examples of embedding WAMR (the runtime API, iwasm, wasi, sockets,
threads, AOT, ...). Each sample is an independent CMake project; `workload` is
a separate, more complex collection of workloads.

## Sample list

| Sample | What it demonstrates |
|---|---|
| [basic](./basic) | Embedding the runtime API: load/instantiate a module, call wasm functions, export native functions to wasm |
| [custom-section](./custom-section) | Embedding a binary payload in a wasm custom section and resolving it from native code |
| [file](./file/README.md) | WASI file I/O APIs (also demonstrates SGX IPFS) |
| [import-func-callback](./import-func-callback) | Iterating a module's imported functions via callback |
| [inst-context](./inst-context) | Per-instance context (TLS-like) accessible from native functions |
| [inst-context-threads](./inst-context-threads) | Per-instance context with `wasm32-wasi-threads` (pthreads in wasm) |
| [mem-allocator](./mem-allocator) | Custom allocator support (`Alloc_With_Allocator`) |
| [multi-module](./multi-module) | Multi-module (load-time dynamic linking) with inter-module calls |
| [multi-thread](./multi-thread) | Running a wasm app that spawns pthreads and uses mutex/cond |
| [native-lib](./native-lib) | Registering native shared libraries to iwasm |
| [native-stack-overflow](./native-stack-overflow) | Native stack-overflow detection across interpreter and AOT |
| [printversion](./printversion) | Querying the runtime version |
| [ref-types](./ref-types) | Reference types: externref arguments and returns |
| [shared-heap](./shared-heap) | Shared heap between two wasm instances, incl. a shared-heap chain |
| [shared-module](./shared-module) | Module sharing between instances (data/elem segments) |
| [socket-api](./socket-api/README.md) | WASI sockets: TCP/UDP server+client, options, timeouts |
| [spawn-thread](./spawn-thread) | Running wasm functions concurrently in host-created threads |
| [terminate](./terminate) | `wasm_runtime_terminate` semantics with multiple instances |
| [wasi-threads](./wasi-threads/README.md) | wasi-threads (`thread-spawn`) based multithreading |
| [wasm-c-api](./wasm-c-api/README.md) | The [wasm-c-api](https://github.com/WebAssembly/wasm-c-api) proposal examples |
| [wasm-c-api-imports](./wasm-c-api-imports) | Interop with wasm-c-api + socket imports, wasm → AOT |
| [bh-atomic](./bh-atomic) | `bh_atomic` helpers (used by the runtime) |
| [debug-tools](./debug-tools/README.md) | Symbolicating a WAMR stack trace with `addr2line.py` |
| [debug-tools-optimized](./debug-tools-optimized) | Same, for `-Oz -flto` optimized apps (binaryen/wamrc) |
| [linux-perf](./linux-perf) | Linux perf profiling of wasm execution (JIT) |
| [sgx-ra](./sgx-ra/README.md) | Remote attestation on SGX with librats |
| [workload](./workload/README.md) | Complex workloads (tensorflow-lite, XNNPACK, wasm-av1, ...) |

## Directory layout (standard)

A sample follows this layout (see [README.md](../README.md) conventions):

```
samples/<name>/
├── CMakeLists.txt          # host build + ctest registration
├── README.md               # what the sample demonstrates and how to run it
├── src/                    # host C sources (if any)
├── wasm-apps/              # wasm application sources
│   ├── CMakeLists.txt      # standalone CMake project that builds the .wasm/.aot
│   └── *.c / *.wat / ...   # the wasm application source
└── ...
```

- The wasm application lives in `wasm-apps/` and is built by a **standalone
  CMake project** (`wasm-apps/CMakeLists.txt`) driven from the parent
  `CMakeLists.txt` via `samples_build_wasm_app()` (see
  `samples/samples_common.cmake`). This keeps wasm compile flags isolated from
  the host build.
- The produced `.wasm`/`.aot` files are installed into the sample's build
  directory under `wasm-apps/`, so tests reference them with a path relative
  to the build directory (e.g. `wasm-apps/testapp.wasm`).
- A few samples deviate for good reasons: `socket-api` keeps its wasm and
  host socket sources together in `wasm-src/`; `wasm-c-api-imports` uses
  `wasm/` for its wasm side; `ref-types` / `wasm-c-api` generate their wasm
  from `.wat` files next to the host sources; `debug-tools*` and `linux-perf`
  follow the same `wasm-apps/` convention.

## Building and testing

Each sample (except `workload`) is a standalone CMake project. Build it and run
its tests with ctest:

```bash
cmake -S samples/<name> -B samples/<name>/build -DCMAKE_BUILD_TYPE=Debug
cmake --build samples/<name>/build
ctest --test-dir samples/<name>/build --output-on-failure
```

The `.wasm`/`.aot` applications are built during `cmake --build`; no shell
scripts are needed.

## Required tools

A sample's `configure` fails if a required tool is missing (no silent
fallback):

- **wasi-sdk** (`WASISDK`): needed by most samples to build wasm applications.
- **wabt** (`WABT`): needed by samples whose wasm application is written in
  `.wat` (`terminate`, `shared-module`, `ref-types`, `wasm-c-api`).
- **wamrc** (`WAMRC`): needed by all AOT-enabled samples. Build it first:

  ```bash
  cmake -S wamr-compiler -B wamr-compiler/build -DWAMR_BUILD_WITH_CUSTOM_LLVM=1
  cmake --build wamr-compiler/build --target wamrc
  ```

  `FindWAMRC.cmake` locates the binary at `wamr-compiler/build/wamrc`.

## Samples not built/run by default

The following samples register ctest tests but are **not** part of the default
build/test set, because their required toolchain is unavailable in the default
CI/devcontainer environment:

| Sample | Reason |
|---|---|
| `sgx-ra` | Requires Intel SGX SDK + SGX-capable hardware + librats |
| `linux-perf` | Requires `WAMR_BUILD_JIT=1` with an in-tree LLVM build, plus `perf` for profiling |
| `debug-tools` | Requires wasi-sdk >= 29 (`llvm-symbolizer` used by `addr2line.py`); the default image ships wasi-sdk 25 |
| `debug-tools-optimized` | Requires binaryen + wasi-sdk >= 29 + wamrc |
