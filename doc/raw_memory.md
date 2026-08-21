# Raw address mode

Raw mode turns off linear-memory isolation for an instance: guest integers
are **host pointers**, and wasm becomes a **code container** in one process
VA. Call into host / sibling modules with real pointers; load and store as
if everything were local. That is the point. Any speedup is a side effect
of not paying sandbox taxes.

Opt-in at build time and per instance. Default WAMR builds stay
sandbox-only.

## Build

```bash
cmake ... -DWAMR_BUILD_RAW_MEMORY=1
```

Default is `0`: no Raw specialization on hot paths.

Forbidden with `WAMR_BUILD_SHARED_HEAP=1`. Works on 32- and 64-bit hosts.

## Modes

| Mode | Guest address | Load / store | `module_malloc` |
|------|---------------|--------------|-----------------|
| `WASM_ADDR_SANDBOX` (default) | mem32 `i32` / mem64 `i64` offset | `base + offset` | linear / app heap offset |
| `WASM_ADDR_RAW` | host `uintptr_t` bits | dereference as-is | host heap hooks → real pointer |

Raw is a **platform ABI**, not a wasm `memtype`.

## Embedder API

```c
wasm_runtime_set_address_mode(inst, WASM_ADDR_RAW);
wasm_runtime_set_raw_alloc_hooks(inst, &hooks); /* optional */
```

Call **before** the instance runs (and before Fast-JIT compiles). Setting
Raw turns bounds checks off for that instance.

### Alloc hooks

```c
typedef struct WASMRawAllocHooks {
    void *(*malloc_func)(void *env, size_t size);
    void (*free_func)(void *env, void *ptr);
    void *(*realloc_func)(void *env, void *ptr, size_t size);
    void *(*calloc_func)(void *env, size_t nmemb, size_t size);
    void *env;
} WASMRawAllocHooks;
```

Unset hooks → `os_malloc` / `os_free` / `os_realloc` / zero-filled
`os_malloc`. Metal may replace them. WAMR does not expose mmap; the
allocator owner can use mmap internally.

`memory.grow` returns `-1` in Raw mode.

## Nested loaders (sandbox outer, Raw inner)

Address mode is **per instance**. There is no nested subdivision of
address spaces.

Example: sandboxed outer (e.g. MicroPython-as-wasm) with a host/native
loader that instantiates an inner module.

| Outer | Inner | What the inner “sees” |
|-------|-------|------------------------|
| Sandbox | Sandbox | Its **own** private linear memory. Outer’s buffer is invisible unless you copy, Shared Heap, or pass converted host pointers through imports. |
| Sandbox | **Raw** | The **process VA** (or whatever host pointers the loader / hooks hand it). **Not** “outer’s linear memory, remapped as the whole world.” Outer’s sandbox is just one host buffer among many. |
| Raw | Raw | One shared VA. Pointers are local; call and access like siblings in one app. |

So: putting Raw under an isolated outer does **not** confine Raw to the
outer’s mem. Outer isolation does not wrap the inner. If the loader only
ever passes pointers into outer’s `memory_data`, Raw *can* poke that blob
as plain host memory — but Raw can also `malloc` process heap, forge
pointers, or touch anything else in the process. Treat Raw inners as
**process-trusted**, even when the parent module is sandboxed.

**Alloc chain is embedder policy.** WAMR does not forward child allocs to
a parent. A loader may set `wasm_runtime_set_raw_alloc_hooks` so each
level’s `module_malloc` calls the parent’s (sandbox linear or Raw/os).
Demo: unit test `nest_alloc_chain_sandbox_vs_raw_outer` (outermost Raw
vs outermost sandbox vs unhooked escape).

## Engines

| Engine | Specialization |
|--------|----------------|
| Classic / fast interp | Mode read once at call entry; resolve absolute `maddr` |
| Fast-JIT | Mode baked from `module->address_mode` at compile time |
| AOT | Compile with `wamrc --enable-raw-memory`; set instance to Raw |

## Address width

Raw does not invent a new pointer size: the guest type still comes from
the module’s memory (`i32` for mem32, `i64` for mem64). So:

- **mem32** — guest addresses are 32-bit. On a 32-bit host that matches
  `uintptr_t`. On a 64-bit host the pointer value must still fit in
  `i32` (typical: allocate in the low 4 GiB, e.g. `MAP_32BIT`).
- **mem64** — guest addresses are 64-bit; full host pointers on a 64-bit
  host. Use classic/fast interp or AOT (`wamrc --enable-raw-memory`).
  Fast-JIT does not support Memory64 (upstream WAMR limit, unrelated to
  Raw).

## Testing

```bash
# Default profile: classic interp + Fast-JIT (+ AOT fixture via wamrc)
cmake -S tests/unit/raw-memory -B build-raw -DRAW_MEMORY_TEST_PROFILE=fast_jit
cmake --build build-raw && (cd build-raw && ./raw_memory_test)

# All engine profiles (fast_jit, fast_interp, memory64)
tests/unit/raw-memory/run_matrix.sh
```

Profiles cover bulk `memory.fill` / `memory.copy`, `memory.grow` → `-1`,
default `os_*` hooks, public `wasm_runtime_module_realloc`, and AOT
(`wamrc --enable-raw-memory`) when LLVM is available.

### Microbench (sandbox vs Raw)

Optional. Useful to show where sandbox taxes show up; not the product
pitch.

```bash
cmake -S tests/unit/raw-memory -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DRAW_MEMORY_TEST_PROFILE=fast_jit
cmake --build build-bench --target raw_memory_bench
(cd build-bench && ./raw_memory_bench 300)
```

Workloads: in-wasm `sum_i32` / `touch_i32`, `host_churn` (wasm→host `*`
arg), and `shared_pool` (two instances, pointer share vs memcpy).

## Trust model

Raw has **no** linear-memory isolation. Only load trusted modules compiled
for host pointers. A sandboxed parent does **not** contain a Raw child.
