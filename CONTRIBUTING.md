# Contributing to WAMR

Welcome bug reports, fixes, tests, docs, feature proposals.

## Before You Start

Open a [GitHub Issue](https://github.com/wasm-micro-runtime/wasm-micro-runtime/issues) first. Use it to report bugs, discuss fixes, suggest tests and doc improvements, and discuss feature proposals before implementation.

## Change Process

- Change code only when it is the only solution.
- Contributors and AI tools MUST keep existing changes. Modify only files the task needs.
- Add or update tests for behavior changes. No test: explain why in the PR, name existing coverage.
- Submit all changes through a PR.
- Set up sample pre-commit hook once: `./ci/setup_pre_commit.sh`. Runs best-effort guideline, clang-tidy, Dockerfile, GitHub workflow checks.
- Run tests relevant to the change. See [unit tests](tests/unit/README.md) and [WAMR test suites](tests/wamr-test-suites/README.md).

## Code Guidelines

- Keep changes minimal. Reuse existing functions, variables.
- Use existing solutions/mechanisms in the codebase. Do not invent new ones. Add no new complexity.
- Keep PR focused, around 300--400 changed lines. Split larger changes ([how](https://www.gustavwengel.dk/pr-sizes)).
- Put formatting-only and refactoring-only changes in separate PRs.
- Prefer platform-specific source directories over platform-selection macros.
- Log every error branch with actionable context.
- Propagate errors through return values.

## Task-Specific Requirements

### New Features

- Put implementation in separate files or directories.
- Guard code with a macro. Control compilation and linking with a compilation flag.
- Add sample in `samples/`.
- Update `doc/`.
- Add tests in `tests/unit/`. New spec or proposal: add spec test in `tests/wamr-test-suites/`.

### New Compilation Flags and Macros

- Add the option to `build-scripts/config_common.cmake`, default `0`.
- Add the macro to `core/config.h`, default `0`.
- Turn the feature on only in the build entry point when the product needs it. Keep it off in `build-scripts/`.
- Test with `#if <MACRO> != 0`. Never `#ifdef`, `#ifndef`, `#if defined(<MACRO>)`.
- Keep CMake path in sync.

### LLVM Upgrades

- Update `build-scripts/build_llvm.py`. Update `.github/workflows/build_llvm_libraries.yml` if needed.
- Verify: AOT LLVM JIT spec tests pass. CI passes with upgraded LLVM libraries. LLVM-dependent features (e.g., PGO, LTO) work.

## Pull Request Checklist

- PR describes problem, solution, validation.
- Code formatted. Relevant tests pass.
- Docs updated when user-visible behavior, configuration, or APIs change.

## License

WAMR is Apache-2.0 with LLVM exception. Contributions use same license. See [LICENSE](LICENSE).

## Code of Conduct

Follow the [Code of Conduct](CODE_OF_CONDUCT.md).
