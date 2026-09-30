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

  `wamr_unit_test_copy_wasm_files()` is only for suites whose fixture exists
  solely as a `.wasm` file and cannot be produced from a `.wat` or `.c`
  source; see [Copy an existing `.wasm`](#copy-an-existing-wasm).

- **Keep the Feature Switch Set Minimal**:
  Enable only the `WAMR_BUILD_*` switches the suite actually exercises, and
  verify every one of them with the toggle procedure in
  [Keeping the Feature Switch Set Minimal](#keeping-the-feature-switch-set-minimal).
  A switch that only part of the cases needs belongs in a separate suite.

- **Keep Using `ctest` as the framework**:
  Continue to use `ctest` for running the test cases, as it is already integrated into the existing test framework.

---

## Writing `CMakeLists.txt` for the Test Suite

A suite is a directory under `tests/unit/` with its own `CMakeLists.txt` and
test sources. It is added as a subdirectory from the top-level
`tests/unit/CMakeLists.txt`:

- Suites that can be configured on every supported build target are added with
  `add_subdirectory()` near the top of the file; each of them still filters
  itself per runtime mode with `wamr_unit_test_suite_run_modes` (see below).
- A suite that comes in several runtime variants is grouped one level deeper
  and added by path (`mem-alloc/base`, `mem-alloc/gc`, `memory64/base`,
  `memory64/atomic`); a nested suite includes `unit_common.cmake` as
  `../../unit_common.cmake`.
- Suites that only make sense on specific targets are added inside the
  matching `if(WAMR_BUILD_TARGET ...)` block instead — for example the
  AOT-related suites (`aot`, `aot-stack-frame`, `custom-section`,
  `compilation`, `memory64/base`, `memory64/atomic`, `shared-heap`,
  `runtime-common`) live in the `X86_64`/`AARCH64` block.
- Suites inside the `llm-enhanced-test` submodule are registered in the
  submodule's own root `CMakeLists.txt`; the top level only adds
  `llm-enhanced-test` as a whole when `FULL_TEST=ON`. A suite nested one level
  deeper includes `unit_common.cmake` as `../../unit_common.cmake`.

Use the no-space CMake style (`set(VAR 1)`, `include(path)`,
`add_executable(target ...)`) throughout, and keep the statements in the order
of the skeleton below.

### Suite Skeleton

```cmake
# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Declare the runtime modes this suite supports.  This must be the first
# statement so suites are skipped early for unsupported modes.
wamr_unit_test_suite_run_modes(new-feature MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# The WAMR_BUILD_* switches this suite needs, and nothing else.  They must be
# set *before* including ../unit_common.cmake: unit_common.cmake includes
# build-scripts/runtime_lib.cmake, which composes the runtime sources and
# feature macro definitions from these switches.
set(WAMR_BUILD_LIBC_BUILTIN 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable(new_feature_test ${unit_test_sources})
target_link_libraries(new_feature_test gtest_main)
gtest_discover_tests(new_feature_test)
```

The rules behind that shape:

### 1. Do Not Repeat the Framework Setup

The top-level `tests/unit/CMakeLists.txt` already fetches Googletest and
CMocka, calls `enable_testing()`, includes `GoogleTest`, and preloads
`unit_common.cmake`. A suite must not repeat any of that: no `project()`, no
`cmake_minimum_required()`, no `include(GoogleTest)`, no `enable_testing()`,
no `FetchContent`, no redefinition of `WAMR_BUILD_PLATFORM`,
`WAMR_BUILD_TARGET`, the build type, or the runtime-mode options.

### 2. Declare the Run Modes First

Call `wamr_unit_test_suite_run_modes(<suite> MODES <modes>)` as the first
statement, then guard the rest with
`if(NOT WAMR_UNIT_TEST_SUITE_ENABLED) return() endif()`. The supported modes
are `classic-interp`, `fast-interp`, `llvm-jit`, `fast-jit`, `aot`, and
`multi-tier-jit`. List every mode the suite supports; a suite without
restrictions still lists all of them explicitly. Use `MODES none` only for
suites that are intentionally excluded until they have a supported runtime
mode.

### 3. Set the `WAMR_BUILD_*` Flags

The flags select which runtime sources are compiled into
`${WAMR_RUNTIME_LIB_SOURCE}` and which `WASM_ENABLE_*` macros the runtime sees,
so they must be in place before `include(../unit_common.cmake)`.

- Every runtime feature switch is written as `set(WAMR_BUILD_xx 1)`. No other
  form is accepted: not `target_compile_definitions(... WASM_ENABLE_xx=1)`,
  not `add_definitions(-DWASM_ENABLE_xx=1)`, not
  `target_compile_options(... -DWAMR_BUILD_xx=1)`.
- The one exception is the in-process AOT compiler:
  `add_definitions(-DWASM_ENABLE_WAMR_COMPILER=1)` (see
  [Suites That Embed the AOT Compiler](#6-suites-that-embed-the-aot-compiler-or-use-llvm)).
- Do **not** define the running mode here. `WAMR_BUILD_INTERP`,
  `WAMR_BUILD_FAST_INTERP`, `WAMR_BUILD_JIT`, `WAMR_BUILD_FAST_JIT` and
  `WAMR_BUILD_AOT` come from the configure command line only; a suite that
  needs per-mode behaviour uses them to select a target (see
  [Per-Run-Mode Test Targets](#4-per-run-mode-test-targets)), never to set
  them.
- Switches that are not boolean keep their value:
  `set(WAMR_BUILD_SANITIZER "asan")`.
- Runtime variables outside the `WAMR_BUILD_*` family
  (`WAMR_DISABLE_HW_BOUND_CHECK`, `WAMR_DISABLE_WRITE_GS_BASE`,
  `WAMR_DISABLE_STACK_HW_BOUND_CHECK`, ...) are set the same way, in the same
  block, before the include.
- Do not restate what `config_common.cmake` derives: for every
  `WAMR_BUILD_xx` it already adds `-DWASM_ENABLE_xx=...` to the whole
  directory, so a suite must never copy `WAMR_BUILD_xx` into
  `WASM_ENABLE_xx` by hand.
- A `set(WAMR_BUILD_xx 0)` line is only meaningful when `build-scripts/`
  turns that switch on by default; otherwise delete it, it does nothing.

### 4. Per-Run-Mode Test Targets

A suite may build different test targets for different runtime modes (for
example a classic-interpreter target and a fast-interpreter target). In that
case:

- `wamr_unit_test_suite_run_modes()` lists the **union** of the modes the
  variants need.
- The running mode still comes from the outside; the suite uses the
  `WAMR_BUILD_*` mode variables to decide which target to create, and only the
  target for the configured mode is registered:

  ```cmake
  wamr_unit_test_suite_run_modes(interpreter-invoke
    MODES classic-interp fast-interp)
  if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
    return()
  endif()

  include(../../unit_common.cmake)

  include_directories(${CMAKE_CURRENT_SOURCE_DIR})

  file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

  set(unit_test_sources
    ${source_all}
    ${WAMR_RUNTIME_LIB_SOURCE}
  )

  if(WAMR_BUILD_FAST_INTERP EQUAL 1)
    add_executable(interpreter-invoke-fast ${unit_test_sources})
    target_link_libraries(interpreter-invoke-fast gtest_main)
    gtest_discover_tests(interpreter-invoke-fast)
  else()
    add_executable(interpreter-invoke-classic ${unit_test_sources})
    target_link_libraries(interpreter-invoke-classic gtest_main)
    gtest_discover_tests(interpreter-invoke-classic)
  endif()
  ```

- `target_compile_definitions(<test target> PRIVATE ...)` is allowed for
  **test-source** branch selection only (a case may compile differently
  depending on a `WASM_ENABLE_*` or a `BUILD_TARGET_*` macro). Such
  definitions also follow the minimal rule, and anything `config_common.cmake`
  already provides must not be repeated there.

### 5. Test Sources and the Executable

Gather the `.cc` files with `file(GLOB_RECURSE ...)`, combine `${source_all}`
with `${WAMR_RUNTIME_LIB_SOURCE}` (add `${UNCOMMON_SHARED_SOURCE}` when the
suite directly tests shared-utils code, and `${IWASM_COMPL_SOURCE}` when it
embeds the AOT compiler), then create one executable per test binary. Link
`gtest_main` (add `gmock` only when needed) and register the cases with
`gtest_discover_tests()`. Never hand-write a `main()`, never copy an
`ExternalProject`/`FetchContent` block from another suite, and never list the
runtime sources by hand.

### 6. Suites That Embed the AOT Compiler or Use LLVM

Set `add_definitions(-DWASM_ENABLE_WAMR_COMPILER=1)` together with the other
feature switches (before `unit_common.cmake`), include
`${IWASM_DIR}/compilation/iwasm_compl.cmake` right after `unit_common.cmake`,
add `${IWASM_COMPL_SOURCE}` to the sources, and link `${LLVM_AVAILABLE_LIBS}`
(see `tests/unit/aot/CMakeLists.txt`).

### 7. C Test Suites

If the suite is written in C, link `cmocka::cmocka` and register the
binaries with `add_test()` and `set_tests_properties()` instead of using
Googletest (see `tests/unit/mem-alloc/base/CMakeLists.txt`).

### 8. Do Not Repeat Work the Framework Already Does

The following are prohibited in a suite's `CMakeLists.txt`; the top level or
`unit_common.cmake` already provides them. `tests/unit/interpreter/CMakeLists.txt`
is the minimal reference for the shape a suite should have.

- `include(GoogleTest)` — the top level includes it once.
- `if(COMMAND gtest_discover_tests) ... else() add_test(NAME ...) endif()` —
  `gtest_discover_tests()` is always available.
- Registering the same target both with `gtest_discover_tests()` and
  `add_test(NAME ...)` — the cases would run twice.
- `enable_testing()`, `project()`, `cmake_minimum_required()`,
  `FetchContent`, `add_library(vmlib ...)`, a second
  `include(build-scripts/runtime_lib.cmake)`.
- A hand-written list of runtime sources
  (`${PLATFORM_SHARED_SOURCE}`, `${LIBC_WASI_SOURCE}`, ...) instead of
  `${WAMR_RUNTIME_LIB_SOURCE}`.
- Copying `WAMR_BUILD_xx` into `WASM_ENABLE_xx`.
- Wiring up code coverage: `-DCOLLECT_CODE_COVERAGE` and `--coverage` are
  handled by `build-scripts/config_common.cmake`; a suite must not add
  `--coverage` to `CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS` or to a target.
- Progress/noise output: `message(...)` and
  `add_custom_command(... -E echo "... built successfully")` do not belong in
  a suite.
- Repeated boilerplate: hard-coded `../../../../core/...` include paths (use
  `${WAMR_ROOT_DIR}` or the includes `unit_common.cmake` already adds), a
  `list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})` that finds
  nothing, unused variables, and the same `target_link_libraries()` /
  `target_include_directories()` block copied once per target.

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

  See `tests/unit/memory64/base/CMakeLists.txt` for its usage.

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

- **Copy an existing `.wasm`**: only when the fixture exists solely as a
  `.wasm` file and cannot be regenerated from a `.wat`/`.c` source, copy those
  files (not the whole directory) into the build directory with
  `wamr_unit_test_copy_wasm_files()`:

  ```cmake
  wamr_unit_test_copy_wasm_files(new_feature_test
      FILES ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/legacy_fixture.wasm
      DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
  )
  ```

  `wamr_unit_test_add_wasm_copy_target()` is deprecated: it exists only for
  out-of-tree callers. Use the `wamr_unit_test_compile_*` helpers, or
  `wamr_unit_test_copy_wasm_files(... FILES ...)` followed by
  `add_dependencies(<target> <copy target>)` when several executables share
  one copy step.

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

---

## Keeping the Feature Switch Set Minimal

A switch is needed only if the suite cannot configure, build, or pass without
it. Decide with a toggle probe instead of guessing:

1. Remove the switch's declaration from the suite's `CMakeLists.txt`.
2. Configure and build that suite, and run it:

   ```bash
   cmake -S tests/unit -B build-probe -DFULL_TEST=ON
   cmake --build build-probe --target <suite targets>
   ctest --test-dir build-probe/<suite> --output-on-failure
   ```

3. Restore the file, then read the result:

   - **Builds and passes** — the switch is a leftover: delete the line (and
     leave a short comment saying the suite does not need it when that is not
     obvious).
   - **Build fails** — it is a real dependency: keep it, and leave a comment
     naming the file or symbol that requires it.
   - **Only some cases fail** — those cases need the switch, the others do
     not: split the suite so each half declares only what it needs
     (`tests/unit/llm-enhanced-test/posix/` is the reference for this), or,
     when a split is not practical, guard the cases with
     `#if WASM_ENABLE_xx` and note why.

4. Probe **every run mode the suite declares**, not just the first one. A
   switch can be needed in one mode and idle in another, so a suite with
   `wamr_unit_test_suite_run_modes(<name> MODES classic-interp aot)` needs both
   probes before any line is deleted.

5. Probe the **group** before deleting anything: remove every switch that
   passed step 3 in one go and repeat the build and the run. Per-switch probes
   miss interactions — `llm-enhanced-test/runtime-common-wasi-mem64` built and
   passed with each of `WAMR_BUILD_THREAD_MGR`, `WAMR_BUILD_LIB_PTHREAD` and
   `WAMR_BUILD_SHARED_MEMORY` removed on its own, yet removing all three left
   `wasm_runtime_spawn_exec_env` undefined in the aot build. Delete a group
   only after the group probe passes; otherwise keep the set together and say
   in a comment why.

A "builds and passes" result is evidence only if the same cases ran. Compare
the test count of the probe run with a control run of the same suite and mode
that removes nothing: a switch that only gates cases makes those cases
disappear (or skip themselves), so the suite still "passes" while silently
losing coverage. `tests/unit/compilation/aot_emit_memory_test.cc` guards whole
`TEST_F`s with `#if WASM_ENABLE_SHARED_MEMORY != 0`, for instance. When the
count drops, the switch is needed for coverage: keep it, or move the guarded
cases into a suite of their own.

A switch that is the feature under test (`WAMR_BUILD_GC` for the GC suite,
`WAMR_BUILD_MEMORY64` for the memory64 suite, and so on) is never removed just
to shrink the set, even though the probe will report it as required.

A switch that the probe reports as unnecessary but that selects the runtime
configuration the suite exists to cover stays as well — the suite's name, or a
split whose only difference is that switch, is the evidence. `mem-alloc/gc`
runs the `mem-alloc/base` cases on a GC-enabled runtime and
`llm-enhanced-test/runtime-common-wasi-mem64` runs the runtime-common cases
with `WAMR_BUILD_MEMORY64`; both keep the switch and say why in a comment. The
same applies to a switch the cases were written against even though they do not
read it, such as `aot-stack-frame`'s `WAMR_DISABLE_HW_BOUND_CHECK` and
`WAMR_DISABLE_WRITE_GS_BASE`, whose values the runtime otherwise auto-detects.

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
