# debug-tools-optimized — Debugging Production-Optimized WASM

This sample demonstrates symbolication of crashes in **production-optimized** WASM
binaries using the merged `addr2line.py`. The wasm apps are compiled with
`-Oz -g -flto` and post-processed with `wasm-opt -Oz -g`, then stripped to produce
minimal production binaries. A "debug companion" binary built in parallel retains
DWARF inline info, enabling source-level call stack recovery offline.

## Why this exists alongside `samples/debug-tools/`

The existing `samples/debug-tools/` sample uses `-O0 -g`: each function is preserved,
no inlining happens. This sample uses `-Oz -g -flto`, which:

- Aggressively inlines functions across translation units (cross-TU inlining via LTO)
- Strips the production binary to minimum size
- Tests `addr2line.py`'s inline expansion (`DW_TAG_inlined_subroutine` resolution)

If you only need to debug development builds, `samples/debug-tools/` is sufficient.
If you need to debug **shipped** binaries, this sample shows how.

## Build pipeline

```
clang -Oz -g -flto source1.c source2.c → <name>.wasm        (intermediate)
    └─ wasm-opt -Oz -g → <name>.debug.wasm                  (companion: code + DWARF + names)
        └─ llvm-strip --strip-all → <name>.prod.wasm        (production: code only)
```

## Why production is derived from the debug companion

`wasm-opt -Oz` (without `-g`) and `wasm-opt -Oz -g` produce **structurally different
binaries**: `-g` inhibits some inlining passes to preserve DWARF integrity. If we ran
them as separate pipelines, the production binary's code offsets would not match the
companion's DWARF address space — offline decode would silently break.

Instead, we run `wasm-opt -Oz -g` once and derive production by stripping the
companion (`llvm-strip --strip-all`). Custom sections (DWARF, names) live *after* the
code section in the WASM binary format, so stripping them doesn't shift code offsets.
This **guarantees byte-identical code** between production and companion.

## Why `-flto`

Without LTO, functions in separate `.c` files remain separate WASM functions even
under `-Oz`. With LTO, the compiler sees all sources as one unit and inlines
aggressively across files. This is what makes the `do_bad_access → trigger_oob →
app_main` chain collapse into a single WASM function with multiple inlined
subroutines.

## Why `recurse()` is non-tail-recursive

`stackoverflow_recurse.c` uses
`int r = recurse(depth + 1); return r + buf[0];` instead of
`return recurse(depth + 1);`. Tail calls (`return f(...)`) get converted to loops at
`-Oz -flto`, eliminating the recursion that we want to test. The non-tail form forces
a real `call` instruction so each iteration pushes a new frame.

## Prerequisites

- wasi-sdk at `WASI_SDK_PATH` or `/opt/wasi-sdk`
- binaryen at `BINARYEN_PATH` or `/opt/binaryen`
- wabt at `WABT_PATH` or `/opt/wabt`
- Python 3

## Quick start

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
cd ..

# wasm (interpreter)
./symbolicate.sh oob
./symbolicate.sh stackoverflow

