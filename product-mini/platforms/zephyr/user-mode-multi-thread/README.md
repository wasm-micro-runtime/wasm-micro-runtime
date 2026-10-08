# Guest pthreads in Zephyr user mode

This sample runs WAMR in a Zephyr user thread. A C guest creates two pthread
workers, coordinates them with a mutex and condition variable, joins them,
and checks their combined result is `42`. General environment setup is in the
[platform README](../README.md); this document explains the embedding contract.

## Storage and permissions

Supervisor code in [src/main.c](src/main.c) owns the native objects:

```c
WAMR_ZEPHYR_THREAD_POOL_DEFINE(wamr_threads, 4, 4096);
WAMR_ZEPHYR_SYNC_POOL_DEFINE(wamr_sync, 16, 8);
K_THREAD_STACK_DEFINE(wamr_root_stack, 8192);
```

Thread structures, user stacks, mutexes, and conditions are registered Zephyr
kernel objects. Keep their storage outside libraries assigned to
`zephyr_library_app_memory()`. The WAMR library's mutable globals and global
heap belong in `wamr_partition`, not its native kernel objects.

The two kinds of permission are independent:

- A memory domain containing `wamr_partition` and `z_libc_partition` makes
  runtime and libc state accessible to user threads.
- Kernel-object grants let those threads use specific native objects through
  Zephyr syscalls. Merely making memory accessible does not grant object access.

Prepare the embedding in supervisor mode, before starting the WAMR root:

1. Create the root with `K_USER` and `K_FOREVER`, leaving it suspended.
2. Initialize its memory domain and add the root to it.
3. Call `wamr_zephyr_thread_pool_prepare(&wamr_threads, root)`.
4. Call `wamr_zephyr_sync_pool_prepare(&wamr_sync, root)`.
5. Start the root and eventually join it.

Check each return value, as the sample does. Preparation validates the native
storage and grants it to the exact root. First preparation requires one
serialized supervisor provisioner; do not call it simultaneously. Repeating
preparation is supported only with the identical descriptor and owner, not
as a way to replace a live pool.

WAMR creates child threads with `K_USER | K_INHERIT_PERMS`. They inherit the
root's memory domain and kernel-object permissions. No `k_object_alloc()` or
dynamic object support is required.

## Handles and capacities

`korp_tid` is an opaque WAMR identity, never a `k_tid_t`. User-mode
`korp_mutex` and `korp_cond` are also opaque handles, not native object
pointers. The platform resolves them to the already-prepared kernel objects.
Never cast these handles and pass them to Zephyr APIs.

The sample provisions four worker slots with 4096-byte stacks, 16 mutex
slots, and eight condition slots. The root has its own 8192-byte stack and
does not consume a worker slot. Runtime-internal synchronization also consumes
sync slots, so capacity must account for more than guest-visible primitives.

The guest pthread stack defaults and minimum are set to 4096 bytes to match
the provisioned worker stacks. Requests larger than the prepared stacks,
capacity exhaustion, malformed descriptors, and stale handles return errors
instead of allocating new Zephyr objects. Pool definitions also check against
the platform's compiled capacity limits.

## Lifecycles

A joinable worker follows the POSIX-like ownership model:

```text
FREE -> RESERVED -> RUNNING -> EXITED -> JOINED -> FREE
```

An exited thread retains its slot until joined. Detach transfers cleanup
ownership to the platform: an active detached slot is released only after
native thread cleanup completes; detaching an already-exited thread performs
that cleanup immediately. A fresh generation identifies each reuse, so a
stale handle cannot join or detach a replacement worker.

Mutexes and conditions have independent `FREE -> ACTIVE -> FREE` lifecycles.
Initialization reserves only the corresponding pool. Destroy requires no
in-flight operations; mutexes must also be unowned and conditions must have
no waiters. Busy destruction returns `BHT_ERROR`. Successful destruction clears
the caller's handle and invalidates that generation before reuse.

## Guest build and supported APIs

[wasm-app/main.c](wasm-app/main.c) uses `pthread_create`, `pthread_join`,
mutex operations, condition wait, signal, and broadcast. Named `os_sem_*`
and rwlocks are outside this sample's supported contract.

The build compiles the guest with wasi-sdk but uses WAMR's
`libc-builtin-sysroot` headers: its pthread ABI is not WASI libc's pthread ABI.
Shared linear memory and `-pthread` are required. The shared Zephyr library
build installs `test_wasm.wasm` and generates `generated/test_wasm.h` in the
build tree. No generated Wasm bytes are checked in; editing the C guest and
rebuilding regenerates them.

The root runs the interpreter with a global heap pool, loads and instantiates
the guest, invokes its exported main, and tears down the instance and runtime.
Guest success requires both workers to finish and the final counter to be `42`.

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

These two tests compile the actual guest with host pthreads, fail only the
first or second creation call, and check real joins and mutex/condition
destruction. They measure guest cleanup control flow, not Zephyr permissions
or platform primitives; the Twister sample exercises those on ARC separately.
