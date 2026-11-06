/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cmocka.h>

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

/* Shared state for the helper threads below. */
struct sync_ctx {
    korp_mutex m;
    korp_cond c;
    bool go;
    int count;
    void *value;
};

static void
ctx_init(struct sync_ctx *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    assert_int_equal(os_mutex_init(&ctx->m), BHT_OK);
    assert_int_equal(os_cond_init(&ctx->c), BHT_OK);
}

static void
ctx_destroy(struct sync_ctx *ctx)
{
    assert_int_equal(os_cond_destroy(&ctx->c), BHT_OK);
    assert_int_equal(os_mutex_destroy(&ctx->m), BHT_OK);
}

static uint64
now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* ---------------- mutex ---------------- */

static void
test_mutex_init_lock_unlock_destroy(void **state)
{
    korp_mutex m;

    (void)state;
    assert_int_equal(os_mutex_init(&m), BHT_OK);
    assert_int_equal(os_mutex_lock(&m), BHT_OK);
    assert_int_equal(os_mutex_unlock(&m), BHT_OK);
    assert_int_equal(os_mutex_destroy(&m), BHT_OK);
}

static void *
trylock_probe(void *arg)
{
    korp_mutex *m = arg;
    int ret = pthread_mutex_trylock(m);

    if (ret == 0)
        pthread_mutex_unlock(m);
    return (void *)(intptr_t)ret;
}

static void
test_mutex_excludes_other_thread(void **state)
{
    korp_mutex m;
    korp_tid tid;
    void *ret = NULL;

    (void)state;
    assert_int_equal(os_mutex_init(&m), BHT_OK);
    assert_int_equal(os_mutex_lock(&m), BHT_OK);
    /* The lock is held across create/join, so the probe must see EBUSY. */
    assert_int_equal(pthread_create(&tid, NULL, trylock_probe, &m), 0);
    assert_int_equal(pthread_join(tid, &ret), 0);
    assert_int_equal((int)(intptr_t)ret, EBUSY);
    assert_int_equal(os_mutex_unlock(&m), BHT_OK);
    assert_int_equal(os_mutex_destroy(&m), BHT_OK);
}

static void *
counter_worker(void *arg)
{
    struct sync_ctx *ctx = arg;
    int i;

    for (i = 0; i < 1000; i++) {
        os_mutex_lock(&ctx->m);
        ctx->count++;
        os_mutex_unlock(&ctx->m);
    }
    return NULL;
}

static void
test_mutex_contention_counter(void **state)
{
    struct sync_ctx ctx;
    korp_tid tids[4];
    int i;

    (void)state;
    ctx_init(&ctx);
    for (i = 0; i < 4; i++)
        assert_int_equal(
            os_thread_create(&tids[i], counter_worker, &ctx, 256 * 1024),
            BHT_OK);
    for (i = 0; i < 4; i++)
        assert_int_equal(os_thread_join(tids[i], NULL), BHT_OK);
    assert_int_equal(ctx.count, 4 * 1000);
    ctx_destroy(&ctx);
}

#if defined(__linux__)
static void
test_mutex_destroy_while_locked(void **state)
{
    korp_mutex m;

    (void)state;
    assert_int_equal(os_mutex_init(&m), BHT_OK);
    assert_int_equal(os_mutex_lock(&m), BHT_OK);
    /* glibc returns EBUSY for a locked mutex. */
    assert_int_equal(os_mutex_destroy(&m), BHT_ERROR);
    assert_int_equal(os_mutex_unlock(&m), BHT_OK);
    assert_int_equal(os_mutex_destroy(&m), BHT_OK);
}
#endif

static void
test_recursive_mutex_relock(void **state)
{
    korp_mutex m;

    (void)state;
    assert_int_equal(os_recursive_mutex_init(&m), BHT_OK);
    assert_int_equal(os_mutex_lock(&m), BHT_OK);
    assert_int_equal(os_mutex_lock(&m), BHT_OK);
    assert_int_equal(os_mutex_unlock(&m), BHT_OK);
    assert_int_equal(os_mutex_unlock(&m), BHT_OK);
    assert_int_equal(os_mutex_destroy(&m), BHT_OK);
}

/* ---------------- condition variable ---------------- */

static void
test_cond_init_destroy(void **state)
{
    korp_cond c;

    (void)state;
    assert_int_equal(os_cond_init(&c), BHT_OK);
    assert_int_equal(os_cond_destroy(&c), BHT_OK);
}

