---
description: "The related code/working directory of this example resides in directory {WAMR_DIR}/samples/wasm-c-api"
---
WAMR supports *wasm-c-api* in both *interpreter* mode and *aot* mode.

Before starting, we need to download and install [WABT](https://github.com/WebAssembly/wabt/releases/latest).

``` shell
$ cd /opt
$ wget https://github.com/WebAssembly/wabt/releases/download/1.0.31/wabt-1.0.31-ubuntu.tar.gz
$ tar -xzf wabt-1.0.31-ubuntu.tar.gz
$ mv wabt-1.0.31 wabt
```

By default, all samples are compiled and run in "interpreter" mode. The
`*.wasm` files are generated from the `.wat` sources in `src/` during
`cmake --build`.

``` shell
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build
$ ctest --test-dir build --output-on-failure
$ ./build/hello
$ ./build/global
$ ./build/callback
```

They can be compiled and run in *aot* mode when some compiling flags are given
(`wamrc` is required; see `samples/README.md`):

``` shell
$ cmake -S . -B build -DWAMR_BUILD_INTERP=0 -DWAMR_BUILD_AOT=1 -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build
$ ctest --test-dir build --output-on-failure
$ ./build/hello
$ ./build/global
$ ./build/callback
```
