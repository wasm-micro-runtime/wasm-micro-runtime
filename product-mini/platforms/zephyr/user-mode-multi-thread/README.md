# Guest pthreads in Zephyr user mode

This sample runs WAMR in a Zephyr user thread: two guest pthread workers use a
mutex and condition variable, then join with a combined result of `42`. See the
[platform README](../README.md) for environment setup; the embedding contract follows.

## Storage and permissions

Supervisor code in [src/main.c](src/main.c) owns the native objects:

```c
WAMR_ZEPHYR_THREAD_POOL_DEFINE(wamr_threads, 4, 4096);
WAMR_ZEPHYR_SYNC_POOL_DEFINE(wamr_sync, 16, 8);
K_THREAD_STACK_DEFINE(wamr_root_stack, 8192);
```

Thread structures, user stacks, mutexes, and conditions are registered Zephyr
kernel objects: keep them outside libraries assigned to
`zephyr_library_app_memory()`. Only WAMR's mutable globals and global heap
belong in `wamr_partition`. Two independent permissions are needed:

- A memory domain containing `wamr_partition` and `z_libc_partition` gives
  access to runtime and libc state.
- Kernel-object grants authorize Zephyr syscalls on specific native objects;
  memory access alone does not grant this permission.

Prepare the embedding in supervisor mode, before starting the WAMR root:

1. Create the root with `K_USER` and `K_FOREVER`, leaving it suspended.
2. Initialize its memory domain and add the root to it.
3. Call `wamr_zephyr_thread_pool_prepare(&wamr_threads, root)`.
4. Call `wamr_zephyr_sync_pool_prepare(&wamr_sync, root)`.
5. Start the root and eventually join it.

Check every return value. Preparation validates native storage and grants it to
the exact root. Use one serialized supervisor provisioner for initial setup;
repeated preparation is idempotent only for the identical descriptor and owner,
never a pool replacement/rebind operation.

WAMR workers require application-provided thread pools in both kernel and user
modes, with no malloc/internal-stack fallback. Applications without WAMR workers
need no thread pool; synchronization setup remains separate. Here, workers use
`K_USER | K_INHERIT_PERMS`, inheriting the root's memory domain and object
permissions without `k_object_alloc()` or dynamic object support.

## Handles and capacities

`korp_tid` is an opaque WAMR identity, never a Zephyr `k_tid_t`; user-mode
`korp_mutex` and `korp_cond` are opaque handles to prepared objects, not native
pointers. Never cast them or pass them to Zephyr APIs.

The declarations provide four 4096-byte worker stacks, 16 mutex slots and eight
condition slots, plus a management mutex and separate 8192-byte root stack.
Runtime-internal synchronization also consumes slots. The root's lifetime is
application-owned; its WAMR identity consumes no slot and is neither joinable
nor detachable (see [worker resources](../README.md#worker-resources)).

Guest pthread stack defaults/minimum are 4096 bytes. Oversized requests,
exhaustion, malformed descriptors and stale handles return errors, not new
native objects. Definitions check compiled limits;
`BH_ZEPHYR_THREAD_POOL_MAX_THREADS` defaults to four and must agree between
application and runtime. Per-generation bookkeeping still uses WAMR's heap;
allocation failure returns an error without consuming a slot.

## Lifecycles

A joinable worker retains its slot until joined:

```text
FREE -> RESERVED -> RUNNING -> EXITED -> JOINED -> FREE
```

Detach transfers cleanup to the platform: active detached slots are released
only after native thread cleanup; detaching an exited thread cleans it up
immediately. Each reuse gets a fresh generation, preventing stale handles from
joining or detaching replacement workers.

Successful quiescent runtime destruction retains provisioning: the same live
root can initialize/destroy repeatedly without preparation. Root and native
storage must outlive the binding; there is no release/rebind API or guarantee
that destruction with live workers is safe.

Alternatively, a supervisor root may prepare resources, initialize WAMR, then
call `k_thread_user_mode_enter()` before creating any WAMR workers, including
through initialization features. Workers use the caller's current privilege;
mixed live supervisor/user workers are unsupported. This sample uses the
suspended `K_USER` root workflow above.

Mutexes and conditions independently follow `FREE -> ACTIVE -> FREE`, reserving
only their corresponding pool. Destroy requires no in-flight operations,
unowned mutexes and no condition waiters; busy destruction returns `BHT_ERROR`.
Success clears the caller's handle and invalidates its generation before reuse.

## Guest build and supported APIs

[wasm-app/main.c](wasm-app/main.c) uses pthread creation/join, mutexes and
condition wait/signal/broadcast. Named `os_sem_*` and rwlocks are unsupported.

The wasi-sdk build uses WAMR's `libc-builtin-sysroot` headers, not WASI libc's
pthread ABI, with shared linear memory and `-pthread`. The runtime is configured
through `CONFIG_WAMR_*`; the [guest helper](../cmake/wasm_app.cmake) installs
`test_wasm.wasm` and generates `generated/test_wasm.h` in the build tree.
The app-owned guest bytes, heap and result are explicitly placed in
`wamr_partition`. No generated bytes are checked in; editing the guest C and
rebuilding regenerates them. The interpreter uses a global heap pool.

## Build and run

From `product-mini/platforms/zephyr`:

```sh
python3 build_and_run.py --sim qemu_arc user-mode-multi-thread
```

Use `--no-docker` in a configured Zephyr workspace with wasi-sdk installed.
The scenario runs on `qemu_arc/qemu_arc_hs`; Twister expects:

```text
PASS: two guest pthread workers completed in Zephyr user mode
```

For the guest's creation-failure cleanup regression on a Linux host with a
GNU-compatible linker, run from the repository root:

```sh
cmake -S product-mini/platforms/zephyr/user-mode-multi-thread/tests -B build/guest-cleanup
cmake --build build/guest-cleanup
ctest --test-dir build/guest-cleanup --output-on-failure
```

These two tests compile the actual guest with host pthreads, fail only the first
or second creation call, and check real joins and mutex/condition destruction.
They cover guest cleanup control flow, not Zephyr permissions or platform
primitives; the ARC Twister sample exercises those separately.
