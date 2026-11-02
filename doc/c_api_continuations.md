# Experimental C API continuations

`WAMR_BUILD_C_API_CONTINUATIONS=1` enables the experimental numeric continuation
API in `core/iwasm/include/wasm_ext.h`. It is disabled by default. This prototype
requires the fast interpreter, software bounds checks, and a single-module build
without AOT, JIT, or GC. Each invocation owns a 64 KiB interpreter stack.

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
