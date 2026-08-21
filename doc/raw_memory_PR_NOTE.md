# PR: Raw address mode (`WAMR_BUILD_RAW_MEMORY`)

## Sorry / not sorry

We're sorry for poking a hole in WAMR's beloved linear-memory isolation.

We're not sorry about what that hole makes possible when you **intentionally**
opt in: wasm as a **code container** where you call everything as local and
access everything as local — closely tied pieces in one VA — instead of a
nest of sandboxed tenants that each own (and often waste) their own address
space. Speedups are a nice side effect of dropping that theater.

Default builds are unchanged. Sandbox stays the default. Raw is build-gated,
per-instance, and documented as trusted-only.

Mutually exclusive with Shared Heap — not because we couldn't wire both, but
because it barely makes sense: Shared Heap exists to punch a shared window
*through* sandbox isolation. In Raw, guest ints are already host pointers in
one VA; the whole heap is shared. Stacking both is “shared, but also shared.”

## Why (the product)

Sandbox isolation is the right default for untrusted modules. For products
that already trust their wasm (same process, same team, same ship), the
pain is **architectural**, not “we need 2× fewer nanoseconds”:

- **Call everything as local** — guest `*` / `$` args *are* host pointers;
  host and sibling modules talk without offset↔native theater.
- **Access everything as local** — one process VA; allocators / arenas /
  shared buffers live where C would put them.
- **Code container, not tenant farm** — modules are compilation units /
  plugins inside one app. No private linear memory per sibling “just
  because wasm.”

Portable IR + load/JIT/AOT, without pretending siblings need mem fences.

## One VA, optional alloc chain

Address mode is **per instance**. There is only one real address space: the
**process VA**. Nested Raw does not invent nested VAs.

Raw sees host pointers. A sandboxed outer that loads a Raw inner does **not**
give the inner “outer’s linear memory as its universe,” and outer isolation
does **not** wrap Raw. Default Raw `module_malloc` is `os_*` (process heap)
unless the embedder says otherwise.

**Alloc hooks are embedder policy**, not automatic hierarchy. The loader
decides what the guest’s heap *is*:

- **Host / base alloc** — default `os_*` or your process allocator (full
  VA; Linux-process shape).
- **Parent borders** — forward into a sandboxed outer’s
  `module_malloc` / linear mem (cooperative “live in my blob”).
- **Separate arena** — hooks that only hand out pointers from some other
  region (mmap slab, pool, Metal arena, …) that lives wherever you put it.

```
process VA
├── A  outermost Raw  (Linux-process shape)
│     top(raw) ──os_*/mmap──► host heap          ← owns the pool
│       └── mid(raw)  hooks → top
│             └── leaf(raw)  hooks → mid
│                   malloc walks leaf → mid → top
│
├── B  outermost Sandbox  (cooperative “live in my blob”)
│     outer(sandbox) ──► outer linear memory     ← owns the pool
│       └── mid(raw)  hooks → outer.module_malloc
│             └── leaf(raw)  hooks → mid
│                   bytes land inside outer’s borders
│
├── C  separate arena  (loader-drawn box anywhere in VA)
│     arena / mmap slab                          ← owns the pool
│       └── child(raw)  hooks → arena only
│
└── ✗  Raw under sandbox, NO hooks
      outer(sandbox)  linear mem
      inner(raw) ──os_*──► process heap          ← escapes outer
```

A loader may chain levels (`leaf → mid → top`) so whoever actually backs
memory owns the pool and everyone below shares that VA. If the **top** is
Raw with `os_*` / mmap, the stack behaves like a normal Linux process —
wasm is just more code in one address space.

Hooking only steers **cooperative** heap traffic. It is not a security
boundary: forge a pointer and you are still on process VA.

Demo (prints the chain): unit test
`nest_alloc_chain_sandbox_vs_raw_outer` — outermost Raw vs outermost
sandbox vs unhooked escape. Details: [`doc/raw_memory.md`](raw_memory.md)
§ Nested loaders.

