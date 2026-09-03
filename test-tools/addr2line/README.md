# `addr2line.py` — symbolicating WAMR call stacks

`addr2line.py` turns a WAMR call-stack dump (`#00: 0x0040 - $f0` lines) back
into source-level information (function names, files, line numbers, inline
chains). It is a thin orchestrator over four LLVM/wabt tools, with workarounds
for two LLVM symbolization issues on wasm targets (one a design choice, one an
invariant violation from `wasm-opt -g`).

The two `samples/debug-tools*/` samples are end-to-end demos of the workflow.
This README focuses on the tool itself: how it works, why each subprocess is
necessary, and why the interval-table overlay is required.

## Quick reference

```bash
python3 addr2line.py \
    --wasi-sdk /opt/wasi-sdk \
    --wabt    /opt/wabt \
    --wasm-file /path/to/app.debug.wasm \
    call_stack.txt
```

| Flag | Default | Purpose |
|------|---------|---------|
| `--mode={interp,aot,fast-interp}` | `interp` | How to interpret runtime offsets — see *Three offset spaces* below. |
| `--no-addr` | off | Resolve by function name only (for very old iwasm dumps that lack offsets, or for fast-interp). |

## What the script does, step by step

1. **Parse each captured frame** (`#NN: 0xADDR - SYMBOL`).
2. **Convert the runtime offset to a DWARF address.** WAMR reports
   file-absolute byte offsets; DWARF addresses are relative to the start of
   the WASM Code section, so the script subtracts `code_section_start` (read
   from `wasm-objdump -h`).
3. **Apply a mode-dependent adjustment.** See *Three offset spaces* below.
4. **Resolve the address.** `llvm-symbolizer -f -i` for line info and inline
   chain, with the outermost frame's function name overlaid from a startup-
   built interval table (see "Why the interval-table overlay exists" below).
5. **Demangle and render.** C++ symbols pass through `llvm-cxxfilt`; inline
   frames are annotated `(inlined into <next>)` so the chain reads top-down.

## Three offset spaces (`--mode`)

WAMR captures `frame_ip` differently in each execution mode, so the offset in
a captured frame means different things:

| Mode | Offset space | Adjustment | What addr2line does |
|------|--------------|-----------|----------------------|
| `interp` (default) | File-absolute, *post*-advance — interpreter has already incremented `frame_ip` past the trapping opcode (and past LEB reads inside the handler) before the trap fires. | `offset - code_start - 1` | Full symbolication: file, line, column, inline chain. |
| `aot` | File-absolute, *instruction-start* — `wamrc` emits a `commit_ip` before every WASM operation. | `offset - code_start` | Full symbolication. |
| `fast-interp` | Function-relative, into the *transformed* in-memory bytecode (opcodes replaced with handler indices, LEBs expanded to fixed widths). The mapping back to source is destroyed by the transform. | (none) | Function-name lookup only — equivalent to `--no-addr`. One frame per input; no inline expansion (the `DW_TAG_inlined_subroutine` overlay is address-keyed and unavailable here). |

Picking the wrong `--mode` lands the resolver inside an adjacent function and
silently produces wrong file/line.

## Why the interval-table overlay exists

`llvm-symbolizer -e <wasm> -f -i <addr>` is the natural way to map an address
to a function + source location, with `-i` expanding inline frames. It's
correct for line/column info and for inline frame names — but its
**outermost frame's function name** goes through two paths that can both
produce wrong answers on wasm:

1. **Symbol-table override on the outermost frame.** `symbolizeInlinedCode`
   unconditionally rewrites the outermost frame's `FunctionName` from
   `getNameFromSymbolTable` whenever `FunctionNameKind::LinkageName` and
   `UseSymbolTable` are both set — which is the default. Inline frames are
   provably NOT rewritten (the override targets only
   `getMutableFrame(getNumberOfFrames() - 1)`). On some wasi-sdk / wasm
   layouts the symbol table's lookup returns the wrong function for an
   address that DWARF places correctly. This is an intentional LLVM design
   choice, not a bug — it exists because `-gline-tables-only` builds have
   only line tables in DWARF and the symbol table gives better linkage
   names in that case. The tradeoff bites on full-DWARF wasm builds.
2. **`AddrDieMap` invariant violation from `wasm-opt` DCE ghosts.**
   `wasm-opt -Oz -g` leaves DIEs for dead-code-eliminated compiler-rt /
   libm helpers behind with `low_pc = 0` and small `high_pc` (typically
   `[0, 0)` or `[0, 6)`, sometimes up to `[0, 0x80)`). This is a
   consequence of binaryen's DWARF preservation model: its address-
   rewrite pass in `src/wasm/wasm-debug.cpp`'s
   `LocationUpdater::getNewFuncStart` returns `0` as a tombstone when
   an old address doesn't map to any surviving IR node, and `updateDIE`
   writes that `0` into the `DW_AT_low_pc` — but leaves the
   `DW_TAG_subprogram` in place, and sometimes leaves the original
   `DW_AT_high_pc`. Binaryen's README calls this out: DWARF support is
   optional, tracks address relocation rather than DIE-tree
   restructuring, and is "not suitable for a fully optimized release
   build." LLVM's `updateAddressDieMap` in `DWARFUnit.cpp` assumes
   children are inserted after parents and each child range ⊆
   parent range; the ghost DIEs violate that invariant and the map's
   `upper_bound − 1` lookup returns an insertion-order-dependent DIE.

