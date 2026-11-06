/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cmocka.h>

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

/*
 * Signal environment rules that shape these tests:
 *
 * - os_thread_create() runs os_thread_signal_init() at thread start, so every
 *   new thread is already initialized, with the creator's handler.
 * - Each test initializes the signal environment in the main thread with the
 *   handler it wants, runs the body there, then destroys it. Tests that need
 *   a fresh thread (inheritance, stack overflow) spawn one from the body.
 * - os_thread_signal_init() saves the currently installed SIGSEGV action as
 *   the "previous" handler. A second init in another thread therefore saves
 *   signal_callback itself, and an unhandled fault would recurse. Tests never
 *   rely on that path; see the report for the source-level issue.
 * - Helper threads never call cmocka asserts (they longjmp on failure). They
 *   record results in globals and the main thread asserts after join.
 */

#define PREV_NONE 0
#define PREV_SIGINFO 1
#define PREV_PLAIN 2

static korp_jmpbuf jb;
static int handler_called;
static int prev_called;
static void *last_sig_addr;

/* Results written by the bodies. */
static int r_inited_start;
static int r_inited_after_destroy;
static int r_inited_after_init;
static int r_init_again_ret;
static int r_child_inited;
static int r_recovered;
static void *r_page;
static uint8 *r_boundary;
static size_t r_guard_bytes;

static void
reset_results(void)
{
    handler_called = 0;
    prev_called = 0;
    last_sig_addr = NULL;
    r_inited_start = 0;
    r_inited_after_destroy = 0;
    r_inited_after_init = 0;
    r_init_again_ret = -1;
    r_child_inited = 0;
    r_recovered = 0;
    r_page = NULL;
    r_boundary = NULL;
    r_guard_bytes = 0;
}

/* WAMR handler: record and longjmp back to the faulting body. */
static void
test_handler_longjmp(void *sig_addr)
{
    handler_called++;
    last_sig_addr = sig_addr;
    os_longjmp(jb, 1);
}

/* WAMR handler that returns, so the previous handler is forwarded to. */
static void
test_handler_return(void *sig_addr)
{
    handler_called++;
    last_sig_addr = sig_addr;
}

/* Previous handler, SA_SIGINFO form: record and longjmp. */
static void
prev_sigaction(int sig, siginfo_t *info, void *uc)
{
    (void)sig;
    (void)uc;
    prev_called++;
    last_sig_addr = info->si_addr;
    os_longjmp(jb, 1);
}

/* Previous handler, plain form: record and longjmp. */
static void
prev_plain(int sig)
{
    (void)sig;
    prev_called++;
    os_longjmp(jb, 1);
}

