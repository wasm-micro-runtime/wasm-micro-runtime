# WAMR fuzz test framework

## Install wasm-tools

Download the release suitable for your specific platform from https://github.com/bytecodealliance/wasm-tools/releases/latest, unpack it, and add the executable wasm-tools to the `PATH`. Then, you should be able to verify that the installation was successful by using the following command:

```bash
$ wasm-tools --version
# Or learn subcommands with
$ wasm-tools help
```

## Install clang Toolchain

Refer to: https://apt.llvm.org/ and ensure that you have clang installed.

```bash
$ clang --version

$ clang++ --version
```

## Build

```bash
# Without custom mutator (libfuzzer modify the buffer randomly)
$ cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=../../../build-scripts/clang_toolchain.cmake -DLLVM_DIR=<llvm_install_dir>/lib/cmake/llvm

# TBC: if `wasm-tools mutate` is supported or not
# Or With custom mutator (wasm-tools mutate)
$ cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=../../../build-scripts/clang_toolchain.cmake -DLLVM_DIR=<llvm_install_dir>/lib/cmake/llvm -DCUSTOM_MUTATOR=1

# Then
$ cmake --build build
```

## Manually generate wasm file in build

````bash
# wasm-tools smith generate some valid wasm file
# The generated wasm file is in corpus_dir under build
# N - Number of files to be generated
$ ./smith_wasm.sh N

# running
``` bash
$ ./build/wasm-mutator/wasm_mutator_fuzz ./build/CORPUS_DIR

$ ./build/aot-compiler/aot_compiler_fuzz ./build/CORPUS_DIR
````
