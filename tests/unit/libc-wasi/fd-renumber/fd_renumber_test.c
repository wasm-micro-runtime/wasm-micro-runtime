/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#include "wasm_exec_env.h"

/*
 * posix.c is excluded from the runtime library for this target. Including it
 * here gives the tests access to the static helpers and the fd_object layout.
 */
#include "posix.c"

static char g_exec_env_storage;
#define TEST_EXEC_ENV ((wasm_exec_env_t) & g_exec_env_storage)

/* ---------------------------------------------------------------------------
 * Stubs for OS and runtime dependencies (linked with -Wl,--wrap=...).
 * ------------------------------------------------------------------------ */

/* Every fd is a regular file unless a test changes this. */
static __wasi_filetype_t g_filetype = __WASI_FILETYPE_REGULAR_FILE;

__wasi_errno_t
__wrap_os_fstat(os_file_handle handle, struct __wasi_filestat_t *buf)
{
    (void)handle;
    memset(buf, 0, sizeof(*buf));
    buf->st_filetype = g_filetype;
    return __WASI_ESUCCESS;
}

/* Fake handles have no real fcntl(); report read-write access. */
__wasi_errno_t
__wrap_os_file_get_access_mode(os_file_handle handle,
                               wasi_libc_file_access_mode *access_mode)
{
    (void)handle;
    *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;
    return __WASI_ESUCCESS;
}

/* Non-zero means "not a tty". */
__wasi_errno_t
__wrap_os_isatty(os_file_handle handle)
{
    (void)handle;
    return __WASI_EINVAL;
}

__wasi_errno_t
__wrap_os_close(os_file_handle handle, bool is_stdio)
{
    (void)handle;
    (void)is_stdio;
    return __WASI_ESUCCESS;
}

void *
__wrap_wasm_runtime_malloc(unsigned int size)
{
    return malloc(size);
}

void
__wrap_wasm_runtime_free(void *ptr)
{
    free(ptr);
}

bool
__wrap_wasm_runtime_begin_blocking_op(WASMExecEnv *exec_env)
{
    (void)exec_env;
    return true;
}

void
__wrap_wasm_runtime_end_blocking_op(WASMExecEnv *exec_env)
{
    (void)exec_env;
}

/* ---------------------------------------------------------------------------
 * Fixtures
 * ------------------------------------------------------------------------ */

/* Each test owns a fresh fd_table and fd_prestats pair. */
struct fixture {
    struct fd_table ft;
    struct fd_prestats pt;
};

static int
fixture_setup(void **state)
{
    struct fixture *f = calloc(1, sizeof(*f));

    assert_non_null(f);
    g_filetype = __WASI_FILETYPE_REGULAR_FILE;
    assert_true(fd_table_init(&f->ft));
    assert_true(fd_prestats_init(&f->pt));
    *state = f;
    return 0;
}

static int
fixture_teardown(void **state)
{
    struct fixture *f = (struct fixture *)*state;

    fd_prestats_destroy(&f->pt);
    fd_table_destroy(&f->ft);
    free(f);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Test cases for wasmtime_ssp_fd_renumber()
 * ------------------------------------------------------------------------ */

/* Renumbering an fd onto itself must succeed and keep the descriptor.
 * Before the fix: fd_table_detach() clears the entry, then
 * refcount_acquire() dereferences NULL. */
static void
test_renumber_SameFd_Succeeds(void **state)
{
    struct fixture *f = (struct fixture *)*state;

    assert_true(fd_table_insert_existing(&f->ft, 3, 100, false));
    assert_int_equal(f->ft.used, 1);

    assert_int_equal(
        wasmtime_ssp_fd_renumber(TEST_EXEC_ENV, &f->ft, &f->pt, 3, 3),
        __WASI_ESUCCESS);

    assert_non_null(f->ft.entries[3].object);
    assert_int_equal(f->ft.used, 1);
}

/* An invalid fd still reports EBADF. */
static void
test_renumber_SameInvalidFd_ReturnsEbadf(void **state)
{
    struct fixture *f = (struct fixture *)*state;

    assert_int_equal(
        wasmtime_ssp_fd_renumber(TEST_EXEC_ENV, &f->ft, &f->pt, 7, 7),
        __WASI_EBADF);
}

/* Self-renumbering a preopen must keep its prestat entry. */
static void
test_renumber_SamePreopenFd_KeepsPrestat(void **state)
{
    struct fixture *f = (struct fixture *)*state;
    struct fd_prestat *pre = NULL;

    assert_true(fd_table_insert_existing(&f->ft, 3, 100, false));
    assert_true(fd_prestats_insert(&f->pt, "/", 3));

    assert_int_equal(
        wasmtime_ssp_fd_renumber(TEST_EXEC_ENV, &f->ft, &f->pt, 3, 3),
        __WASI_ESUCCESS);

    assert_int_equal(fd_prestats_get_entry(&f->pt, 3, &pre), __WASI_ESUCCESS);
    assert_non_null(pre->dir);
}

/* A successful renumber removes exactly one descriptor, so `used` drops by
 * one. Before the fix it drops by two. */
static void
test_renumber_DistinctFds_DecrementsUsedOnce(void **state)
{
    struct fixture *f = (struct fixture *)*state;

    assert_true(fd_table_insert_existing(&f->ft, 3, 100, false));
    assert_true(fd_table_insert_existing(&f->ft, 4, 101, false));
    assert_int_equal(f->ft.used, 2);

    assert_int_equal(
        wasmtime_ssp_fd_renumber(TEST_EXEC_ENV, &f->ft, &f->pt, 3, 4),
        __WASI_ESUCCESS);

    assert_null(f->ft.entries[3].object);
    assert_non_null(f->ft.entries[4].object);
    assert_int_equal(f->ft.used, 1);
}

/* Repeated insert + renumber must not leave `used` under-counted. If it
 * does, the table reports no free slots and fd_table_unused() never returns.
 * The used-count check runs first so a regression fails instead of hanging. */
static void
test_renumber_Loop_NoHang(void **state)
{
    struct fixture *f = (struct fixture *)*state;
    __wasi_fd_t unused = 0;
    int i;

    assert_true(fd_table_insert_existing(&f->ft, 3, 100, false));

    for (i = 0; i < 16; i++) {
        assert_true(fd_table_insert_existing(&f->ft, 4, 200 + i, false));
        assert_int_equal(
            wasmtime_ssp_fd_renumber(TEST_EXEC_ENV, &f->ft, &f->pt, 4, 3),
            __WASI_ESUCCESS);
    }

    assert_int_equal(f->ft.used, 1);

    rwlock_rdlock(&f->ft.lock);
    assert_int_equal(fd_table_unused(&f->ft, &unused), __WASI_ESUCCESS);
    rwlock_unlock(&f->ft.lock);
    assert_null(f->ft.entries[unused].object);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_renumber_SameFd_Succeeds,
                                        fixture_setup, fixture_teardown),
        cmocka_unit_test_setup_teardown(
            test_renumber_SameInvalidFd_ReturnsEbadf, fixture_setup,
            fixture_teardown),
        cmocka_unit_test_setup_teardown(
            test_renumber_SamePreopenFd_KeepsPrestat, fixture_setup,
            fixture_teardown),
        cmocka_unit_test_setup_teardown(
            test_renumber_DistinctFds_DecrementsUsedOnce, fixture_setup,
            fixture_teardown),
        cmocka_unit_test_setup_teardown(test_renumber_Loop_NoHang,
                                        fixture_setup, fixture_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
