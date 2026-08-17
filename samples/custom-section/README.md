---
description: "The related code/working directory of this example resides in directory {WAMR_DIR}/samples/custom-section"
---

# The "custom-section" sample project

This sample demonstrates how to:

- embed a separate binary payload into a Wasm custom section through a `.s` file
- load that Wasm module with `WAMR_BUILD_LOAD_CUSTOM_SECTION=1`
- export native APIs that resolve a custom section name to a host-side handle
- print the custom section bytes later through a second native API
- optionally compile the Wasm module to AoT while preserving the `demo` custom section

The Wasm application is built from:

- `wasm-apps/custom_section.c`
- `wasm-apps/custom_section_payload.s`
- `wasm-apps/custom_section_payload.bin`

The assembler file emits a section named `.custom_section.demo`, which becomes a Wasm custom section named `demo` in the final `.wasm` file.

## Why use a custom section for this payload

The payload in this sample is treated as read-only metadata. Putting it in a custom section lets the embedder access the bytes directly from the loaded module through `wasm_runtime_get_custom_section`, instead of copying the data into Wasm linear memory per instance.

That matters when the data is large or rarely changed:

- the bytes stay in the module image as immutable data
- the host can look them up by section name and use them in place
- the Wasm app only needs to pass a small section name and receive a small handle
- no extra application-level serialization or buffer duplication is needed for the read-only payload

This pattern is useful for embedded assets, lookup tables, model metadata, certificates, and other static blobs that the host wants to consume without treating them as mutable Wasm heap data.

## Build and test this sample

Configure, build and run the ctest tests. The Wasm app (`custom_section.wasm`)
and its AoT variant (`custom_section.aot`, built with
`wamrc --emit-custom-sections=demo` so the `demo` custom section is preserved)
are generated during `cmake --build`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run the sample

Enter the build directory and run the Wasm sample directly:

```sh
cd ./build/
./custom_section -f wasm-apps/custom_section.wasm
```

To run the AoT artifact instead:

```sh
./custom_section -f wasm-apps/custom_section.aot
```
