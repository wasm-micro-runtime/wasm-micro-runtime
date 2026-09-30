# WAMR Zephyr tests

These Ztest applications exercise WAMR's Zephyr integration. They complement
the [samples](../README.md#samples) and Zephyr's own kernel/verifier tests.
Runtime scenarios use the interpreter and `WAMR_BUILD_GLOBAL_HEAP_POOL`.

## Test roots

| Directory | WAMR contracts exercised |
| --- | --- |
| `platform-api/` | Pool allocation and alignment, time, thread creation/join/detach, synchronization, and unsupported named semaphores. Deterministic lifecycle tests cover exhaustion, stale handles, cleanup ownership, busy destruction, and recovery. |
| `thread-pool/` | Fresh-image kernel/user contracts: no-worker operation without preparation, caller-supplied native storage/capacity, real metadata allocation failure and recovery, retained provisioning across restarts, and actual supervisor initialization followed by user-mode entry. |
| `runtime/` | Initialization, loading, instantiation, execution, malformed modules, small-pool failures, missing exports, and repeated workflows. |
| `usermode-faults/` | Required memory partitions, supervisor-only runtime state, recovery after an expected user fault, and Wasm out-of-bounds traps. Each fatal case has its own Twister scenario. |
| `common/` | Shared Wasm fixtures and runtime workflow helpers; not a standalone Twister application. |
| `test_build_and_run.py` | Host-side regression tests for the wrapper's command construction, root selection, artifact paths, and error handling. |

Each runnable root declares scenarios in `testcase.yaml`; Twister reports
scenario and individual Ztest verdicts in JSON and XML.

## Kernel and user contexts

The platform API runs kernel scenarios on `native_sim` and
`qemu_arc/qemu_arc_hs`, and userspace on ARC with `CONFIG_USERSPACE=y`,
`CONFIG_WAMR_TEST_USER_MODE=y`, and `CONFIG_DYNAMIC_OBJECTS=n`.
`WAMR_CONTEXT_TEST()`/`WAMR_CONTEXT_TEST_F()` select `ZTEST`/`ZTEST_F`
or `ZTEST_USER`/`ZTEST_USER_F`, respectively.

All roots build WAMR through the Zephyr module. Userspace runtime globals use
`ztest_mem_partition` for `platform-api/` and `wamr_partition` for the other roots.

Supervisor-only cases provision or inspect objects before user execution:

| Case | Supervisor role |
| --- | --- |
| `platform_thread_pool.test_prepare_validates_and_preserves_pool` | Validate thread/stack descriptors and registered-object provisioning. |
| `platform_thread_pool.test_self_thread_rejects_preinit_and_unmapped_callers` | Verify a raw thread cannot obtain a WAMR identity. |
| `platform_sync_pool.test_prepare_validates_and_preserves_sync_pool` | Validate native storage, capacities, overlapping ranges, and initialization. |
| `platform_sync_pool.test_concurrent_prepare_is_idempotent_and_rejects_replacement` | Check retries after preparation completes. |
| `platform_sync.test_prepared_native_sync_objects_are_user_accessible` | Start a suspended user probe with inherited object permissions. |
| `platform_sync.test_condvar_wait_requires_a_grant` | Withhold a condition grant and expect the user thread to fault. |

Pool preparation requires one serialized supervisor provisioner and a genuine
created root, running or suspended. Concurrent first preparation is unsupported;
retries are idempotent only for the identical descriptor and owner. The platform
API fixture binds to the persistent Ztest runner; recreated case threads inherit
its grants. Reusing a case-thread address does not make its owner continuously
live. Sequential cases share one WAMR heap and runtime setup/teardown fixture.

The separate `platform_api.userspace.mutex_only` fresh-image scenario uses two
mutex slots and no conditions. It checks pointer/count consistency, clean
condition-allocation rejection, mutex exhaustion, user-mode locking, and
capacity recovery without changing the shared fixture's pool.

The `thread-pool/` root uses one ordered case per fresh image to preserve initial
unprepared state, with two 2048-byte worker slots against the default maximum of
four. Its ARC user root stays alive during controller preparation; a separate
scenario calls `k_thread_user_mode_enter()` after supervisor initialization,
before any WAMR worker exists. Both report through shared memory for controller
assertions. Pressure tests exhaust the real 128 KiB WAMR heap, reject worker
creation, free pressure allocations, and prove full slot recovery without mocks
or a production reset seam. The kernel case rejects joining/detaching the
application-owned root and confirms no-worker teardown still works. Oversized
worker requests and recovery are covered in `platform-api/` instead.

Internal metadata uses `sys_mutex` in userspace because WAMR library globals
reside in application memory. Public `os_mutex_*`/`os_cond_*` opaque handles
use application-provided, registered `k_mutex`/`k_condvar` pools with
`CONFIG_USERSPACE=y`, even for supervisor callers. `CONFIG_USERSPACE=n` uses
embedded native objects, including native `k_condvar`, not semaphore-backed
waiter nodes. Named `os_sem_*` remain unsupported.

Runtime userspace workflows reuse one user worker. The fault suite owns its
fatal handler and isolates five WAMR-specific boundaries; generic MPU,
syscall-verifier, and illegal-pointer matrices remain Zephyr's responsibility.

### Intentional skips

- Native simulation lacks Zephyr userspace and filters out those scenarios.
- Opaque synchronization-slot tests require `CONFIG_USERSPACE`; object-access,
  missing-grant, and user-context preparation rejection probes require the
  userspace scenario.
- Native simulation's statistics counter does not advance during busy-work
  CPU-time tests. ARC userspace CPU-time tests skip because Zephyr 3.7's
  statistics API reaches privileged `arch_irq_lock()`.

These skips retain their bodies and explanatory comments. Thread lifecycle
and positive synchronization cases are enabled in both applicable contexts.

## Running and inspecting results

From `product-mini/platforms/zephyr`, use the Docker-backed wrapper:

```sh
python3 build_and_run.py --sim native_sim --sim qemu_arc tests/platform-api
python3 build_and_run.py --sim native_sim --sim qemu_arc tests/thread-pool
python3 build_and_run.py --sim native_sim --sim qemu_arc tests/runtime
python3 build_and_run.py --sim qemu_arc tests/usermode-faults
```

Add `--no-docker` inside an already configured Zephyr workspace. General
environment setup and CI invocation are in the [platform README](../README.md).
Run wrapper regression tests from the repository root with:

```sh
python3 -m unittest product-mini/platforms/zephyr/tests/test_build_and_run.py
```

Ordinary reports live under `build/twister-<root>-<sims>/`: `twister.json`
contains scenario and case results, XML reports support CI consumers, and
`<platform>/<scenario>/handler.log` contains target output. The streamed log is
`build/logs/<root>-<sims>.log`. Root separators become hyphens and simulators
are hyphen-joined (e.g. `tests-platform-api-native_sim-qemu_arc`). The wrapper
forwards Twister's exit status; inspect scenario and case statuses, not console
text alone.

## Informational coverage

Coverage has no percentage pass threshold. `native_sim` provides measurement;
QEMU ARC supplies kernel/userspace behavioral evidence. CI measures the complete
native matrix; for a focused platform API run:

```sh
python3 build_and_run.py --coverage --sim native_sim tests/platform-api
```

Artifacts are separate from ordinary runs, under
`build/twister-tests-platform-api-native_sim-coverage/`:

- `coverage/index.html`: browsable report;
- `coverage/coverage.xml`: Cobertura XML;
- `coverage.json`: raw gcovr trace data;
- `twister.json`: test verdicts.

Zephyr uses compiler gcov data; Twister processes it with gcovr. Filter the raw
report to WAMR's Zephyr platform sources when comparing platform coverage:

```sh
docker run --rm \
  -v "$PWD:/root/zephyrproject/modules/wasm-micro-runtime" \
  -w /root/zephyrproject/modules/wasm-micro-runtime wamr-zephyr \
  gcovr -r /root/zephyrproject/modules/wasm-micro-runtime \
  --filter 'core/shared/platform/zephyr/' \
  --add-tracefile product-mini/platforms/zephyr/build/twister-tests-platform-api-native_sim-coverage/coverage.json \
  --txt-metric line --txt -
```

Run that filtering command from the repository root; use `--txt-metric branch`
for branches. Focused and full-matrix percentages compile different source
surfaces and cannot be compared directly. Native coverage does not measure
userspace permissions or MPU isolation. AOT, alternate allocators, exhaustive
platform API coverage, and physical-board testing remain outside this suite.