static void *
cond_waiter(void *arg)
{
    struct sync_ctx *ctx = arg;

    os_mutex_lock(&ctx->m);
    while (!ctx->go)
        os_cond_wait(&ctx->c, &ctx->m);
    ctx->count++;
    os_mutex_unlock(&ctx->m);
    return NULL;
}

static void
test_cond_wait_signal(void **state)
{
    struct sync_ctx ctx;
    korp_tid tid;

    (void)state;
    ctx_init(&ctx);
    assert_int_equal(os_thread_create(&tid, cond_waiter, &ctx, 256 * 1024),
                     BHT_OK);

    os_mutex_lock(&ctx.m);
    ctx.go = true;
    assert_int_equal(os_cond_signal(&ctx.c), BHT_OK);
    os_mutex_unlock(&ctx.m);

    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);
    assert_int_equal(ctx.count, 1);
    ctx_destroy(&ctx);
}

static void
test_cond_broadcast_wakes_all(void **state)
{
    struct sync_ctx ctx;
    korp_tid tids[3];
    int i;

    (void)state;
    ctx_init(&ctx);
    for (i = 0; i < 3; i++)
        assert_int_equal(
            os_thread_create(&tids[i], cond_waiter, &ctx, 256 * 1024), BHT_OK);

    os_mutex_lock(&ctx.m);
    ctx.go = true;
    assert_int_equal(os_cond_broadcast(&ctx.c), BHT_OK);
    os_mutex_unlock(&ctx.m);

    for (i = 0; i < 3; i++)
        assert_int_equal(os_thread_join(tids[i], NULL), BHT_OK);
    assert_int_equal(ctx.count, 3);
    ctx_destroy(&ctx);
}

/* Sets ctx->go after a short delay, so the timed wait really blocks. */
static void *
delayed_signaler(void *arg)
{
    struct sync_ctx *ctx = arg;
    struct timespec ts = { 0, 10 * 1000 * 1000 };

    nanosleep(&ts, NULL);
    os_mutex_lock(&ctx->m);
    ctx->go = true;
    os_cond_signal(&ctx->c);
    os_mutex_unlock(&ctx->m);
    return NULL;
}

/* Caller must hold ctx->m. Returns the last reltimedwait result. */
static int
wait_go_reltimed(struct sync_ctx *ctx, uint64 useconds)
{
    int ret = 0;

    while (!ctx->go) {
        ret = os_cond_reltimedwait(&ctx->c, &ctx->m, useconds);
        if (ret != 0)
            break;
    }
    return ret;
}

static void
run_reltimed_signaled(uint64 useconds)
{
    struct sync_ctx ctx;
    korp_tid tid;
    int ret;

    ctx_init(&ctx);
    assert_int_equal(os_thread_create(&tid, delayed_signaler, &ctx, 256 * 1024),
                     BHT_OK);

    os_mutex_lock(&ctx.m);
    ret = wait_go_reltimed(&ctx, useconds);
    os_mutex_unlock(&ctx.m);

    assert_int_equal(ret, 0);
    assert_true(ctx.go);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);
    ctx_destroy(&ctx);
}

static void
test_cond_reltimedwait_timeout(void **state)
{
    struct sync_ctx ctx;
    uint64 start;
    int ret;

    (void)state;
    ctx_init(&ctx);

    os_mutex_lock(&ctx.m);
    start = now_us();
    ret = os_cond_reltimedwait(&ctx.c, &ctx.m, 10000);
    assert_true(now_us() - start >= 9000);
    os_mutex_unlock(&ctx.m);

    assert_int_equal(ret, ETIMEDOUT);
    ctx_destroy(&ctx);
}

static void
test_cond_reltimedwait_signaled(void **state)
{
    (void)state;
    run_reltimed_signaled(5 * 1000 * 1000);
}

static void
test_cond_reltimedwait_forever(void **state)
{
    (void)state;
    run_reltimed_signaled(BHT_WAIT_FOREVER);
}

static void
test_cond_reltimedwait_sec_overflow(void **state)
{
    (void)state;
    /* Large enough that tv_sec saturates in msec_nsec_to_abstime(). */
    run_reltimed_signaled(UINT64_MAX - 1);
}

