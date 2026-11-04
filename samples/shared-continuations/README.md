# Shared continuation C fixture

The same `fixture.c`, Wasm inputs, and `wasm_ext.h` declarations are used for TinyWasm and WAMR.
Read [READINESS.md](READINESS.md) for the primitive's remaining cancellation and
store-concurrency limitations. The later stackless HTTP adapter has separate
WASIp3 subset receipts. Neither establishes full component concurrency.

The extension is experimental, numeric-only, thread-confined, and limited to one result. It is
not an implementation of the component model or the WASI 0.3 scheduler. Only explicit pending
host calls suspend; cancellation does not preempt executing guest code or a blocking callback.
Callbacks must return normally and must not reenter invocation APIs. Stores, instances, and
callback environments must outlive their invocations. C callers supply valid live handles and
initialized input buffers; guest-controlled counts/types are checked before completion is committed.

Run from a compiler developer shell (Python 3 and the runtime's native toolchain are required):

```sh
python path/to/run.py --runtime tinywasm --root /path/to/tinywasm --build /tmp/tiny-test --feature on
python path/to/run.py --runtime wamr --root /path/to/wamr --build /tmp/wamr-test --feature on
```

`--feature off` builds the ordinary C API benchmark without extension references. It uses release
LTO, linker dead stripping, and hidden visibility on Unix; MSVC uses `/GL`, `/LTCG`, `/OPT:REF`,
and `/OPT:ICF`. Use the same compiler/linker versions, flags, source, fixture, and executable name
when comparing an unmodified baseline with feature-off and feature-on-but-unused builds. The
TinyWasm Rust static library uses fat LTO and one codegen unit; final C link optimization does not
imply C/Rust cross-language inlining. Match panic strategy and allocator as well.

The tests cover pending/resume/cancel/delete, invalid count/type completions, immediate completion, malformed callback results, host traps, no callback replay,
store invocation exclusion, repeated suspensions, indirect calls, guest traps, and mixed-width
numeric parameters/results. Each benchmark checks deterministic results and reports pure-Wasm
and native-import cost separately. Feature-on tests additionally measure native imports through
the opt-in driver. Peak process RSS is reported; Windows also reports peak committed bytes.
RSS is a noisy hosted process high-water mark, not a measurement of MCU SRAM or live heap.

For WAMR, this prototype requires a single-module fast interpreter, software bounds checks,
and no JIT. Guest GC/SIMD and ordinary AOT modules can coexist with this API.
AOT functions cannot be suspended. A pending interpreter call uses the existing
instance stack budget (32 KiB by default), configurable through WAMR's existing
`wasm_instance_new_with_args` API.
Unsupported build combinations fail configuration. Numeric packing uses memcpy so mixed i32/i64
sequences need not be naturally aligned. Async metadata is store-owned, independent of copied
C function handles. TinyWasm reserves its store while the Rust execution borrow is live and
releases that borrow before accessing the completed result buffer.

The main fixture's standard wasm.h vectors are source-portable, not a promise of binary-plugin
compatibility: WAMR's public vector has additional fields. The fixture prints layout sizes.
A common binary plugin needs a normalized base ABI or an external adapter that translates it;
objects must never be passed between runtime implementations. The continuation header remains
a review prototype; its inherited wasm.h value/vector ABI must be settled before freezing it.

`wide.wasm` is generated from `wide.wat`; `wide.h` contains those same bytes for the C test.
Rebuild with WABT's `wat2wasm`; the checked-in binaries let consumers run without WABT.
See [VALIDATION.md](VALIDATION.md) for measured results and limits, with raw timing/RSS and
section-size/hash samples. `measure.py --fixture fixture.wasm baseline=/path/to/baseline
disabled=/path/to/disabled enabled=/path/to/enabled` alternates seven rounds and emits CSV;
add `--cpu 2` on Windows/Linux for affinity. Keep the machine otherwise idle.

## Component-parser experiment

Reviewed TinyWasm's `exp/component-parser` at `2a942e64f26d3bb5727b0b64d05afb5d7a15c4eb`.
Its feature-gated `Component`/`ComponentDeclaration` metadata and core-module extraction provide
input for an external linker; they do not execute components or supply a scheduler. This fixture
therefore uses ordinary core modules and does not cherry-pick that branch's parser/type changes.
The experimental parser can evolve independently of execution handles and the default core build.
Imported-memory/table support and complete component/async ABI conformance remain separate gates.

## Instrumented validation

`--sanitize` enables runtime/client ASan (nightly Rust with rust-src for TinyWasm
on supported Unix targets). `--client-asan` on Windows TinyWasm is explicitly
C-client-only coverage; native Rust remains uninstrumented. Both modes require a
working ASan canary and run the shared fixtures plus TinyWasm's standard C API tests.
`--ubsan` additionally enables Unix WAMR UBSan; baseline alignment warnings remain
a separate limitation. Use a fresh build directory for each mode.

`edges.wasm` and `edges.h` contain the same module generated from `edges.wat`.
The additional edge fixture does not change the measured `fixture.c` benchmark.

## Feature compatibility

`--gc --simd` adds guest GC and SIMD suspension tests with an unchanged numeric
C boundary. They cover live references and vectors in parent and leaf frames,
allocation pressure, repeated suspension, indirect host calls, cancellation and
post-resume traps. WAMR also checks 4 KiB and 64 KiB instance stack budgets.
`--aot` builds the WAMR AOT backend alongside the interpreter. Add
`--aot-compiler /path/to/wamrc` to execute compiled versions and check that the
suspension API rejects them. The runner emits software memory and stack checks
to match the runtime. WAMR fast-interpreter SIMD is unavailable on Windows.

Regenerate the portable fixtures with `wasm-tools parse gc.wat -o gc.wasm`
and `wasm-tools parse simd.wat -o simd.wasm`.
See [FEATURE_COMPAT.md](FEATURE_COMPAT.md) for results and remaining boundaries.
