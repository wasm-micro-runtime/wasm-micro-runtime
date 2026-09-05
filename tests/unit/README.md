# Guide to Creating a Test Suite for a New Feature in WAMR

This guide provides instructions for contributors on how to create a test suite for a new feature in the WAMR project. Follow these steps to ensure consistency and maintainability across the test framework.

---

## General Guidelines

- **Create a New Directory**:
  Always create a dedicated directory for a new feature under the `tests/unit/` directory.

  - Reuse existing test cases and patch them when possible to avoid redundancy.
  - Name the directory in lowercase with words separated by hyphens (e.g., `new-feature`).
  - Name the test source file in lowercase with words separated by underscore (e.g., `new_test.cc`).

- **Avoid Committing `.wasm` Files**:
  Do not commit precompiled `.wasm` files. Instead:

  - Generate `.wasm` files from `.wat` or `.c` source files during the build
    process, using the fixtures helpers described in [Generating `.wasm`
    Files](#generating-wasm-files) below.

- **Keep Using `ctest` as the framework**:
  Continue to use `ctest` for running the test cases, as it is already integrated into the existing test framework.

---

## Writing `CMakeLists.txt` for the Test Suite

A suite is a directory under `tests/unit/` with its own `CMakeLists.txt` and
test sources. It is added as a subdirectory from the top-level
`tests/unit/CMakeLists.txt`:

- Suites that can be configured on every supported build target are appended
  to the `UNIT_TEST_SUITES` list; each of them still filters itself per
  runtime mode with `wamr_unit_test_suite_run_modes` (see below).
- Suites that only make sense on specific targets are added inside the
  matching `if(WAMR_BUILD_TARGET ...)` block instead — for example the
  AOT-related suites (`aot`, `aot-stack-frame`, `custom-section`,
  `compilation`, `memory64`, `shared-heap`, `runtime-common`) live in the
  `X86_64`/`AARCH64` block.
- Suites inside the `llm-enhanced-test` submodule are registered in the
  submodule's own root `CMakeLists.txt`; the top level only adds
  `llm-enhanced-test` as a whole when `FULL_TEST=ON`.

A suite `CMakeLists.txt` follows this shape (see for example
`tests/unit/exception-handling/CMakeLists.txt`):

```cmake
# Declare the runtime modes this suite supports.  This must be the first
# statement so suites are skipped early for unsupported modes.
wamr_unit_test_suite_run_modes(new-feature MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# Enable the WAMR features exercised by this suite.  The WAMR_BUILD_* flags
# must be set *before* including ../unit_common.cmake: unit_common.cmake
# includes build-scripts/runtime_lib.cmake, which composes the runtime
# sources and feature macro definitions from these switches.
set(WAMR_BUILD_LIBC_BUILTIN 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Collect the suite's own test sources.
file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)
set(UNIT_SOURCE ${source_all})

# Assemble the executable from the test sources and the WAMR runtime library.
set(unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable(new_feature_test ${unit_test_sources})
target_link_libraries(new_feature_test gtest_main)
gtest_discover_tests(new_feature_test)
```

Guidelines:

1. **Do Not Fetch Googletest Again**:
   The top-level `tests/unit/CMakeLists.txt` already fetches Googletest and
   CMocka, calls `enable_testing()`, and preloads `unit_common.cmake`. A
   suite must not repeat that setup: no `project()`, no
   `cmake_minimum_required()`, and no redefinition of `WAMR_BUILD_PLATFORM`,
   `WAMR_BUILD_TARGET`, the build type, or the runtime-mode options.

2. **Declare the Runtime Modes First**:
   Call `wamr_unit_test_suite_run_modes(<suite> MODES <modes>)` as the first
   statement, then guard the rest with
   `if(NOT WAMR_UNIT_TEST_SUITE_ENABLED) return() endif()`. The supported
   modes are `classic-interp`, `fast-interp`, `llvm-jit`, `fast-jit`, `aot`,
   and `multi-tier-jit`. List every mode the suite supports; a suite without
   restrictions still lists all of them explicitly. Use `MODES none` only
   for suites that are intentionally excluded until they have a supported
   runtime mode.

3. **Set the `WAMR_BUILD_*` Flags Before Including `unit_common.cmake`**:
   The flags select which runtime sources are compiled into
   `${WAMR_RUNTIME_LIB_SOURCE}` and which `WASM_ENABLE_*` macros the runtime
   sees, so they must be in place before `include(../unit_common.cmake)`.
   Only touch the features the suite actually exercises.

4. **Collect the Suite Sources and Build the Executable**:
   Gather the `.cc` files with `file(GLOB_RECURSE ...)`, combine
   `${UNIT_SOURCE}` with `${WAMR_RUNTIME_LIB_SOURCE}` (add
   `${UNCOMMON_SHARED_SOURCE}` when the suite directly tests shared-utils
   code), then create one executable per test binary. Link `gtest_main`
   (add `gmock` only when needed) and register the cases with
   `gtest_discover_tests()`. Never hand-write a `main()`.

5. **Suites That Embed the AOT Compiler or Use LLVM**:
   Set `add_definitions(-DWASM_ENABLE_WAMR_COMPILER=1)` together with the
   other feature switches (before `unit_common.cmake`), include
   `${IWASM_DIR}/compilation/iwasm_compl.cmake` right after
   `unit_common.cmake`, add `${IWASM_COMPL_SOURCE}` to `unit_test_sources`,
   and link `${LLVM_AVAILABLE_LIBS}` (see `tests/unit/aot/CMakeLists.txt`).

6. **C Test Suites**:
   If the suite is written in C, link `cmocka::cmocka` and register the
   binaries with `add_test()` and `set_tests_properties()` instead of using
   Googletest (see `tests/unit/mem-alloc/CMakeLists.txt`).

---

## Generating `.wasm` Files

Generated `.wasm`/`.aot` fixtures must not be committed; compile them from
`.wat`, `.c` or `.cc` sources during the build. `unit_common.cmake` provides
helpers for the common cases (they need the tools found by the
`FindWABT.cmake` / `FindWASISDK.cmake` / `FindWAMRC.cmake` modules in
`build-scripts/`):

- **Compile `.wat` to `.wasm`** with `wamr_unit_test_compile_wat_to_wasm`
  (uses WABT's `wat2wasm`):

  ```cmake
  wamr_unit_test_compile_wat_to_wasm(
      TARGET new_feature_test
      SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/example.wat
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/example.wasm
  )
  ```

  See `tests/unit/wasm-vm/CMakeLists.txt` for a loop over several WAT
  fixtures.

- **Compile `.c` to `.wasm`** with `wamr_unit_test_compile_c_to_wasm`
  (builds a `wasm-apps/` subproject with the wasi-sdk toolchain):

  ```cmake
  wamr_unit_test_compile_c_to_wasm(
      TARGET new_feature_test
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
      DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
  )
  ```

  See `tests/unit/memory64/CMakeLists.txt` for its usage.

- **Compile `.wasm` to `.aot`** with `wamr_unit_test_compile_wasm_to_aot`
  (uses `wamrc`), usually right after the `.wasm` is generated:

  ```cmake
  wamr_unit_test_compile_wasm_to_aot(
      TARGET new_feature_test
      INPUT ${CMAKE_CURRENT_BINARY_DIR}/example.wasm
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/example.aot
      FLAGS --bounds-checks=1
  )
  ```

  See `tests/unit/shared-heap/CMakeLists.txt` for its usage.

When a helper does not fit, fall back to an explicit `ExternalProject_Add`.
Locate the wasi-sdk first and use `WASISDK_HOME` (the tool variables are
provided by `FindWASISDK.cmake`):

```cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../build-scripts")
find_package(WASISDK REQUIRED)

include(ExternalProject)
ExternalProject_Add(
    generate_wasm
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
    BUILD_ALWAYS YES
    CONFIGURE_COMMAND  ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps -B build
                          -DWASI_SDK_PREFIX=${WASISDK_HOME}
                          -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
    BUILD_COMMAND      ${CMAKE_COMMAND} --build build
    INSTALL_COMMAND    ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}/wasm-apps
)
```

See `tests/unit/custom-section/CMakeLists.txt` and
`tests/unit/running-modes/CMakeLists.txt` for this pattern.

- **Example for `wasm-apps` Directory**:
  Place your source files in a `wasm-apps/` subdirectory within your test
  suite directory, with its own `CMakeLists.txt` (it is configured as a
  standalone project by the `ExternalProject` above, so it may declare
  `cmake_minimum_required()`/`project()`). Name each executable after its
  target `.wasm` file, link it with the wasi-sdk options, and install it:

  ```cmake
  cmake_minimum_required(VERSION 3.14)
  project(wasm-apps)

  add_executable(example.wasm example.c)
  target_compile_options(example.wasm PUBLIC -nostdlib)
  target_link_options(example.wasm PRIVATE
    -nostdlib
    LINKER:--allow-undefined
    LINKER:--export-all
    LINKER:--no-entry
  )

  # install .wasm
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/example.wasm DESTINATION .)
  ```

  See `tests/unit/running-modes/wasm-apps/CMakeLists.txt` and
  `tests/unit/custom-section/wasm-apps/CMakeLists.txt`.

- **Copy `.wasm`/`.aot` Files with Shared Helpers**:
  Use the helpers from `unit_common.cmake` instead of open-coded copy commands.

  ```cmake
  wamr_unit_test_copy_wasm_files(new_feature_test
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
      DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/wasm-apps
      COMMENT "Copying WASM test files"
  )
  ```

  If several test executables should share one copy step, create a copy target
  and make each executable depend on it:

  ```cmake
  wamr_unit_test_add_wasm_copy_target(copy_new_feature_wasm_apps
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
      DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/wasm-apps
      COMMENT "Copying WASM test files"
  )
  add_dependencies(new_feature_test copy_new_feature_wasm_apps)
  ```

---

## Initializing Submodules

Test suite `llm-enhanced-test` is maintained in separate repository and included as git submodule. You need to initialize it before building.

```bash
git submodule update --init --recursive
```

Alternatively, if you haven't cloned the repository yet, use `--recursive` when cloning:

```bash
git clone --recursive https://github.com/bytecodealliance/wasm-micro-runtime.git
```

---

## Compiling and Running Test Cases

To compile and run the test cases, follow these steps:

1. **Generate Build Files**:

   ```bash
   cmake -S . -B build
   ```

   By default, unit tests use `classic-interp`; no runtime-mode option is
   required. To select another mode, set only the option for that mode to `1`.
   Do not set the other mode options to `0`; the unit-test CMake configuration
   supplies their defaults:

   - Classic interpreter: no option (equivalent to `-DWAMR_BUILD_INTERP=1`)
   - Fast interpreter: `-DWAMR_BUILD_FAST_INTERP=1`
   - LLVM JIT: `-DWAMR_BUILD_JIT=1`
   - Fast JIT: `-DWAMR_BUILD_FAST_JIT=1`
   - AOT: `-DWAMR_BUILD_AOT=1`
   - Multi-tier JIT: `-DWAMR_BUILD_JIT=1 -DWAMR_BUILD_FAST_JIT=1`

   Exactly one runtime mode must be selected for each build. AOT cannot be
   combined with fast interpreter or JIT options. `WAMR_BUILD_JIT=1` and
   `WAMR_BUILD_FAST_JIT=1` together are the one supported multi-tier JIT mode.
   Invalid combinations stop CMake configuration with an error. Using
   separate build directories for different modes is recommended.
   `WAMR_BUILD_INTERP=1` may remain enabled as a runtime build dependency and
   does not count as selecting an additional runtime mode.

   For example, to configure the LLVM JIT mode, set only `WAMR_BUILD_JIT`:

   ```bash
   cmake -S . -B build-jit \
       -DWAMR_BUILD_JIT=1
   ```

   CI runs the unit tests as a runtime-mode matrix.

   By default, all unit tests except `llm-enhanced-test` are built (`-DFULL_TEST=OFF`).  
   To also include `llm-enhanced-test`, configure with:

   ```bash
   cmake -S . -B build -DFULL_TEST=ON
   ```

2. **Build the Test Suite**:

   ```bash
   cmake --build build
   ```

3. **Run the Tests**:

   ```bash
   ctest --test-dir build --output-on-failure
   ```

   This will compile and execute all test cases in the test suite, displaying detailed output for any failures.

   The `unsupported-features` tests need to be built and run separately from
   the main unit test project.

4. **List all Tests**:
   To see all available test cases, use:

   ```bash
   ctest --test-dir build -N
   ```

5. **Run a Specific Test**:
   To run a specific test case, use:
   ```bash
   ctest --test-dir build -R <test_name> --output-on-failure
   ```

---

## Collecting Code Coverage Data

To collect code coverage data using `lcov`, follow these steps:

1. **Build with Coverage Flags**:
   Ensure the test suite is built with coverage flags enabled:

   ```bash
   cmake -S . -B build -DCOLLECT_CODE_COVERAGE=1
   cmake --build build
   ```

2. **Run the Tests**:
   Execute the test cases as described above.

3. **Generate Coverage Report**:
   Use `lcov` to collect and generate the coverage report:

   ```bash
   lcov --capture --directory build --output-file coverage.all.info
   lcov --extract coverage.all.info "*/core/iwasm/*" "*/core/shared/*" --output-file coverage.info
   genhtml coverage.info --output-directory coverage-report
   ```

4. **View the Report**:
   Open the `index.html` file in the `coverage-report` directory to view the coverage results in your browser.

5. **Summary of Coverage**:
   To get a summary of the coverage data, use:

   ```bash
   lcov --summary coverage.info
   ```

---

## Example Directory Structure

Here’s an example of how your test suite directory might look:

```
new-feature/
├── CMakeLists.txt
├── new_feature_test.cc
├── wasm-apps/
|   ├── CMakeLists.txt
│   ├── example.c
│   └── example.wat
```

---

## Additional Notes

- **Testing Framework**: Use Googletest for writing unit tests. Refer to existing test cases in the `tests/unit/` directory for examples.
- **Documentation**: Add comments in your test code to explain the purpose of each test case.
- **Edge Cases**: Ensure your test suite covers edge cases and potential failure scenarios.
- **Reuse Utilities**: Leverage existing utilities in `common/` (e.g., `mock_allocator.h`, `test_helper.h`) to simplify your test code.

---

By following these guidelines, you can create a well-structured and maintainable test suite that integrates seamlessly with the WAMR testing framework.