static void
test_cond_reltimedwait_nsec_carry(void **state)
{
    struct sync_ctx ctx;
    int ret;

    (void)state;
    ctx_init(&ctx);

    /* 999999 us: tv_nsec + usec % 1e6 * 1000 crosses 1e9 and carries. */
    os_mutex_lock(&ctx.m);
    ret = os_cond_reltimedwait(&ctx.c, &ctx.m, 999999);
    os_mutex_unlock(&ctx.m);

    assert_int_equal(ret, ETIMEDOUT);
    ctx_destroy(&ctx);
}

/* ---------------- semaphore ---------------- */

static char sem_name[64];

static void
sem_name_init(void)
{
    snprintf(sem_name, sizeof(sem_name), "/wamr_ut_sem_%d", (int)getpid());
}

static korp_sem *
sem_create(unsigned int val)
{
    korp_sem *sem;

    sem_name_init();
    sem_unlink(sem_name);
    sem = os_sem_open(sem_name, O_CREAT | O_EXCL, 0600, val);
    assert_true(sem != SEM_FAILED);
    return sem;
}

static void
sem_remove(korp_sem *sem)
{
    assert_int_equal(os_sem_close(sem), 0);
    assert_int_equal(os_sem_unlink(sem_name), 0);
}

static void
test_sem_open_close_unlink(void **state)
{
    korp_sem *sem;

    (void)state;
    sem = sem_create(1);
    sem_remove(sem);
}

static void
test_sem_wait_post_getvalue(void **state)
{
    korp_sem *sem;
    int val = -1;

    (void)state;
    sem = sem_create(1);

    assert_int_equal(os_sem_getvalue(sem, &val), 0);
    assert_int_equal(val, 1);
    assert_int_equal(os_sem_wait(sem), 0);
    assert_int_equal(os_sem_getvalue(sem, &val), 0);
    assert_int_equal(val, 0);
    assert_int_equal(os_sem_post(sem), 0);
    assert_int_equal(os_sem_getvalue(sem, &val), 0);
    assert_int_equal(val, 1);

    sem_remove(sem);
}

static void
test_sem_trywait(void **state)
{
    korp_sem *sem;

    (void)state;
    sem = sem_create(0);

    assert_int_equal(os_sem_trywait(sem), -1);
    assert_int_equal(errno, EAGAIN);
    assert_int_equal(os_sem_post(sem), 0);
    assert_int_equal(os_sem_trywait(sem), 0);

    sem_remove(sem);
}

struct sem_ctx {
    korp_sem *sem;
    int done;
};

static void *
sem_waiter(void *arg)
{
    struct sem_ctx *ctx = arg;

    os_sem_wait(ctx->sem);
    ctx->done = 1;
    return NULL;
}

static void
test_sem_wait_blocks_until_post(void **state)
{
    struct sem_ctx ctx;
    korp_tid tid;

    (void)state;
    ctx.sem = sem_create(0);
    ctx.done = 0;
    assert_int_equal(os_thread_create(&tid, sem_waiter, &ctx, 256 * 1024),
                     BHT_OK);

    /* The waiter cannot finish before the post, so this is deterministic. */
    assert_int_equal(ctx.done, 0);
    assert_int_equal(os_sem_post(ctx.sem), 0);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);
    assert_int_equal(ctx.done, 1);

    sem_remove(ctx.sem);
}

static void
test_sem_open_existing_excl(void **state)
{
    korp_sem *sem, *dup;

    (void)state;
    sem = sem_create(1);

    dup = os_sem_open(sem_name, O_CREAT | O_EXCL, 0600, 1);
    assert_true(dup == SEM_FAILED);
    assert_int_equal(errno, EEXIST);

    sem_remove(sem);
}

static void
test_sem_unlink_nonexistent(void **state)
{
    (void)state;
    sem_name_init();
    sem_unlink(sem_name);
    assert_int_equal(os_sem_unlink(sem_name), -1);
    assert_int_equal(errno, ENOENT);
}

/* ---------------- rwlock ---------------- */

struct rw_ctx {
    korp_rwlock *lock;
    int rd_ret;
    int wr_ret;
};

static void *
rw_probe(void *arg)
{
    struct rw_ctx *ctx = arg;

    ctx->rd_ret = pthread_rwlock_tryrdlock(ctx->lock);
    if (ctx->rd_ret == 0)
        pthread_rwlock_unlock(ctx->lock);
    ctx->wr_ret = pthread_rwlock_trywrlock(ctx->lock);
    if (ctx->wr_ret == 0)
        pthread_rwlock_unlock(ctx->lock);
    return NULL;
}

