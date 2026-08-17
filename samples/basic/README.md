---
description: "The related code/working directory of this example resides in directory {WAMR_DIR}/samples/basic"
---

The "basic" sample project
==============

This sample demonstrates a few basic usages of embedding WAMR:
- initialize runtime
- load wasm app and instantiate the module
- call wasm function and pass arguments
- export native functions to the WASM apps
- wasm function calls native function and pass arguments
- deinitialize runtime

Build and test this sample
==============
Configure, build and run the ctest tests. The wasm application
(`wasm-apps/testapp.wasm`) is built during `cmake --build`.

```
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
$ cmake --build build
$ ctest --test-dir build --output-on-failure
```

Run the sample
==========================
Run the `basic` executable from the build directory.
```
$ cd ./build/
$
$ ./basic -f wasm-apps/testapp.wasm
calling into WASM function: generate_float
Native finished calling wasm function generate_float(), returned a float value: 102009.921875f
calling into WASM function: float_to_string
calling into native function: intToStr
calling into native function: get_pow
calling into native function: intToStr
Native finished calling wasm function: float_to_string, returned a formatted string: 102009.921
```
The `free_buffer_early` executable exercises freeing the wasm binary buffer
early; it is also covered by the ctest tests (`basic_free_buffer_early`).