## Nice side effect: measured speedups

Speed is what you get when you stop paying taxes you did not want — not
the reason to ship Raw. Microbench
`tests/unit/raw-memory/raw_memory_bench` (Release, x86_64, 16 384 elems ×
300 rounds, order-swapped medians of 5).

| Engine | Config | Workload | Sandbox | Raw | Speedup |
|--------|--------|----------|---------|-----|---------|
| Fast-JIT | HW bounds | `host_churn` (wasm→host `*` arg) | 11.8 ns/el | 4.3 ns/el | **~2.7×** |
| Fast-JIT | soft bounds | `host_churn` | 11.5 ns/el | 4.4 ns/el | **~2.6×** |
| AOT | soft (`wamrc` ± `--enable-raw-memory`) | `host_churn` | 0.294 s | 0.214 s | **~1.4×** |
| Fast-interp | soft bounds | `host_churn` | 83.3 ns/el | 68.6 ns/el | ~1.2× |
| Fast-JIT | HW / soft | in-wasm `sum_i32` / `touch_i32` | ~2.5–3.2 ns/el | ~2.5–3.0 ns/el | ~1.0–1.09× |
| Fast-JIT | soft | `shared_pool` (2 mods, write+share+read) | 0.015 s | 0.014 s | ~1.1× |

Reading the table honestly:

- **Host pointer calls** (Fast-JIT) show the convert tax clearly once the
  rest of the path is already lean.
- **Fast-interp ~1.2×** is small because the baseline is fat: dispatch
  dominates, so removing convert is a modest fraction of a slow path
  (same absolute save looks bigger under Fast-JIT).
- **In-wasm loads under HW bounds** ≈ parity — isolation was already
  cheap there.
- **`shared_pool`** wall time on a tiny buffer is still the write/read
  loops; the real win is no private linear mem / no copy architecture.

```bash
cmake -S tests/unit/raw-memory -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DRAW_MEMORY_TEST_PROFILE=fast_jit
cmake --build build-bench --target raw_memory_bench
(cd build-bench && ./raw_memory_bench 300)
```

## What

| Piece | Behavior |
|-------|----------|
| Build | `-DWAMR_BUILD_RAW_MEMORY=1` (default `0`) |
| API | `wasm_runtime_set_address_mode(inst, WASM_ADDR_RAW)` + optional alloc hooks |
| Engines | Classic / fast interp, Fast-JIT (mode baked at compile), AOT via `wamrc --enable-raw-memory` |
| `memory.grow` | returns `-1` in Raw |
| Shared Heap | cmake-refused with Raw (redundant once the whole VA is shared) |
| Hosts | 32- and 64-bit (mem32 on 64-bit still needs pointers that fit in `i32`) |

Details: [`doc/raw_memory.md`](raw_memory.md).

## Trust model

Raw has **no** linear-memory isolation. One bad store can trash the process.
Only load modules compiled/understood for host pointers. A sandboxed parent
does **not** contain a Raw child. If you need tenants, keep sandbox (on
every instance that must stay a tenant).

## Test plan

```bash
# refuse
cmake ... -DWAMR_BUILD_RAW_MEMORY=1 -DWAMR_BUILD_SHARED_HEAP=1   # must FATAL

# product-mini
cmake ... -DWAMR_BUILD_RAW_MEMORY=0   # build
cmake ... -DWAMR_BUILD_RAW_MEMORY=1   # build (x86_64 and X86_32)

# unit matrix
tests/unit/raw-memory/run_matrix.sh
# incl. nest_alloc_chain_sandbox_vs_raw_outer (hook chain demo)
# fast_jit / fast_interp / memory64 — load/store, bulk, grow→-1, hooks, AOT
```

Verified locally before this PR: refuse Shared+Raw; PM RAW=0/1 + Fast-JIT
+ X86_32; matrix **13+8+11**; nest A/B/escape; Fast-JIT sandbox/raw smoke
×80; rebench soft/HW Fast-JIT + AOT + fast-interp (numbers above).