static void
test_rwlock_init_destroy(void **state)
{
    korp_rwlock lock;

    (void)state;
    assert_int_equal(os_rwlock_init(&lock), BHT_OK);
    assert_int_equal(os_rwlock_destroy(&lock), BHT_OK);
}

static void
test_rwlock_multiple_readers(void **state)
{
    korp_rwlock lock;
    struct rw_ctx ctx;
    korp_tid tid;

    (void)state;
    assert_int_equal(os_rwlock_init(&lock), BHT_OK);
    assert_int_equal(os_rwlock_rdlock(&lock), BHT_OK);

    ctx.lock = &lock;
    ctx.rd_ret = -1;
    ctx.wr_ret = -1;
    assert_int_equal(os_thread_create(&tid, rw_probe, &ctx, 256 * 1024),
                     BHT_OK);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);

    assert_int_equal(ctx.rd_ret, 0);
    assert_int_equal(ctx.wr_ret, EBUSY);
    assert_int_equal(os_rwlock_unlock(&lock), BHT_OK);
    assert_int_equal(os_rwlock_destroy(&lock), BHT_OK);
}

static void
test_rwlock_writer_excludes(void **state)
{
    korp_rwlock lock;
    struct rw_ctx ctx;
    korp_tid tid;

    (void)state;
    assert_int_equal(os_rwlock_init(&lock), BHT_OK);
    assert_int_equal(os_rwlock_wrlock(&lock), BHT_OK);

    ctx.lock = &lock;
    ctx.rd_ret = -1;
    ctx.wr_ret = -1;
    assert_int_equal(os_thread_create(&tid, rw_probe, &ctx, 256 * 1024),
                     BHT_OK);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);

    assert_int_equal(ctx.rd_ret, EBUSY);
    assert_int_equal(ctx.wr_ret, EBUSY);
    assert_int_equal(os_rwlock_unlock(&lock), BHT_OK);
    assert_int_equal(os_rwlock_destroy(&lock), BHT_OK);
}

static void
test_rwlock_unlock_roundtrip(void **state)
{
    korp_rwlock lock;

    (void)state;
    assert_int_equal(os_rwlock_init(&lock), BHT_OK);
    assert_int_equal(os_rwlock_rdlock(&lock), BHT_OK);
    assert_int_equal(os_rwlock_unlock(&lock), BHT_OK);
    assert_int_equal(os_rwlock_wrlock(&lock), BHT_OK);
    assert_int_equal(os_rwlock_unlock(&lock), BHT_OK);
    assert_int_equal(os_rwlock_destroy(&lock), BHT_OK);
}

/* ---------------- thread lifecycle ---------------- */

static void *
record_value_worker(void *arg)
{
    *(int *)arg += 1;
    return NULL;
}

static void
test_thread_create_join(void **state)
{
    int value = 41;
    korp_tid tid;

    (void)state;
    assert_int_equal(
        os_thread_create(&tid, record_value_worker, &value, 256 * 1024),
        BHT_OK);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);
    /* arg is passed through and the thread ran exactly once. */
    assert_int_equal(value, 42);
}

static void *
exit_with_value_worker(void *arg)
{
    os_thread_exit(arg);
    return NULL;
}

static void
test_thread_exit_retval(void **state)
{
    korp_tid tid;
    void *marker = &tid;
    void *ret = NULL;

    (void)state;
    assert_int_equal(
        os_thread_create(&tid, exit_with_value_worker, marker, 256 * 1024),
        BHT_OK);
    assert_int_equal(os_thread_join(tid, &ret), BHT_OK);
    assert_ptr_equal(ret, marker);
}

static void
test_thread_create_with_prio(void **state)
{
    int value = 0;
    korp_tid tid;

    (void)state;
    assert_int_equal(os_thread_create_with_prio(&tid, record_value_worker,
                                                &value, 256 * 1024,
                                                BH_THREAD_DEFAULT_PRIORITY),
                     BHT_OK);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);
    assert_int_equal(value, 1);
}

static void
test_thread_create_invalid_stack(void **state)
{
    korp_tid tid;

    (void)state;
    /* Smaller than PTHREAD_STACK_MIN: pthread_attr_setstacksize fails. */
    assert_int_equal(os_thread_create(&tid, record_value_worker, NULL, 1),
                     BHT_ERROR);
}