Empirical truth table (probing an inlined address of `trigger_oob` in a
small test module across all four toolchain combinations):

| wasi-sdk | wasm-opt -Oz -g | symbolizer outer | mechanism |
|---|---|---|---|
| 29 (clang 21) | no  | `free` ❌    | Symbol-table override |
| 29 (clang 21) | yes | `free` ❌    | Symbol-table override |
| 33 (clang 22) | no  | `app_main` ✅ | Neither mechanism triggers |
| 33 (clang 22) | yes | `fmaf` ❌    | AddrDieMap invariant violated by a `[low_pc=0, 0x72)` `fmaf` ghost |

The overlay fixes all four cases. Inner (inlined) frames are not affected
because they come from a different LLVM code path
(`getInliningInfoForAddress`), which walks `getParent()` links from the
leaf DIE and does NOT go through the symbol-table override applied to
the outermost concrete frame.

### How the overlay works

At startup, addr2line.py runs one `llvm-dwarfdump --debug-info` pass and
builds an in-memory list `[(low_pc, high_pc, name), ...]` covering every
real callable `DW_TAG_subprogram`. Three skip guards keep the list clean:

- `DW_AT_declaration=true` → forward declarations (e.g. WASI imports like
  `__imported_wasi_snapshot_preview1_*`) have no code, so they're excluded.
- `low_pc == 0` → structurally impossible for real code, since the wasm
  binary spec guarantees the Code section begins after at least a Type +
  Function section header. Any `low_pc = 0` DIE is a wasm-opt DCE ghost
  or a broken producer's output.
- `high_pc <= low_pc` → empty or degenerate range. Defensive; never
  observed on wasi-sdk 29 or 33.

For each captured call-stack address, `lookup_subprogram_name` returns
the innermost (smallest range) surviving DW_TAG_subprogram covering the
address. That result overwrites the outermost frame's function name in
the symbolizer output; inner frame names are left alone.

## Tools used by addr2line.py

| Tool | From | Why this tool, and not another |
|------|------|--------------------------------|
| `llvm-symbolizer` | wasi-sdk | The actual address → `(function, file:line:column)` resolver, with inline expansion via `-i`. Ships in wasi-sdk 29+ (the project's minimum supported version). |
| `llvm-dwarfdump` | wasi-sdk | Single `--debug-info` pass at startup builds a `(low_pc, high_pc, name)` interval table for every real callable `DW_TAG_subprogram`. Used to overlay the outermost frame's function name in every resolution. The overlay is necessary because `llvm-symbolizer -f` unconditionally rewrites that frame's `FunctionName` from an object-file symbol-table lookup (LLVM design, not a bug) and — separately — `wasm-opt -Oz -g` output can violate the `AddrDieMap` invariant so DWARF's own DIE lookup returns the wrong entry. Reading DWARF ranges directly bypasses both mechanisms. No equivalent CLI exposes this lookup as structured output, so we parse the text. |
| `wasm-objdump` | wabt | Reports the Code-section start offset (needed to convert file-absolute runtime offsets to DWARF addresses) and the function-index → name table (a last-resort fallback for declaration-only DW_TAG_subprograms like `__main_void`). |
| `llvm-cxxfilt` | wasi-sdk | C++ symbol demangling, applied as a final pass on resolved names. |

The script also detects an emscripten-style sourceMappingURL section and
falls back to `emsymbolizer` for that case.

## Behavior matrix

| Input | Resolver path | Output |
|-------|---------------|--------|
| `--mode=interp` or `--mode=aot`, address in DWARF range | Unified | `llvm-symbolizer -f -i` for line info + inline chain; outermost frame's function name overlaid from a startup-built interval table (innermost match wins) |
| `--mode=fast-interp` | Function-name | Single frame per input, no inline expansion, no source mapping |
| `--no-addr` | Function-name | Resolves the *function* — file:line refers to its declaration, not the trap site |
| Offset = 0 | Function-name fallback (with `(offset=0 — function entry)` note) | Used when WAMR couldn't capture `frame_ip` (e.g., trap at function entry, or top frame of an `iwasm -f` invocation) |
| Address outside any DW_TAG_subprogram | wasm-objdump function-index table | Single name; useful for declaration-only entries |
| Source has emscripten `sourceMappingURL` custom section | `emsymbolizer` source-map path | Single frame per address; no inline expansion (source maps don't carry it) |

## Tests

Run the pytest suite under `test-tools/addr2line/tests/`. It builds tiny
purpose-built C/C++ apps, captures their trap sites, and asserts the resolver
behaves correctly under a number of scenarios (single trap, always_inline,
deep inline chain, cross-TU LTO inlining, mid-function trap, multi-frame
call stack, C++ demangling, AOT mode, fast-interp mode, `--no-addr`,
`offset=0`, empty input). Multi-SDK runs (`pytest --multi-sdk`) exercise
the overlay across multiple toolchain versions and require multiple wasi-sdk
installations under `/opt`.

## See also

- `test-tools/addr2line/addr2line.py` — the script
- `test-tools/addr2line/tests/` — pytest harness
- `samples/debug-tools/` — minimal end-to-end sample
- `samples/debug-tools-optimized/` — production-realistic workflow (LTO, wasm-opt, strip, AOT, debug companion)
