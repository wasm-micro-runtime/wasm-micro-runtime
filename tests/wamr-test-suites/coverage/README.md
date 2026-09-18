# WAMR Code Coverage

Parameterized code coverage measurement for WAMR, based on **GCC `--coverage`
(gcov data) + gcovr** (line / function / branch). Everything coverage-related
lives in this directory; the former lcov/genhtml `collect_coverage.sh` is gone.

## Scope

Only the WAMR core sources are counted:

- **counted**: `core/iwasm`, `core/shared`
- **excluded**: `core/deps`, `tests/`, `samples/`, `product-mini/`,
  `wamr-compiler/`, `test-tools/`

Regression tests (`tests/regression/ba-issues`) are **not** part of the
coverage scope; see [Regression tests](#regression-tests).

## Toolchain

- gcc/gcov: the system toolchain (`build-essential` on Debian/Ubuntu).
- gcovr 6.0: `python3 -m gcovr` must work for the interpreter that runs the
  collector (`pip install gcovr==6.0`).
- LLVM 18.1.8: built by `build-scripts/build_llvm.py` at `core/deps/llvm/build`
  — required by the unit suites that build against `LLVM_DIR`. `--llvm-dir`
  defaults to that bundled build; pass it only for a custom LLVM.

## Scripts

| Script | Purpose |
|---|---|
| `run_coverage.py` | Parameterized entry: build + run spec/unit for a report, collect gcovr reports, merge reports. |
| `coverage_targets.py` | The feature set F (parses `--feature`) and the unit-target selection: reads `compile_commands.json` as cmake's build plan and picks the targets whose configuration fits inside F (E ⊆ F). |
| `run_classic_fset.py` | The canned classic-interp feature-set report: that mode + a fixed feature set + unit. |
| `run_full.py` | Full run: every spec variant plus the unit suites of the supported modes, merged into `_merged/`. |
| `run_minimum.py` | The minimum unit report: classic-interp + the bare feature set, i.e. only the suites that enable no feature of their own. |
| `collect_coverage_gcovr.py` | gcovr collector: one or more build dirs → HTML + JSON + txt reports (scope-filtered). |
| `tests/regression/ba-issues/build_run.py` | Build + run the BA-issue regression tests; quality gate, not part of the coverage scope. |

## Pipeline: orchestrator and collector

`run_coverage.py` is the **orchestrator**: it decides what runs and which build
directories count. `collect_coverage_gcovr.py` is the **collector**: it knows
nothing but "a list of build dirs plus one output dir". The two never import
each other — the orchestrator shells out, and so does `test_wamr.sh -C`.

```
run_coverage.py  (orchestrator)
 ├── bash test_wamr.sh -s spec -b -t <mode> -C          # run the spec suite
 │     ├── builds iwasm in place -> product-mini/platforms/<platform>/build
 │     └── -C: python3 collect_coverage_gcovr.py --out <test-suites>/coverage-report <build dir>
 ├── cmake + ctest on tests/unit (per running mode)      # run the unit suites
 └── python3 collect_coverage_gcovr.py --out <report> <spec copy> <unit suite dirs...>

test_wamr.sh -C   (used standalone, or via -s unit / -s regression)
 └── python3 collect_coverage_gcovr.py --out <test-suites>/coverage-report <build dirs...>
```

### Why there are two collection passes

A single `run_coverage.py` report triggers gcovr **twice** over the same spec
`.gcda` data, on purpose:

1. **`test_wamr.sh -C`** collects the product build dir on its own into
   `tests/wamr-test-suites/workspace/coverage-report/`. That report belongs to
   `test_wamr.sh`'s `-C` contract (the same path serves `-s unit` and
   `-s regression`) and covers the spec build only.
2. **`run_coverage.py`** then collects again from the spec build **plus** every
   unit suite selected for F — that merged result is the actual report.

Pass 2 needs its own copy of the spec data because `test_wamr.sh` builds
`product-mini/platforms/<platform>/build` in place and `run_spec()` wipes it
before every run. Right after each spec run, `run_spec()` therefore copies that
directory's `.gcno`/`.gcda` into `<out>/_work/<report>/spec-coverage-<mode>/`,
which is what pass 2 collects from (and what makes a later `--merge` able to see
every spec variant).

## Reports, fingerprints and logs

A **report** is one `(running mode, spec options, feature set)` combination. Its
**fingerprint** is `running mode + spec options + selected unit targets and
their macro sets`: the unit half is taken from the build plan (the macros cmake
resolved), not from the spelling of F, so two spellings of the same
configuration produce the same fingerprint and the same report directory, and
reports stay comparable across runs.

`run_coverage.py --help` prints the output layout. In short: the report
directory `<out>/<report>_<fingerprint>/` holds `index.html` / `*.html`,
`coverage.json`, `summary.txt`, `summary.json`, `fingerprint.txt`,
`unit-selection.txt` and — only when a step failed — `failures.txt`;
`<out>/_work/<report>/` keeps the build dirs the data was collected from plus
`logs/` with the output of every child process.