# aot
./symbolicate.sh oob aot
./symbolicate.sh stackoverflow aot
```

To build with the fast interpreter instead of the classic interpreter:

```bash
mkdir -p build && cd build
cmake .. -DUSE_FAST_INTERP=ON && make -j$(nproc)
```

The same `symbolicate.sh` invocations work for both build modes — it auto-detects
which interpreter the iwasm binary uses (by inspecting it for the `wasm_interp_fast.c`
symbol) and passes the right `--mode` to `addr2line.py`.

### `verify.sh` — assertion-based smoke test

`verify.sh <app> <wasm|aot>` runs `symbolicate.sh` and asserts the
symbolicated output matches the expected shape: for the OOB sample,
all three inline frames (`do_bad_access (inlined into trigger_oob)`,
`trigger_oob (inlined into app_main)`, `app_main`) plus their source
files; for the stackoverflow sample, `recurse` and `app_main` resolved
across the recursive chain. It auto-detects classic vs fast-interp
builds and relaxes the OOB assertion for fast-interp + wasm (where the
runtime offset doesn't map to source, so addr2line.py only emits the
outermost function name). Used by CI; useful locally to check a build:

```bash
./verify.sh oob wasm        # PASS or fails with the captured output
./verify.sh stackoverflow aot
```

The build pipeline produces three artifacts per app:

| Artifact | Purpose |
|----------|---------|
| `<name>.debug.wasm` | Debug companion — DWARF + name section, used for symbolication |
| `<name>.prod.wasm` | Production wasm — fully stripped, runs in interpreter mode |
| `<name>.prod.aot` | Production AOT — wamrc-compiled with `--enable-dump-call-stack` |

Both `.prod.wasm` and `.prod.aot` use the same `.debug.wasm` companion for offline
symbolication. AOT and classic-interp produce nearly identical call stack offsets
(within 1-2 bytes), and `addr2line.py` resolves both correctly.

## Build-side tools (and why each is needed)

This sample chains four compile-time and post-build tools, each filling a
specific gap; dropping any of them would break the inline-DWARF round trip
the sample is meant to demonstrate:

| Tool | From | What it does here | Why we need it |
|------|------|-------------------|----------------|
| `clang -Oz -g -flto` | wasi-sdk | Compiles `.c` → `.wasm` with size-optimized code, DWARF debug info, and link-time optimization | `-flto` enables cross-translation-unit inlining. Without LTO, `oob_main.c` and `oob_access.c` would stay as separate WASM functions; with LTO they collapse into a single function with `DW_TAG_inlined_subroutine` entries — exactly what we need to test inline-aware symbolication. |
| `wasm-opt -Oz -g` | binaryen | Post-optimization pass on the wasm; preserves DWARF integrity | Further size shrink (LEB compaction, dead-code elimination) on top of clang. The `-g` flag inhibits transformations that would corrupt DWARF, producing a "debug companion" with the same code layout as production. |
| `llvm-strip --strip-all` | wasi-sdk | Strips DWARF and name sections from the companion → production binary | Custom sections (DWARF, name) live AFTER the code section in the wasm format, so stripping them doesn't shift code offsets. This guarantees the production binary's runtime offsets map 1:1 to the companion's DWARF — without this property, offline decode would silently break. |
| `wamrc --enable-dump-call-stack --bounds-checks=1` | wamr-compiler | Compiles `.wasm` → `.aot` for ahead-of-time execution | AOT is the realistic embedded deployment target. `--bounds-checks=1` forces software memory bounds checks instead of hardware traps, so OOB exceptions go through the runtime exception path (which captures `frame_ip`) instead of a SIGSEGV that bypasses ip capture — without this, the OOB sample's AOT path would crash with no captured call stack. |

For the **decode-side** tools (`llvm-symbolizer`, `llvm-dwarfdump`,
`wasm-objdump`, `llvm-cxxfilt`) and the interval-table overlay that
addr2line.py applies to correct the LLVM symbolizer's outermost
function-name lookup on wasm, see
[`test-tools/addr2line/README.md`](../../test-tools/addr2line/README.md).

## Three execution modes, three offset spaces

`addr2line.py` supports a `--mode={interp,aot,fast-interp}` flag because each
mode reports offsets differently:

| Mode | Offset space | Adjustment | Source resolution |
|------|--------------|-----------|-------------------|
| `interp` (default) | File-absolute, post-advance | `offset - code_start - 1` | Full file:line, inline expansion |
| `aot` | File-absolute, instruction-start | `offset - code_start` | Full file:line, inline expansion |
| `fast-interp` | Function-relative, transformed bytecode | (not mappable) | Function-name only |

**Why fast-interp can't show source lines**: at load time, fast-interp rewrites the
WASM bytecode in memory (replaces opcodes with handler indices, expands LEBs to
fixed widths, etc.). The runtime ip then points into this *transformed* buffer, not
the original WASM bytes — so there's no way to map the offset back to a source line.
Function-name lookup still works via `wasm-objdump` + `llvm-dwarfdump --name=...`.

`symbolicate.sh` handles all three modes transparently. For manual invocation:

```bash
# Classic interp (default)
python3 ../../test-tools/addr2line/addr2line.py --wasm-file ... callstack.txt

# AOT
python3 ../../test-tools/addr2line/addr2line.py --mode=aot --wasm-file ... callstack.txt

