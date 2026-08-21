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

  - Generate `.wasm` files from `.wat` or `.c` source files.
  - Use `ExternalProject` and the `wasi-sdk` toolchain to compile `.wasm` files during the build process.

- **Keep Using `ctest` as the framework**:
  Continue to use `ctest` for running the test cases, as it is already integrated into the existing test framework.

---

## Writing `CMakeLists.txt` for the Test Suite

When creating a `CMakeLists.txt` file for your test suite, follow these best practices:

1. **Do Not Fetch Googletest Again**:
   The root `unit/CMakeLists.txt` already fetches Googletest. Avoid including or fetching it again in your test suite.

2. **Find LLVM on Demand**:
   If your test suite requires LLVM, use `find_package` to locate LLVM components as needed. Do not include LLVM globally unless required.

3. **Include `unit_common.cmake`**:
   Always include `../unit_common.cmake` in your `CMakeLists.txt` to avoid duplicating common configurations and utilities.

   Example:

   ```cmake
   include("../unit_common.cmake")
   ```

4. **Use `WAMR_RUNTIME_LIB_SOURCE`**:
   Replace long lists of runtime source files with the `WAMR_RUNTIME_LIB_SOURCE` variable to simplify your configuration.

   Example:

   ```cmake
   target_sources(your_test_target PRIVATE ${WAMR_RUNTIME_LIB_SOURCE})
   ```

5. **Avoid Global Compilation Flags**:
   Do not define global compilation flags in the `unit` directory. Each test case should specify its own compilation flags based on its unique requirements.

---

## Generating `.wasm` Files

- **Compile `.wasm` Files Dynamically**:
  Use `ExternalProject` in your `CMakeLists.txt` to compile `.wasm` files from `.wat` or `.c` source files.
  - Use the `wasi-sdk` toolchain for `.c` or `.cc` source files.
  - Example configuration:
    ```cmake
    ExternalProject_Add(
        generate_wasm
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
        BUILD_ALWAYS YES
        CONFIGURE_COMMAND  ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps -B build
                              -DWASI_SDK_PREFIX=${WASI_SDK_DIR}
                              -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
        BUILD_COMMAND      ${CMAKE_COMMAND} --build build
        INSTALL_COMMAND    ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}/wasm-apps
    )
    ```
- **Example for `wasm-apps` Directory**:
  Place your source files in a `wasm-apps/` subdirectory within your test suite directory.

  - Create a `CMakeLists.txt` in `wasm-apps/` to handle the compilation of these files.
  - Example `CMakeLists.txt` for `wasm-apps/`:

    ```cmake
    cmake_minimum_required(VERSION 3.13)
    project(wasm_apps)

    add_executable(example example.c)
    set_target_properties(example PROPERTIES SUFFIX .wasm)
    install(TARGETS example DESTINATION .)
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

   The runtime mode can be selected with one of the following existing build
   options:

   - Classic interpreter: default configuration.
   - Fast interpreter: `-DWAMR_BUILD_FAST_INTERP=1`
   - LLVM JIT: `-DWAMR_BUILD_JIT=1`
   - AOT: `-DWAMR_BUILD_AOT=1`
   - Fast JIT: `-DWAMR_BUILD_FAST_JIT=1`

   Only one runtime mode can be selected for each build. Using separate build
   directories for different modes is recommended, but not required.

   For example, to configure the LLVM JIT mode:

   ```bash
   cmake -S . -B build-jit -DWAMR_BUILD_JIT=1
   ```

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