`cmake`, `ctest`, `test_wamr.sh` and `gcovr` are all extremely chatty, so
**their output never reaches the console** — it goes to those log files.

A failing step does **not** withhold the report. Whatever ran before the
failure has already written its `.gcda`, and a partial report is more useful
than none, so the run continues: a failing spec run is still snapshotted, a
failing unit suite does not stop the remaining suites (a failing unit *build* or
*configure* does skip what depends on it), everything collected is written and
summarized, and the failures are listed in the report's `failures.txt`, echoed
at the end, and turned into a non-zero exit status. `run_full.py` likewise keeps
the matrix going and still merges, then exits non-zero naming the affected
reports. The console carries the orchestrator's own lines only: the
report's mode/spec command/feature set, the resolved paths, the unit selection
with its curation warnings, the per-suite test counts, and the line / function /
branch summary read back from `summary.json`.

Paths are printed in their **repository-relative** spelling with the absolute
path underneath when the two differ: inside the devcontainer the absolute path
is `/workspaces/...`, which does not exist on the host.

`--out` is resolved against the directory the command is invoked from (the entry
scripts resolve it before launching the inner runner, which runs with
`cwd=<repository root>`), and the resolved location is printed at startup.

Reports support merging:

- **same-report multi-test merge**: with `--unit`, the `.gcda` of the spec and
  unit runs is merged into one report;
- **cross-report merge**: repeated `--merge <report>` re-collects the union of
  the `_work/<name>/` build dirs of those reports into `_merged/`.

## Usage

Run from anywhere in the repository (the repository root is auto-detected).

```bash
# single report: classic-interp + a curated feature set + spec + unit
python3 tests/wamr-test-suites/coverage/run_coverage.py \
    --report classic-fset --mode classic-interp \
    --feature "-DWASM_ENABLE_LIBC_BUILTIN=1 \
               -DWASM_ENABLE_BULK_MEMORY=1 -DWASM_ENABLE_BULK_MEMORY_OPT=1 \
               -DWASM_ENABLE_SHRUNK_MEMORY=1" \
    --unit --out build/coverage

# spec variant: test_wamr.sh -s spec -b -t classic-interp -C -G
python3 tests/wamr-test-suites/coverage/run_coverage.py \
    --report gc --mode classic-interp --spec "-G" --out build/coverage

# no feature constraint: every unit target belongs to the report
python3 tests/wamr-test-suites/coverage/run_coverage.py \
    --report ci --mode classic-interp --unit --full-test \
    --out build/coverage

# the canned classic-interp feature-set report, the minimum one, the full matrix
python3 tests/wamr-test-suites/coverage/run_classic_fset.py --out build/coverage
python3 tests/wamr-test-suites/coverage/run_minimum.py --out build/coverage
python3 tests/wamr-test-suites/coverage/run_full.py --out build/coverage
# ... the same matrix without the llm-enhanced-test submodule suites
python3 tests/wamr-test-suites/coverage/run_full.py --no-full-test --out build/coverage

# merge two previously generated reports
python3 tests/wamr-test-suites/coverage/run_coverage.py \
    --merge classic-fset --merge gc --out build/coverage
```

`run_classic_fset.py`, `run_minimum.py` and `run_full.py` are fixed pipelines:
they take only `--out` and `--llvm-dir` (plus `--no-full-test` for
`run_full.py`), and always run everything they describe.

`--spec` only carries the extra `test_wamr.sh` switches — `-s spec` (spec suite)
and `-b` (use the wabt binary release instead of compiling wabt) are always
passed. The switches used by `run_full.py`:

| Variant | `--spec` |
|---|---|
| default | (none) |
| gc | `-G` |
| exception-handling | `-e` |
| extended-const | `-N` |
| memory64 | `-W` |
| multi-memory | `-E` |
| threads | `-p` |

## Feature set F

`--feature` takes the report's feature set as **compile macros** — the plane the
compiler sees, and the plane the coverage numbers are computed in:

```bash
--feature "-DWASM_ENABLE_GC=1 -DWASM_ENABLE_REF_TYPES=1"
```

F describes **features** only. The running mode is a report dimension of its own
(`--mode`): `run_coverage.py` configures the whole unit build with that mode's
`WAMR_BUILD_*` flags, so every target agrees on `INTERP`, `FAST_INTERP`, `JIT`,
`FAST_JIT`, `LAZY_JIT`, `AOT` (`coverage_targets.MODE_MACROS`). They can never
discriminate between targets, so they are dropped from **both sides** of the
comparison and a classic-interp report need not spell out `INTERP=1`. Writing
one in `--feature` is allowed but ignored (and warned about).

F is an **upper bound** for the unit selection: a macro it does not mention is 0,
so a target that enables anything F does not declare is left out.
`-DWASM_ENABLE_XXX=0` may be written for emphasis but is redundant. There is no
feature checklist to maintain and no cmake-variable → macro translation table:
implications (`GC` → `REF_TYPES`, `JIT` → `INTERP`, ...) are cmake's job and are
already resolved in the macros every compile unit is invoked with.

