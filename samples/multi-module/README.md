---
description: "The related code/working directory of this example resides in directory {WAMR_DIR}/samples/multi-module"
---
# WAMR multi-module sample

**WAMR supports *multi-module* in both *interpreter* mode and *aot* mode.**

Multi-modules determine the running mode based on the type of the main module
(`.wasm` → interpreter, `.aot` → AOT).

```shell
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build
$ # Builds the multi_module runtime and the wasm modules (mA..mE.wasm) in
$ # ./build. If wamrc is available, the AOT variants are also generated.
$ ctest --test-dir build --output-on-failure
$ ./build/multi_module mC.wasm
$ ./build/multi_module mC.aot
```
