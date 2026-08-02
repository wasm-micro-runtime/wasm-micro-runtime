# Contributing to WAMR

We welcome bug reports, fixes, tests, documentation, and feature proposals.

## Before You Start

Always start by creating a [GitHub Issues](https://github.com/wasm-micro-runtime/wasm-micro-runtime/issues). You can use these to report bugs and to discuss related fixes, suggest new tests, improvements to documentation, and to discuss feature proposals before implementation.

## Change Process

- Contributors and AI tools MUST preserve existing changes and modify only files needed for the task.
- Add or update tests for behavior changes. If no test is added, explain why in the pull request and identify applicable existing coverage.
- Submit all changes through a pull request.
- Format changed C and C++ files with `clang-format-21 --style=file -i <file>` before submitting.
- Run `clang-tidy-diff.py` with `.clang-tidy` before submitting to report `clang-tidy` diagnostics only on changed C and C++ lines.
- Run tests relevant to the change. See [unit tests](tests/unit/README.md) and [WAMR test suites](tests/wamr-test-suites/README.md) for commands and supported configurations.

## Code Guidelines

- Keep changes minimal. Reuse existing mechanisms, functions, and variables when they meet the need.
- Keep each pull request focused and approximately 300--400 changed lines. [Split larger changes into reviewable pull requests](https://www.gustavwengel.dk/pr-sizes). AI will be good at splitting large changes into smaller ones, but humans should review the split for correctness and completeness.
- Submit formatting-only and refactoring-only changes in separate pull requests.
- Prefer platform-specific source directories over platform-selection macros.
- Log every error branch with actionable context.
- Use return values to propagate errors.

## Task-Specific Requirements

### New Features

- Add a compilation option in `build-scripts/config_common.cmake`. Default it to `0` unless the feature must be enabled by default.
- Add the corresponding macro switch in `core/config.h`. Default it to `0` unless the feature must be enabled by default.
- Update `doc/build_wamr.md` with the option and its usage.
- Add or update a demonstration in `samples`.

### LLVM Upgrades

- Update `build-scripts/build_llvm.py`.
- Update `.github/workflows/build_llvm_libraries.yml` as needed for the new LLVM version.
- Verify the AOT LLVM JIT spec tests pass.
- Verify CI can use the upgraded LLVM libraries and passes.
- Verify features highly dependent on LLVM (e.g., PGO, LTO) work as expected.

## Pull Request Checklist

- The pull request describes the problem, solution, and validation performed.
- Changed code is formatted and relevant tests pass.
- Documentation is updated when user-visible behavior, configuration, or APIs change.

> ![TIPS]
> Unfinished, waiting for more

## License

WAMR is licensed under Apache-2.0 with the LLVM exception. Contributions are submitted under the same license. See [LICENSE](LICENSE).

## Code of Conduct

Follow the [Code of Conduct](CODE_OF_CONDUCT.md).
