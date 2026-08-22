# flag-tracer

Answers: **which build switches guard the code this change touches?**

Given a diff, a commit, or a PR number, it reports the `WAMR_BUILD_*`
combinations under which the changed lines are actually compiled — so CI can
pick the configurations worth building, and a reviewer can tell at a glance
that a change is invisible in the default build.

## Usage

```sh
python3 test-tools/flag-tracer                      # uncommitted changes
python3 test-tools/flag-tracer --commit HEAD
python3 test-tools/flag-tracer --pr 5034
git diff | python3 test-tools/flag-tracer --diff -
```

Output is JSON:

```json
{
  "targets": ["iwasm"],
  "configs": [
    {
      "flags": ["WAMR_BUILD_GC=0", "WAMR_BUILD_REF_TYPES=1"],
      "confidence": "high",
      "source": "preprocessor",
      "targets": ["iwasm"],
      "evidence": [{"file": "core/iwasm/interpreter/wasm_loader.c",
                    "line": 1007,
                    "macros": ["WASM_ENABLE_GC", "WASM_ENABLE_REF_TYPES"]}]
    }
  ],
  "flags_union": ["WAMR_BUILD_GC=0", "WAMR_BUILD_REF_TYPES=1"],
  "unmapped_macros": [], "unattributed_files": [], "skipped_files": []
}
```

Switches within one `config` are a **conjunction** (they must hold together);
separate `config` entries are **alternatives** — either is worth building.

`targets` says which build the change lands in: `iwasm`, `wamrc`, or both.
`core/iwasm/compilation/` is always compiled by wamrc but only reaches iwasm
with `WAMR_BUILD_JIT=1`, so a change there reports both.

## Two-way verification

`--verify` stops the result being a guess. For each reported combination it
splices an `#error WAMR_FLAG_PROBE:<id>` above the changed line, then
preprocesses that file twice — once with the switches as reported, once with
every one flipped:

| round | expectation | verdict when violated |
| --- | --- | --- |
| switches as reported | the probe **must** fire | `over_broad` |
| switches flipped | the probe **must not** fire | `unconditional` |

`#error` fires only when that exact line is being compiled, and it cannot fire
at all when the file is not part of the build — so one probe checks both the
line-level and the file-level claim.

Each config gains `verdict` (`verified` / `over_broad` / `unconditional` /
`not_in_build` / `error` / `skipped`) and `verified_with` (the target whose
cmake project was used).

Cost: no compilation and no build — only `cmake` configure (cached per switch
set) plus one `-E` run per probe. Two configures per combination, capped by
`--verify-max` (default 8; what was skipped is reported, never silently
dropped).

`not_in_build` means the file is not part of the cmake project being used
(`product-mini/platforms/linux` by default) — `core/iwasm/compilation/` needs
`--cmake-source=wamr-compiler`, for instance. `--strict` turns `over_broad` /
`unconditional` into exit code 3.

Requirements: python3 and git (`--verify` additionally needs cmake and a
compiler). Nothing else — no third-party packages, and the
local and CI code paths are identical.

## How it works

1. **Preprocessor scope** (`confidence: high`) — the `#if/#else/#endif` block
   stack around every changed line gives the `WASM_ENABLE_*` macros guarding it.
2. **The file's own prerequisites** — the switch that has to be on for the
   file to be compiled at all, from the cmake walk: the conditional
   `include()` of its directory (`libraries/lib-pthread/`, `aot/`, ...), or a
   conditional `set()` naming that one source (`wasm_mini_loader.c` needs
   `WAMR_BUILD_MINI_LOADER`). These are added to *every* combination the file
   produces, and stand alone (`confidence: medium`) for unguarded lines.
3. **The guard line itself** (`source: macro-edit`) — when the changed line
   *is* part of an `#if`, the macros written on that line are the answer.
   Adding `|| WASM_ENABLE_SHARED_HEAP != 0` to a twelve-macro chain is a
   change to `WAMR_BUILD_SHARED_HEAP`, not to the other eleven.
4. **cmake diffs** — switches are named in the diff, so they are read directly.

`#if A || B` yields two alternatives rather than one conjunction, and
`\`-continued conditions are joined before parsing.

Polarity is tracked: `#if WASM_ENABLE_GC == 0`, `#ifndef`, `#else` and `!X`
all yield `WAMR_BUILD_GC=0`, so the reported combination is the one that
actually compiles the code. When a switch is constrained twice (`#if A || B`
wrapping `#if A == 0`) the innermost guard wins.

`WASM_ENABLE_* -> WAMR_BUILD_*` is not a name substitution
(`WASM_ENABLE_FAST_INTERP` needs `WAMR_BUILD_INTERP` too; `WASM_ENABLE_TAGS`
comes from `WAMR_BUILD_EXCE_HANDLING`). The mapping lives in `known_map.py`,
generated from the cmake tree so the common path costs zero IO:

```sh
python3 test-tools/flag-tracer/cmake_map.py .    # regenerate the table
```

`tests/test_map_drift.py` re-parses the cmake tree on every CI run and fails if
the committed table has drifted.

## Limits

- `#if` expressions are scanned lexically, not evaluated. `&&` and `||` are
  modelled, parenthesisation is not, so a compound condition can come out
  slightly wide. Deliberately so: a missed switch is worse than an extra one.
  The product of nested alternatives is capped at 16 per line.
- Macros renamed through an intermediate `#define` are not followed.
- Files with no attribution are listed under `unattributed_files` — that means
  "compiled in every configuration", which is a result, not a failure.
- Switches that happen to default to on (`WAMR_BUILD_INTERP`, ...) are kept in
  the output rather than filtered out. A combination is meant to be a complete,
  self-contained description of what compiles the code — filtering against the
  current defaults would make results depend on when they were produced, and
  the defaults are not the same for every cmake project.
- Turning a macro *off* is reported as its most specific switch only
  (`WASM_ENABLE_FAST_INTERP=0` → `WAMR_BUILD_FAST_INTERP=0`, leaving
  `WAMR_BUILD_INTERP` alone) — the negation of a conjunction is a disjunction,
  and this is the useful branch of it.

## Tests

```sh
cd test-tools/flag-tracer/tests && python3 -m pytest -q
```