F may declare more than the selected targets enable — a target covering a
*subset* of F is admitted, and the F macros no selected target enables are
reported as a warning. That keeps the pick-up wide (a suite that does not turn on
the runtime under test still contributes) while the report still never contains
code compiled with a feature F does not declare.

An *empty* F is the one exception: it is a wildcard, every unit target belongs to
the report, and each suite keeps the values its own `CMakeLists.txt` declares.

### F selects, it does not filter

The unit build of the mode is **configured** first (`cmake -S tests/unit -B
<work>/unittest-build-<mode> -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`), and nothing is
built yet. `compile_commands.json` is then read as cmake's *build plan*: every
entry already names the target it belongs to (its object path,
`<build>/<suite>/CMakeFiles/<target>.dir/...`, read from the `-o` argument) and
the macros that target is compiled with, so the selection is a set comparison:

```
target belongs to the report  <=>  the feature macros it enables are a subset of the macros F enables
```

A **suite** belongs to the report only when *all* of its targets do, because a
suite is what `ctest` runs and what the collector collects; a partially matching
suite is excluded as a whole (and reported) rather than half-collected. Only then
are the selected targets built, the selected suites tested, and only their build
directories collected.

F is **not** injected into any configure — writing `-DWASM_ENABLE_GC=1` here does
**not** turn GC on for a build. The unit suites hard-code their own
`WAMR_BUILD_*` switches (CMake directory scope), so unit coverage is the union of
each sub-suite's own configuration, and the spec layer is configured by
`test_wamr.sh` itself. F is a statement about *what the report covers*.

### Warnings instead of silence

A feature set that does not fit the unit suites is a curation problem, not a
build error, so it is reported — printed once after the configure, before the
spec layer starts, and recorded in `unit-selection.txt` — and the run continues:

- an enabled macro that **no unit target** enables (e.g.
  `WASM_ENABLE_SPEC_TEST=1`, configured by `test_wamr.sh` rather than by the unit
  suites) — the unit half cannot cover that feature;
- an enabled macro that **no selected** unit target enables while some unselected
  target does — admitted by the subset rule, but not exercised either;
- an enabled macro that is neither a `core/config.h` `#ifndef` default nor used
  by any compile unit of this build — most likely a typo;
- a running-mode macro in F — accepted, but ignored by the selection;
- a suite of which only *some* targets match F;
- an F that selects **no** unit target at all — the warning then names the
  closest target and what it enables beyond F, so one run is enough to curate F.

`unit-selection.txt` keeps the full record: the selected suites and each target's
macro set, the skipped and partially matching suites, and every warning.

## Unit test: full and minimum

The unit half has two canned shapes, both built out of the switches above:

| | full (`run_full.py`) | minimum (`run_minimum.py`) |
|---|---|---|
| suites built | every one, `FULL_TEST=ON` (incl. `llm-enhanced-test`) | `tests/unit` only, `FULL_TEST=OFF` |
| F | none — no suite is filtered out | `MINIMUM_FEATURE_SET`: cmake's own defaults (`BULK_MEMORY`, `BULK_MEMORY_OPT`, `SHRUNK_MEMORY`) plus `LIBC_BUILTIN` |
| what it measures | the upper bound: everything the unit suites can reach | the baseline: a runtime built without any feature switch |

Because F is an upper bound, minimum keeps exactly the suites that enable no
feature of their own (plus the libc-builtin ones); everything that turns on GC,
memory64, exception handling, multi-module, shared heap, ... is skipped. The
selected suites are printed after the configure and recorded in
`unit-selection.txt`, so the list is never copied into this file.

Both shapes still run the spec suite: it is configured by `test_wamr.sh` and is
not gated on F. A curated middle ground is `--feature` (see
`run_classic_fset.py`).

## Collecting with test_wamr.sh (`-C`)

```bash
cd tests/wamr-test-suites
./test_wamr.sh -s spec -b -C -t classic-interp
```

Reports land under `tests/wamr-test-suites/workspace/coverage-report/`.

## Regression tests

The BA-issue regression tests are a quality gate, not part of the coverage
report. Run them directly (see `tests/regression/ba-issues/README.md`):

```bash
cd tests/regression/ba-issues

# build + run every active case (runtimes auto-derived from running_config.json)
python3 build_run.py

# only classic-interp cases
python3 build_run.py --mode classic-interp

# build with gcov data (-DCOLLECT_CODE_COVERAGE=1)
python3 build_run.py --coverage
```

To turn their `.gcda` into a report, point the collector at the runtime build
directories:

```bash
python3 tests/wamr-test-suites/coverage/collect_coverage_gcovr.py \
    --out build/regression-coverage \
    tests/regression/ba-issues/build/build-iwasm-*
```

## Unit test new cases: cmocka first

New unit cases should prefer **cmocka** (its stub/mock support fits C projects
better than GoogleTest). See `tests/unit/mem-alloc/` as the template
(`mem_alloc_test.c` + `test_runner.c` + `cmocka::cmocka` + ctest registration +
`-DWAMR_BUILD_TEST=1`). Existing gtest cases are kept as is.