/* One PROT_NONE page; touching it raises SIGSEGV. */
static void *
map_inaccessible_page(void)
{
    size_t page = (size_t)getpagesize();
    void *p = mmap(NULL, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    return p == MAP_FAILED ? NULL : p;
}

/* Faults on a PROT_NONE page; the handler (or previous one) recovers. */
static void
fault_and_recover(void)
{
    volatile int *p;

    r_page = map_inaccessible_page();
    if (!r_page)
        return;

    if (os_setjmp(jb) == 0) {
        p = r_page;
        *p = 1;
    }
    else {
        r_recovered = 1;
        os_signal_unmask();
    }
}

static void *
child_inited_probe(void *arg)
{
    (void)arg;
    r_child_inited = os_thread_signal_inited();
    return NULL;
}

static void *
child_fault_and_recover(void *arg)
{
    (void)arg;
    fault_and_recover();
    return NULL;
}

#if !defined(__SANITIZE_ADDRESS__)
/*
 * Keeps a 1 KiB frame per call so the walk reaches the guard pages quickly.
 * The depth cap is never reached: the guard page faults first.
 */
static int __attribute__((noinline))
recurse_until_overflow(int depth)
{
    volatile char buf[1024];

    buf[0] = (char)depth;
    if (depth > 1000000)
        return buf[0];
    return recurse_until_overflow(depth + 1) + buf[0];
}

static void *
child_stack_overflow(void *arg)
{
    (void)arg;
    r_boundary = os_thread_get_stack_boundary();
    r_guard_bytes =
        (size_t)getpagesize() * STACK_OVERFLOW_CHECK_GUARD_PAGE_COUNT;

    if (os_setjmp(jb) == 0) {
        (void)recurse_until_overflow(0);
    }
    else {
        r_recovered = 1;
        os_signal_unmask();
    }
    return NULL;
}
#endif

static void
spawn_and_join(void *(*fn)(void *))
{
    korp_tid tid;

    assert_int_equal(os_thread_create(&tid, fn, NULL, 256 * 1024), BHT_OK);
    assert_int_equal(os_thread_join(tid, NULL), BHT_OK);
}

/*
 * Initializes the signal environment in the main thread with `handler`
 * (optionally after installing a previous SIGSEGV handler), runs `body`,
 * then destroys it. Returns the init result.
 */
static int
run_in_main(void (*body)(void), os_signal_handler handler, int prev_kind)
{
    struct sigaction act;
    int ret;

    if (prev_kind != PREV_NONE) {
        memset(&act, 0, sizeof(act));
        sigemptyset(&act.sa_mask);
        if (prev_kind == PREV_SIGINFO) {
            act.sa_sigaction = prev_sigaction;
            act.sa_flags = SA_SIGINFO;
        }
        else {
            act.sa_handler = prev_plain;
        }
        assert_int_equal(sigaction(SIGSEGV, &act, NULL), 0);
    }

    ret = os_thread_signal_init(handler);
    if (ret == 0)
        body();
    os_thread_signal_destroy();
    return ret;
}

/* ---------------- tests ---------------- */

static void
body_init_destroy(void)
{
    r_inited_start = os_thread_signal_inited();
    os_thread_signal_destroy();
    r_inited_after_destroy = os_thread_signal_inited();
    r_init_again_ret = os_thread_signal_init(test_handler_longjmp);
    r_inited_after_init = os_thread_signal_inited();
}

static void
test_signal_init_destroy(void **state)
{
    int ret;

    (void)state;
    reset_results();
    ret = run_in_main(body_init_destroy, test_handler_longjmp, PREV_NONE);

    assert_int_equal(ret, 0);
    assert_true(r_inited_start);
    assert_false(r_inited_after_destroy);
    assert_int_equal(r_init_again_ret, 0);
    assert_true(r_inited_after_init);
    assert_false(os_thread_signal_inited());
}

static void
body_init_idempotent(void)
{
    /* Already initialized: must return 0 early and keep the state. */
    r_init_again_ret = os_thread_signal_init(test_handler_longjmp);
    r_inited_after_init = os_thread_signal_inited();
}

static void
test_signal_init_idempotent(void **state)
{
    (void)state;
    reset_results();
    assert_int_equal(
        run_in_main(body_init_idempotent, test_handler_longjmp, PREV_NONE), 0);

    assert_int_equal(r_init_again_ret, 0);
    assert_true(r_inited_after_init);
}

static void
test_signal_destroy_without_init(void **state)
{
    (void)state;
    assert_false(os_thread_signal_inited());
    os_thread_signal_destroy();
    assert_false(os_thread_signal_inited());
}

static void
body_thread_local(void)
{
    r_inited_start = os_thread_signal_inited();
    spawn_and_join(child_inited_probe);
}

static void
test_signal_inited_is_thread_local(void **state)
{
    (void)state;
    reset_results();
    assert_int_equal(
        run_in_main(body_thread_local, test_handler_longjmp, PREV_NONE), 0);

    /* The main thread was initialized; the new thread saw its own state. */
    assert_true(r_inited_start);
    assert_true(r_child_inited);
}

static void
test_signal_handler_invoked_on_segv(void **state)
{
    (void)state;
    reset_results();
    assert_int_equal(
        run_in_main(fault_and_recover, test_handler_longjmp, PREV_NONE), 0);

    assert_non_null(r_page);
    assert_int_equal(handler_called, 1);
    assert_ptr_equal(last_sig_addr, r_page);
    assert_int_equal(r_recovered, 1);
    assert_int_equal(prev_called, 0);
}

static void
body_inherit(void)
{
    spawn_and_join(child_fault_and_recover);
}

static void
test_signal_handler_inherited_by_child(void **state)
{
    (void)state;
    reset_results();
    /* The child thread takes its handler from the creator's init. */
    assert_int_equal(run_in_main(body_inherit, test_handler_longjmp, PREV_NONE),
                     0);

    assert_non_null(r_page);
    assert_int_equal(handler_called, 1);
    assert_ptr_equal(last_sig_addr, r_page);
    assert_int_equal(r_recovered, 1);
}

static void
test_signal_forward_to_sigaction(void **state)
{
    (void)state;
    reset_results();
    assert_int_equal(
        run_in_main(fault_and_recover, test_handler_return, PREV_SIGINFO), 0);

    assert_int_equal(handler_called, 1);
    assert_int_equal(prev_called, 1);
    assert_ptr_equal(last_sig_addr, r_page);
    assert_int_equal(r_recovered, 1);
}

static void
test_signal_forward_to_plain_handler(void **state)
{
    (void)state;
    reset_results();
    assert_int_equal(
        run_in_main(fault_and_recover, test_handler_return, PREV_PLAIN), 0);

    assert_int_equal(handler_called, 1);
    assert_int_equal(prev_called, 1);
    assert_int_equal(r_recovered, 1);
}

#if !defined(__SANITIZE_ADDRESS__)
static void
body_stack_overflow(void)
{
    spawn_and_join(child_stack_overflow);
}

static void
test_stack_guard_overflow(void **state)
{
    (void)state;
    reset_results();
    assert_int_equal(
        run_in_main(body_stack_overflow, test_handler_longjmp, PREV_NONE), 0);

    assert_non_null(r_boundary);
    assert_int_equal(r_recovered, 1);
    assert_int_equal(handler_called, 1);
    /* The fault must land in the guard pages at the stack boundary. */
    assert_true((uint8 *)last_sig_addr >= r_boundary);
    assert_true((uint8 *)last_sig_addr < r_boundary + r_guard_bytes);
}
#endif

/*
 * Runs in a forked process: SIGSEGV is reset to SIG_DFL, so the unhandled
 * path prints the report and calls abort(). Keeping this out of the main
 * process avoids leaving the test process in a broken signal state.
 */
static void
test_signal_unhandled_aborts(void **state)
{
    struct sigaction dfl;
    pid_t pid;
    int status = 0;

    (void)state;
    fflush(stdout);
    fflush(stderr);

    pid = fork();
    assert_true(pid >= 0);
    if (pid == 0) {
        memset(&dfl, 0, sizeof(dfl));
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        sigaction(SIGSEGV, &dfl, NULL);

        os_thread_signal_init(NULL);
        {
            volatile int *p = NULL;
            *p = 1;
        }
        _exit(0); /* not reached */
    }

    assert_int_equal(waitpid(pid, &status, 0), pid);
    assert_true(WIFSIGNALED(status));
    assert_int_equal(WTERMSIG(status), SIGABRT);
}

static void
test_sigreturn_smoke(void **state)
{
    (void)state;
    os_sigreturn();
}
