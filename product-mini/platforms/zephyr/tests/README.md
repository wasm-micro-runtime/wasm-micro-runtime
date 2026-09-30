# WAMR Zephyr tests

These Ztest applications exercise WAMR's Zephyr integration. They complement
the [samples](../README.md#samples) and Zephyr's own kernel/verifier tests.
Runtime scenarios use the interpreter and `WAMR_BUILD_GLOBAL_HEAP_POOL`.

## Test roots

| Directory | WAMR contracts exercised |
| --- | --- |
| `platform-api/` | Pool allocation and alignment, time, thread creation/join/detach, synchronization, and unsupported named semaphores. Deterministic lifecycle tests cover exhaustion, stale handles, cleanup ownership, busy destruction, and recovery. |
| `runtime/` | Initialization, loading, instantiation, execution, malformed modules, small-pool failures, missing exports, and repeated workflows. |
| `usermode-faults/` | Required memory partitions, supervisor-only runtime state, recovery after an expected user fault, and Wasm out-of-bounds traps. Each fatal case has its own Twister scenario. |
| `common/` | Shared Wasm fixtures and runtime workflow helpers; not a standalone Twister application. |
| `test_build_and_run.py` | Host-side regression tests for the wrapper's command construction, root selection, artifact paths, and error handling. |

Each runnable root has a `testcase.yaml`. Twister records scenario and
individual Ztest verdicts in JSON and XML reports.

## Kernel and user contexts

The platform API kernel scenario runs on `native_sim` and
`qemu_arc/qemu_arc_hs`. Its userspace scenario runs on QEMU ARC with
`CONFIG_USERSPACE=y`, `CONFIG_WAMR_TEST_USER_MODE=y`, and
`CONFIG_DYNAMIC_OBJECTS=n`. `WAMR_CONTEXT_TEST()` and
`WAMR_CONTEXT_TEST_F()` compile the same body as `ZTEST`/`ZTEST_F` or
`ZTEST_USER`/`ZTEST_USER_F`, respectively.

Supervisor-controlled cases provision or inspect objects before starting user
threads:

- `platform_thread_pool.test_prepare_validates_and_preserves_pool` checks
  thread/stack descriptors and registered-object provisioning.
- `platform_thread_pool.test_self_thread_rejects_preinit_and_unmapped_callers`
  checks that a raw thread cannot obtain a WAMR identity.
- `platform_sync_pool.test_prepare_validates_and_preserves_sync_pool` checks
  native storage, capacities, overlapping ranges, and initialization.
- `platform_sync_pool.test_concurrent_prepare_is_idempotent_and_rejects_replacement`
  checks retries after preparation has completed.
- `platform_sync.test_prepared_native_sync_objects_are_user_accessible` starts
  a suspended user probe with inherited object permissions.
- `platform_sync.test_condvar_wait_requires_a_grant` withholds a condition
  grant and expects the offending user thread to fault.

Pool preparation is a supervisor API for one serialized provisioner and a
suspended WAMR user root. Simultaneous first preparation is unsupported;
later calls are idempotent only for the identical descriptor and owner.

Internal metadata uses a `sys_mutex` in userspace because WAMR library globals
reside in application memory. Public `os_mutex_*` and `os_cond_*` values are
opaque handles backed by application-provided, registered `k_mutex` and
`k_condvar` pools. Kernel-mode conditions also use native `k_condvar`;
semaphore-backed waiter nodes are unnecessary. Named `os_sem_*` remain
unsupported.

The runtime userspace suite keeps one user worker for repeated workflows.
The fault suite owns its fatal handler and isolates five WAMR-specific
boundaries. Generic MPU, syscall-verifier, and illegal-pointer matrices remain
Zephyr's responsibility.

### Intentional skips

- Native simulation filters out userspace scenarios because its architecture
  does not implement Zephyr userspace.
- Opaque synchronization-slot tests run only with `CONFIG_USERSPACE`; native
  object-access and missing-grant probes require the userspace scenario.
- The user-context preparation rejection case skips in kernel scenarios.
- Native simulation's runtime-statistics counter does not advance during the
  busy-work CPU-time case. QEMU ARC userspace CPU-time cases skip because
  Zephyr 3.7's statistics API reaches privileged `arch_irq_lock()`.

These skips retain their bodies and explanatory comments. Thread lifecycle
and positive synchronization cases are enabled in both applicable contexts.

## Running and inspecting results

From `product-mini/platforms/zephyr`, use the Docker-backed wrapper:

```sh
python3 build_and_run.py --sim native_sim tests/platform-api
python3 build_and_run.py --sim qemu_arc tests/platform-api
python3 build_and_run.py --sim native_sim tests/runtime
python3 build_and_run.py --sim qemu_arc tests/runtime
python3 build_and_run.py --sim qemu_arc tests/usermode-faults
```

Add `--no-docker` inside an already configured Zephyr workspace. General
environment setup and CI invocation are in the [platform README](../README.md).
Run wrapper regression tests from the repository root with:

```sh
python3 -m unittest product-mini/platforms/zephyr/tests/test_build_and_run.py
```

Ordinary reports live under `build/twister-<root>-<sim>/`: `twister.json`
contains scenario and case results, XML reports support CI consumers, and
`<platform>/<scenario>/handler.log` contains target output. The streamed log
is `build/logs/<root>-<sim>.log`. Root path separators become hyphens, so
`tests/platform-api` uses `tests-platform-api` in artifact names.

The wrapper forwards Twister's exit status. Inspect both scenario results and
individual case statuses rather than treating console text as the verdict.

## Informational coverage

Coverage is measurement only, with no percentage pass threshold. This test
series collects coverage on `native_sim`; QEMU ARC supplies kernel and
userspace behavioral evidence. The normal CI smoke lane measures the complete
native matrix. For a focused platform API run:

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