# Fast interp
python3 ../../test-tools/addr2line/addr2line.py --mode=fast-interp --wasm-file ... callstack.txt
```

## Why `iwasm -f app_main` (and not just `iwasm <wasm>`)

The `symbolicate.sh` script invokes `iwasm -f app_main` instead of letting iwasm run
the default wasi `_start` entry. This matters for two reasons:

1. **Compiler folding**: Under `-Oz -flto`, when control reaches the OOB write through
   `_start → __wasi_main_void → main → app_main → ...`, LLVM observes the entire chain
   leading to undefined behavior and may rewrite it as `unreachable`. Calling
   `app_main` directly preserves the explicit OOB instruction and produces the
   expected `out of bounds memory access` exception.

2. **Cleaner trap point**: With `-f app_main`, the WAMR call stack starts at our app's
   entry, not deep inside wasi-libc startup, making the symbolication output more
   focused on user code.

## Expected output

### oob app

```
=== Running iwasm on oob.prod.wasm (expect crash) ===

#00: 0x1dd8 - app_main

Exception: out of bounds memory access

=== Captured call stack ===
#00: 0x1dd8 - app_main

=== Symbolicated call stack (using debug companion) ===
0: do_bad_access (inlined into trigger_oob)
        at .../wasm-apps/oob_access.c:11:15
   trigger_oob (inlined into app_main)
        at .../wasm-apps/oob_main.c:17:5
   app_main
        at .../wasm-apps/oob_main.c:23:5
```

Although `do_bad_access` and `trigger_oob` were both inlined into `app_main`
under `-Oz -flto` (the runtime sees only a single WASM function), the debug
companion's DWARF retains `DW_TAG_inlined_subroutine` entries that describe
the inline chain. `addr2line.py` walks them and reports the trap site at all
three source levels.

This works because the iwasm in this sample is built with
`WAMR_DISABLE_HW_BOUND_CHECK=1`: OOB memory access goes through the
interpreter's exception path (which captures `frame_ip` via
`SYNC_ALL_TO_FRAME`), not through a SIGSEGV signal handler that
longjmps out without updating ip. The AOT build uses `--bounds-checks=1`
for the same reason.

### stackoverflow app

```
=== Running iwasm on stackoverflow.prod.wasm (expect crash) ===

#00: 0x1e15 - $f12
#01: 0x1e15 - $f12
   ... (~20 identical frames as the wasm operand stack overflows) ...
#21: 0x1e15 - $f12
#22: 0x1dd0 - app_main

Exception: wasm operand stack overflow

=== Captured call stack ===
... same as above ...

=== Symbolicated call stack (using debug companion) ===
0: recurse
        at .../wasm-apps/stackoverflow_recurse.c:20:13
   ... (one resolved frame per captured frame) ...
22: app_main
        at .../wasm-apps/stackoverflow_main.c:14:5
```

Stack overflow produces non-zero offsets at every frame (the runtime
captures the ip of the call instruction in each caller), so `addr2line.py`
resolves source file, line number, and function name correctly across the
recursive chain. `llvm-symbolizer`'s outermost frame's function name is
unreliable on wasm for two independent reasons: LLVM unconditionally
overrides that frame's name from an object-file symbol-table lookup
(which can return a wrong function on some wasm layouts), and
`wasm-opt -Oz -g` leaves dead-code-eliminated `DW_TAG_subprogram` DIEs
behind with `low_pc = 0` which then violate DWARF's DIE-range map
invariants. `addr2line.py` always applies an interval-table overlay
to the outermost frame's name, so it renders as `recurse` / `app_main`
in both cases.

## Manual decode

If you have a captured stack from another iwasm run (e.g., from a remote board or
saved log), you can symbolicate it directly:

```bash
python3 ../../test-tools/addr2line/addr2line.py \
    --wasi-sdk /opt/wasi-sdk \
    --wabt /opt/wabt \
    --wasm-file build/wasm-apps/oob.debug.wasm \
    /path/to/saved/call_stack.txt
```

## Environment variables

| Variable | Default | Used by |
|----------|---------|---------|
| `WASI_SDK_PATH` | `/opt/wasi-sdk` | Build (clang, llvm-strip) and decode (llvm-symbolizer, llvm-dwarfdump) |
| `BINARYEN_PATH` | `/opt/binaryen` | Build (wasm-opt) |
| `WABT_PATH` | `/opt/wabt` | Decode (wasm-objdump) |

## References

- [addr2line.py](../../test-tools/addr2line/addr2line.py)
- [WAMR Dump Call Stack Feature](../../doc/build_wamr.md#dump-call-stack-feature)
- [Zephyr coredump-debug sample](../../product-mini/platforms/zephyr/coredump-debug/) — same workflow on embedded
- [debug-tools sample](../debug-tools/) — non-optimized debug build for comparison
