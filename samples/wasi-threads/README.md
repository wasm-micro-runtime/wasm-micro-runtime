# "WASI threads" sample introduction

To run the sample, `wasi-sdk` >= 20 is required.

## Build and run the samples

To run the sample, `wasi-sdk` >= 20 is required.

```shell
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build
$ ctest --test-dir build --output-on-failure
$ ./build/iwasm wasm-apps/no_pthread.wasm
```

## Run samples in AOT mode
```shell
$ wamrc --enable-multi-thread \
    -o build/wasm-apps/no_pthread.aot build/wasm-apps/no_pthread.wasm
$ ./build/iwasm build/wasm-apps/no_pthread.aot
```
