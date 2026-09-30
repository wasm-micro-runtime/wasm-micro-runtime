# WAMR Code Coverage

Code coverage measurement for WAMR, based on **GCC `--coverage` (gcov data) +
gcovr** (line / function / branch). Everything coverage-related lives in this
directory; the former lcov/genhtml `collect_coverage.sh` is gone.

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
| `run_coverage.py` | Entry for one report: build + run spec/unit for it and collect them with gcovr; also holds `merge_reports()`, the tracefile merge `run_full.py` ends with. |
| `coverage_targets.py` | The feature set F (parses `--feature`) and the unit-target selection: reads `compile_commands.json` as cmake's build plan and picks the targets whose configuration fits inside F (E ⊆ F). |
| `run_classic_fset.py` | The canned classic-interp feature-set report: that mode + a fixed feature set + unit. |
| `run_full.py` | Full run: every spec variant plus the unit suites of the supported modes; merges everything it wrote into `_merged/`. |
| `run_minimum.py` | The minimum unit report: classic-interp + the bare feature set, i.e. only the suites that enable no feature of their own. |
| `collect_coverage_gcovr.py` | gcovr collector: build dirs and/or previously generated reports (`--add-tracefile`) → HTML + JSON + txt reports (scope-filtered). |
| `test_wamr.sh -C` | Collects each test suite into its own report, covering every running mode the invocation tested; see [Collecting with test_wamr.sh](#collecting-with-test_wamrsh--c). |
| `tests/regression/ba-issues/build_run.py` | Build + run the BA-issue regression tests; quality gate, not part of the coverage scope. |

## Pipeline: orchestrator and collector

`run_coverage.py` is the **orchestrator**: it decides what runs and which build
directories count. `collect_coverage_gcovr.py` is the **collector**: it knows
nothing but "a list of build dirs and/or tracefiles plus one output dir". The two
never import each other — the orchestrator shells out, and so does
`test_wamr.sh -C`.

```
run_coverage.py  (one invocation, one report)
 ├── bash test_wamr.sh -s spec -b -t <mode> -C      # run the spec suite
 │     ├── builds iwasm into product-mini/platforms/<platform>/build/<mode>/
 │     └── -C: one report per suite under $COVERAGE_DIR
 ├── cmake + ctest on tests/unit                     # the selected unit suites
 └── python3 collect_coverage_gcovr.py --out <out>/<report> \
         <build>/<mode> <unit suite dirs...>          # spec + unit in one report

test_wamr.sh -C   (direct, without run_coverage.py)
 └── one report per suite (spec / unit / standalone / regression) under
     $COVERAGE_DIR, each covering every running mode of the invocation
```

### One build dir per running mode, and no copies

`test_wamr.sh` builds one iwasm per running mode into
`product-mini/platforms/<platform>/build/<mode>/` and re-points `build/iwasm` at
the mode it just built (a symlink, so every existing consumer — the malformed and
wamr_compiler suites, `tests/standalone/*/run.sh` — keeps working unchanged).
Nothing is deleted when the next mode is built, so the suite report at the end of
the invocation can cover all of them at once. The unit build dirs
(`<out>/_work/<report>/unittest-build-<mode>/`) are per running mode as well.

The one place where gcov data is *copied* is the standalone suite: its cases
build into their own `<case>/build` and wipe it for every running mode, which the
coverage tooling does not get to change, so `test_wamr.sh` saves each mode's
`.gcno/.gcda` under `$COVERAGE_DIR/standalone/<mode>/<case>/` before the next
mode gets there.

### Two collections of the same spec data

When `run_coverage.py` runs the spec suite, gcovr goes over that iwasm build dir
twice, on purpose:

1. **`test_wamr.sh -C`** writes its own spec report under `$COVERAGE_DIR` (which
   `run_coverage.py` points at `<out>/_work/<report>/`, so it does not litter the
   repository workspace). That report is `test_wamr.sh`'s own `-C` contract and
   it covers the accepted iwasm build only.
2. **`run_coverage.py`** collects the same build dir again, this time together
   with the selected unit suites — that is the report.

Pass 2 reads the data where it was produced (no copy): the spec half of a report
is `product-mini/platforms/<platform>/build/<mode>/`, which is still there when
the unit half is done.

## Reports, fingerprints and logs

One `run_coverage.py` invocation runs one `(running mode, spec options, feature
set)` combination and writes it to `<out>/<report>/`. Its **fingerprint**
(`fingerprint.txt`) is a digest of the knobs the invocation ran with: running
mode, spec switches, F (canonicalized — parsed, then sorted, so the spelling and
a redundant `=0` do not matter) and whether the unit half / `FULL_TEST` were on.
It is a *record* of the run, not a directory key: the report directory is simply
`<out>/<report>/`.

`run_coverage.py --help` prints the output layout. In short: the report
directory holds `index.html` / `*.html`, `coverage.json`, `summary.txt`,
`summary.json`, `fingerprint.txt`, `unit-selection.txt` and — only when a step
failed but still produced data — `failures.txt`; `<out>/_work/<report>/` keeps
the unit build dirs, the child-process logs (`spec-<mode>.log`,
`unit-configure-<mode>.log`, `unit-build.log`, `ctest-<suite>.log`,
`collect.log`) and `test_wamr.sh`'s own per-suite report (`_work/<report>/spec/`).

`cmake`, `ctest`, `test_wamr.sh` and `gcovr` are all extremely chatty, so
**their output never reaches the console** — it goes to those log files.

A failing step that still produced data does **not** withhold the report:
whatever ran has already written its `.gcda`, and a partial report is more useful
than none, so a failing unit suite does not stop the remaining suites (a failing
unit *build* or *configure* does skip what depends on it), the failures are
listed in the report's `failures.txt`, echoed at the end, and turned into a
non-zero exit status. `run_full.py` likewise keeps the matrix going and still
merges, then exits non-zero naming the affected reports.

A step that measured **nothing at all** is treated differently: a spec run that
leaves no `.gcno/.gcda`, or a collection that produces no summary, aborts the run
instead of writing an empty report and exiting 0 — an automation job must never
read "success" out of "measured nothing".

The console carries the orchestrator's own lines only: the report's mode/spec
command/feature set, the resolved paths, the unit selection with its curation
warnings, the per-suite test counts, and the line / function / branch summary
read back from `summary.json`.

Paths are printed in their **repository-relative** spelling with the absolute
path underneath when the two differ: inside the devcontainer the absolute path
is `/workspaces/...`, which does not exist on the host.

`--out` is resolved against the directory the command is invoked from (the entry
scripts resolve it before launching the inner runner, which runs with
`cwd=<repository root>`), and the resolved location is printed at startup.

`run_full.py` merges the reports it wrote into `<out>/_merged/`: one gcovr run
over their tracefiles (`--add-tracefile`), which is what a batch driver does at
the end of a batch — the caller does not spell out what to merge. The merge lists
the reports it covers in `merged-reports.txt`.

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
```

One invocation runs exactly one report: `--report` names it (`<out>/<report>/`),
`--mode/--spec/--feature` describe it, and there is no report list to pair them
with. Merging several reports is a batch step, not a caller option —
`run_full.py` merges everything it wrote into `<out>/_merged/`.

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

`-C` writes **one report per suite** — `spec/`, `unit/`, `standalone/`,
`regression/` — under `$COVERAGE_DIR`, which defaults to the run's `REPORT_DIR`,
i.e. the timestamped `tests/wamr-test-suites/workspace/report/<date>/`. Each
report covers *every running mode* the invocation tested, so a plain
`./test_wamr.sh -C` (all six modes) ends with all six modes' data in one spec
report rather than only the last mode's.

A driver can put those reports elsewhere with the `COVERAGE_DIR` environment
variable — `coverage/run_coverage.py` sets it to `<out>/_work/<report>/` so
`test_wamr.sh`'s own report does not litter the repository workspace. It is a
workaround for a proper `test_wamr.sh` option; see the TODO in the script.

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