static void
test_thread_create_malloc_fail(void **state)
{
    korp_tid tid;

    (void)state;
    malloc_fail_next = 1;
    assert_int_equal(
        os_thread_create(&tid, record_value_worker, NULL, 256 * 1024),
        BHT_ERROR);
}

static void *
self_recorder(void *arg)
{
    *(korp_tid *)arg = os_self_thread();
    return NULL;
}

static void
test_self_thread_identity(void **state)
{
    korp_tid tid, seen = 0;

    (void)state;
    assert_true(pthread_equal(os_self_thread(), pthread_self()));

    assert_int_equal(os_thread_create(&tid, self_recorder, &seen, 256 * 1024),
                     BHT_OK);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);
    assert_true(pthread_equal(seen, tid));
    assert_false(pthread_equal(seen, pthread_self()));
}

static void *
detached_worker(void *arg)
{
    struct sync_ctx *ctx = arg;

    os_mutex_lock(&ctx->m);
    ctx->count = 1;
    os_cond_signal(&ctx->c);
    os_mutex_unlock(&ctx->m);
    return NULL;
}

static void
test_thread_detach(void **state)
{
    struct sync_ctx ctx;
    korp_tid tid;

    (void)state;
    ctx_init(&ctx);
    assert_int_equal(os_thread_create(&tid, detached_worker, &ctx, 256 * 1024),
                     BHT_OK);
    assert_int_equal(os_thread_detach(tid), BHT_OK);

    os_mutex_lock(&ctx.m);
    while (ctx.count == 0)
        os_cond_wait(&ctx.c, &ctx.m);
    os_mutex_unlock(&ctx.m);
    assert_int_equal(ctx.count, 1);
    ctx_destroy(&ctx);
}

static void
test_thread_join_null_retval(void **state)
{
    int value = 0;
    korp_tid tid;

    (void)state;
    assert_int_equal(
        os_thread_create(&tid, record_value_worker, &value, 256 * 1024),
        BHT_OK);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);
    assert_int_equal(value, 1);
}

static void
test_thread_create_join_many(void **state)
{
    int i, value;
    korp_tid tid;

    (void)state;
    for (i = 0; i < 64; i++) {
        value = 0;
        assert_int_equal(
            os_thread_create(&tid, record_value_worker, &value, 64 * 1024),
            BHT_OK);
        assert_int_equal(os_thread_join(tid, NULL), BHT_OK);
        assert_int_equal(value, 1);
    }
}

/* ---------------- stack boundary ---------------- */

static void
test_stack_boundary_non_null(void **state)
{
    uint8 *boundary;
    uint8 local;

    (void)state;
    boundary = os_thread_get_stack_boundary();
    assert_non_null(boundary);
    assert_true(boundary < &local);
}

static void
test_stack_boundary_cached(void **state)
{
    (void)state;
    assert_ptr_equal(os_thread_get_stack_boundary(),
                     os_thread_get_stack_boundary());
}

struct stack_probe {
    uint8 *boundary;
    size_t depth; /* distance from the boundary to a local in the thread */
};

/* Runs in a helper thread: record only, assert in the main thread. */
static void *
stack_boundary_worker(void *arg)
{
    struct stack_probe *probe = arg;
    uint8 local;

    probe->boundary = os_thread_get_stack_boundary();
    probe->depth = (size_t)(&local - probe->boundary);
    return NULL;
}

static void
test_stack_boundary_in_created_thread(void **state)
{
    struct stack_probe probe = { NULL, 0 };
    uint8 *main_boundary;
    korp_tid tid;

    (void)state;
    main_boundary = os_thread_get_stack_boundary();
    assert_int_equal(
        os_thread_create(&tid, stack_boundary_worker, &probe, 256 * 1024),
        BHT_OK);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);

    assert_non_null(probe.boundary);
    /* The local lies above the boundary, within the 256 KiB stack. */
    assert_true(probe.depth > 0);
    assert_true(probe.depth < 256 * 1024 + 4096);
    /* Each thread caches its own boundary, so they must differ. */
    assert_ptr_not_equal(probe.boundary, main_boundary);
}

/* ---------------- JIT write protect ---------------- */

static void
test_jit_write_protect_smoke(void **state)
{
    (void)state;
    os_thread_jit_write_protect_np(true);
    os_thread_jit_write_protect_np(false);
}
