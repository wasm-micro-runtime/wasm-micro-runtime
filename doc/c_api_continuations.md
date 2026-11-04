# Experimental C API continuations

`WAMR_BUILD_C_API_CONTINUATIONS=1` enables the experimental numeric continuation
API in `core/iwasm/include/wasm_ext.h`. It is disabled by default. This prototype
requires the fast interpreter, software bounds checks, and a single-module build
without JIT. Guest GC and SIMD can be enabled. AOT modules can coexist and use
ordinary calls, including the stackless component adapter; creating a suspended
invocation for an AOT function is rejected. Each interpreter invocation uses the
instance stack budget supplied to `wasm_instance_new_with_args` (32 KiB for the
ordinary C API constructor), rather than an additional fixed 64 KiB budget.

Include the public C API header before the extension header. An invocation can
return pending from an explicitly registered host callback, accept a checked
completion, resume after that call, or cancel without replaying the callback.
Keep the store, instance, functions, and callback environment alive until the
invocation is deleted. Invocations and callbacks are confined to one thread;
callbacks must return normally and cannot reenter invocation APIs. Cancellation
only applies at a pending host boundary; it cannot preempt guest computation.

The identical [C fixture](../samples/shared-continuations/README.md) also runs
against TinyWasm. It checks ownership, completion validation, synchronous and
pending callbacks, cancellation, traps, indirect calls, and mixed-width values.
`tests/unit/c-api-continuations` runs that source through CTest in fast-interpreter
unit-test builds; the standalone sample runner also tests optimized native builds.
See the fixture's validation report for linked size, process memory, timings, and
Windows hardware-sampling results.

This is an execution primitive for an external adapter, not a component runtime
or WASI 0.3 implementation. Canonical ABI, resources, memory/table import parity,
caller-context access, nested realloc/post-return operations, task scheduling,
and the inherited vector binary ABI need separate work before supporting an
arbitrary component stack. The ordinary interpreter and native-import path have
no continuation state or hooks when this feature is disabled.

See [feature compatibility](../samples/shared-continuations/FEATURE_COMPAT.md)
for GC/SIMD/AOT tests and the unchanged embedded image comparison.

With `WAMR_BUILD_DUMP_CALL_STACK=1`, continuation traps retain the ordinary
C API guest trace, including traps after resumption. This is not a complete
mixed native/guest crash stack; see the [debugging audit](../samples/shared-continuations/debug/README.md).
