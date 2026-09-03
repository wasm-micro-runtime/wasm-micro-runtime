# addr2line.py tests

Pytest suite for `test-tools/addr2line/addr2line.py`. Replaces the older
diagnostic harness that lived under `samples/debug-tools/llvm-bug-experiment/`.

## What's covered

Each test case exercises one specific aspect of `addr2line.py`:

| Test | Source | Purpose |
|------|--------|---------|
| `test_build_subprogram_intervals_filters_invalid_dies` | `apps/simple.c` (+ wasm-opt post-process) | Builds a wasi-libc-linked wasm and post-processes with `wasm-opt -Oz -g` to trigger the DCE-ghost pattern. Asserts the resulting interval table contains no `low_pc = 0` entries and no `high_pc <= low_pc` entries. Guards the skip-guard invariant in `build_subprogram_intervals`. |
| `test_simple_resolution` | `apps/simple.c` | Baseline. One function, one trap. Asserts the resolved frame names the function and points at its source file — proves the basic address → `(func, file:line)` plumbing works end to end. |
| `test_inline_chain_basic` | `apps/always_inline.c` | A trap inside an `__attribute__((always_inline))` helper produces a 2-frame inline chain (the helper plus its caller). Asserts addr2line emits BOTH names and the `(inlined into <next>)` annotation that ties them together — guards against future regressions in `print_frames`. |
| `test_inline_chain_deep` | `apps/deep_inline_chain.c` | Four nested always-inline functions all collapse into one WASM function under `-O0 -g`. Asserts all four names render in the inline chain AND that the outermost frame name is `app_main` (not a wasi-libc symbol like `__multi3` that overlaps app_main's PC range). This is the test that catches the "(inlined into free)" / wasi-libc-shadow class of legacy-resolver regressions. |
| `test_cross_tu_inline` | `apps/multi_file_recur_*.c` | The canonical trigger for both LLVM outermost-name failure modes: two TUs, `-Oz -flto`, then `wasm-opt -Oz -g` (which produces DCE ghosts). Both the symbol-table override and the AddrDieMap invariant violation apply here, so the test only pins the limited contract that the *line table* stays inside our source files (or returns `??:0`) — it must NEVER reach into wasi-libc / wasi-sysroot. |
| `test_trap_mid_function` | `apps/trap_in_loop.c` | Trap is inside a loop body, several instructions past function entry. Asserts the resolved line is > 10 (i.e. NOT the function declaration line). Catches future regressions where addr2line might collapse mid-function addresses back to function-entry. |
| `test_multi_frame_callstack` | `apps/multi_frame.c` | Four distinct WASM functions in a call chain. Asserts every frame renders with the correct function name and a distinct frame index — catches frame-numbering or per-frame-resolution bugs that a single-frame test misses. |
| `test_cxx_demangling` | `apps/cxx_mangled.cpp` | A templated, namespaced C++ function exercises the cxxfilt pass. Asserts mangled `_Z...` never leaks to the user and that the line table resolves to our `.cpp` (not wasi-libc). Doesn't pin the symbolizer's function-name choice — both clang < 22 and clang 22+ pick the wrong subprogram on some C++ template addresses on wasm. |
| `test_aot_mode` | `apps/simple.c` (`-Oz -g`) | `--mode=aot` uses no `-1` adjustment (wamrc commits ip at instruction-start, not post-advance). The test picks an address at `low_pc+delta` and asserts the function still resolves under `--mode=aot`. Skips when `crash()` gets inlined away under `-Oz`. |
| `test_fast_interp_mode` | `fixtures/fast_interp_stack.txt` + `apps/simple.c` | The fixture has `$f0`/`$f1` indices; `--mode=fast-interp` forces function-name lookup (offsets aren't mappable). Asserts both indices resolve to the real wasm name-section entries (`crash`/`app_main`). |
| `test_no_addr_mode` | `fixtures/fast_interp_stack.txt` + `apps/simple.c` | Same fixture; `--no-addr` is the explicit form of function-name lookup. Additionally asserts the DWARF decl-file (`simple.c`) is surfaced (function-name lookup falls back to declaration-line, not call-site). |
| `test_offset_zero_fallback` | `apps/simple.c` (inline cs) | WAMR reports `offset=0` when `frame_ip` wasn't captured (trap at function entry, top frame of `iwasm -f`, etc.). The test asserts addr2line falls back to function-name resolution gracefully and prints the `app_main` name — does NOT crash or assert. |
| `test_empty_input` | `fixtures/empty_stack.txt` + `apps/simple.c` | A zero-line call stack should exit cleanly (rc=0) and produce no output. Defends against an `IndexError`-style crash when the loop body never runs. |

Sources under `apps/` are written for the test suite, not copied from
`samples/`. They're small (~10–25 lines each) and target specific edge
cases that aren't covered by the sample-CI integration tests.

## Running

```bash
# Default: against $WASI_SDK_PATH or /opt/wasi-sdk
./run_tests.sh
# or
python3 -m pytest -v

# Fixture-only (fast, no builds)
./run_tests.sh -m "not slow"

# Multi-SDK: parametrize build-based tests over every detected
# /opt/wasi-sdk-*-x86_64-linux installation. Run each build-based
# test once per detected wasi-sdk.
./run_tests.sh --multi-sdk

# Verbose: print each test's addr2line input + stdout/stderr so you
# can see what got resolved. Use -v -s to surface it live; -v alone
# captures it and only shows it on failure.
./run_tests.sh -v -s
```

## Background: why the interval-table overlay exists

`addr2line.py` exists primarily to convert WAMR call-stack dumps into
source `file:line:column`. On most well-behaved DWARF inputs this is
a thin wrapper around `llvm-symbolizer -f -i`. But on wasm targets,
the symbolizer's OUTERMOST frame's function name can be wrong for
two independent reasons — both source-verified against upstream
LLVM (`SymbolizableObjectFile.cpp` and `DWARFUnit.cpp`):

1. **Symbol-table override on the outermost frame** (LLVM design
   choice). `symbolizeInlinedCode` unconditionally rewrites the
   outermost frame's `FunctionName` from `getNameFromSymbolTable`
   whenever `FunctionNameKind::LinkageName` and `UseSymbolTable` are
   both set. Inline frames are provably NOT rewritten — the override
   is applied only to `getMutableFrame(getNumberOfFrames() - 1)`. On
   some wasi-sdk builds the symbol-table lookup returns the wrong
   function for an address that DWARF places correctly.
2. **`AddrDieMap` invariant violation** (root cause in binaryen's
   DWARF preservation model). `wasm-opt -Oz -g` deletes functions
   from the wasm module but its DWARF updater does not remove the
   corresponding `DW_TAG_subprogram` DIEs — instead, when an old
   address can no longer be mapped to a surviving IR node,
   `LocationUpdater::getNewFuncStart` in `src/wasm/wasm-debug.cpp`
   returns `0` as a tombstone value and `updateDIE` writes it into
   `DW_AT_low_pc`. The `DW_TAG_subprogram` remains in place; sometimes
   its `DW_AT_high_pc` retains its original value. Result: stale
   "ghost" DIEs like `fmaf [0, 0x72)`. LLVM's `updateAddressDieMap`
   in `DWARFUnit.cpp` assumes children are inserted after parents
   and each child range ⊆ parent range; these ghosts violate that
   invariant, so the map's `upper_bound − 1` lookup returns an
   insertion-order-dependent DIE for real addresses.

The symptom is the same in both cases — only the OUTERMOST frame's
function name is wrong. Line/column info and inner inlined frame
names are correct because they come from a different LLVM code path
(`getInliningInfoForAddress`, which walks `getParent()` links from
the leaf DIE and does NOT go through the symbol-table override).

`addr2line.py` always runs `llvm-dwarfdump --debug-info` at startup,
builds a `(low_pc, high_pc, name)` interval table for every real
callable `DW_TAG_subprogram` (three skip guards described below), and
overlays the outermost frame's name from an innermost-wins lookup on
that table.

### Skip guards applied to the interval table

- `DW_AT_declaration=true` → forward declaration only, no code.
- `low_pc == 0` → structurally impossible for real code (wasm binary
  format guarantees the Code section starts after Type + Function
  section headers). Any `low_pc = 0` DIE is a wasm-opt DCE ghost or
  a broken producer's output.
- `high_pc <= low_pc` → empty or degenerate range. Never observed
  in practice; kept as a defense-in-depth guard against future
  toolchain changes.

Multi-SDK CI (`./run_tests.sh --multi-sdk`) exercises the parser
against DWARF produced by wasi-sdk 29 (clang 21) and wasi-sdk 33
(clang 22) to catch DIE-encoding differences between clang versions.

## Why we left samples/debug-tools/llvm-bug-experiment behind

The earlier experiment was useful for diagnosing the bug but wasn't a
proper test suite — it was a one-shot script that printed status to
stdout. This pytest suite covers more cases, runs in CI per-PR (default
SDK) and nightly (multi-SDK), and is decoupled from sample evolution.

## Layout

```
test-tools/addr2line/tests/
├── README.md             # this file
├── conftest.py           # fixtures (wasi_sdk, build_wasm, run_addr2line, ...)
├── test_addr2line.py     # the tests
├── pytest.ini            # marker definitions
├── run_tests.sh          # convenience wrapper
├── apps/                 # purpose-built test sources
│   ├── simple.c
│   ├── always_inline.c
│   ├── deep_inline_chain.c
│   ├── multi_file_recur_main.c
│   ├── multi_file_recur.c
│   ├── trap_in_loop.c
│   ├── multi_frame.c
│   └── cxx_mangled.cpp
└── fixtures/             # plaintext call-stack inputs
    ├── fast_interp_stack.txt
    ├── aot_stack.txt
    └── empty_stack.txt
```
