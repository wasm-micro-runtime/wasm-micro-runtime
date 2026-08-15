# "import function callback" sample introduction

This sample demonstrates how to use import function callbacks to handle WebAssembly modules that import external functions. The sample shows how to register callback functions for imported functions and execute them when the WASM module loads these imported functions.

The sample includes a WASM module that imports external functions and a host application that provides callback `import_func_type_callback` for these imported functions.

## Build and run the sample

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/import-func-callback
```
