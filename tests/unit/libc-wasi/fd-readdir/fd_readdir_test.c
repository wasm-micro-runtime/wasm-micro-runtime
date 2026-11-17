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

/* Sentinel DIR stream returned by the os_fdopendir() stub. */
static char g_dir_storage;
#define TEST_DIR_STREAM ((os_dir_stream) & g_dir_storage)

/* ---------------------------------------------------------------------------
 * Stub state, reset by each test's fixture.
 * ------------------------------------------------------------------------ */

static __wasi_filetype_t g_filetype = __WASI_FILETYPE_REGULAR_FILE;
static int g_fdopendir_calls;
static int g_closedir_calls;
static int g_readdir_calls;

/* Records the directory handle seen by wasm_runtime_free() for one object. */
static struct fd_object *g_watch_fo;
static bool g_watch_snapshot_taken;
static os_dir_stream g_watch_handle_at_free;

/* ---------------------------------------------------------------------------
 * Stubs for OS and runtime dependencies (linked with -Wl,--wrap=...).
 * ------------------------------------------------------------------------ */

__wasi_errno_t
__wrap_os_fstat(os_file_handle handle, struct __wasi_filestat_t *buf)
{
    (void)handle;
    memset(buf, 0, sizeof(*buf));
    buf->st_filetype = g_filetype;
    return __WASI_ESUCCESS;
}

/* Non-zero means "not a tty". */
__wasi_errno_t
__wrap_os_isatty(os_file_handle handle)
{
    (void)handle;
    return __WASI_EINVAL;
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

__wasi_errno_t
__wrap_os_close(os_file_handle handle, bool is_stdio)
{
    (void)handle;
    (void)is_stdio;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
__wrap_os_fdopendir(os_file_handle handle, os_dir_stream *dir_stream)
{
    (void)handle;
    g_fdopendir_calls++;
    *dir_stream = TEST_DIR_STREAM;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
__wrap_os_closedir(os_dir_stream dir_stream)
{
    (void)dir_stream;
    g_closedir_calls++;
    return __WASI_ESUCCESS;
}

/* First call yields one entry named "a"; later calls report end of stream. */
__wasi_errno_t
__wrap_os_readdir(os_dir_stream dir_stream, __wasi_dirent_t *entry,
                  const char **d_name)
{
    (void)dir_stream;
    if (g_readdir_calls++ == 0) {
        memset(entry, 0, sizeof(*entry));
        entry->d_next = 1;
        entry->d_namlen = 1;
        *d_name = "a";
    }
    else {
        *d_name = NULL;
    }
    return __WASI_ESUCCESS;
}

/*
 * Fill freshly allocated memory with 0xBE, as ASan does. A non-directory
 * object that never initializes directory.handle then looks like a valid
 * DIR stream, which exposes the missing initialization deterministically.
 */
void *
__wrap_wasm_runtime_malloc(unsigned int size)
{
    void *ptr = malloc(size);
    if (ptr != NULL)
        memset(ptr, 0xBE, size);
    return ptr;
}

void
__wrap_wasm_runtime_free(void *ptr)
{
    if (g_watch_fo != NULL && ptr == (void *)g_watch_fo) {
        g_watch_handle_at_free = g_watch_fo->directory.handle;
        g_watch_snapshot_taken = true;
        g_watch_fo = NULL;
    }
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

static void
reset_stubs(void)
{
    g_filetype = __WASI_FILETYPE_REGULAR_FILE;
    g_fdopendir_calls = 0;
    g_closedir_calls = 0;
    g_readdir_calls = 0;
    g_watch_fo = NULL;
    g_watch_snapshot_taken = false;
    g_watch_handle_at_free = NULL;
}

static int
fixture_setup(void **state)
{
    struct fixture *f = calloc(1, sizeof(*f));

    assert_non_null(f);
    reset_stubs();
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

/* Reads a directory fd through the public entry point. */
static __wasi_errno_t
readdir_fd(struct fixture *f, __wasi_fd_t fd, size_t *bufused)
{
    char buf[256];

    return wasmtime_ssp_fd_readdir(TEST_EXEC_ENV, &f->ft, fd, buf, sizeof(buf),
                                   0, bufused);
}

/* Opens a directory fd and reads from it, so directory.handle is valid. */
static void
open_and_read_dir(struct fixture *f, __wasi_fd_t fd)
{
    size_t bufused = 0;

    g_filetype = __WASI_FILETYPE_DIRECTORY;
    assert_true(fd_table_insert_existing(&f->ft, fd, 100, false));
    assert_int_equal(readdir_fd(f, fd, &bufused), __WASI_ESUCCESS);
    assert_true(
        os_is_dir_stream_valid(&f->ft.entries[fd].object->directory.handle));
}

/* ---------------------------------------------------------------------------
 * Test cases for the directory state lifecycle and fd_readdir()
 * ------------------------------------------------------------------------ */

/* A new non-directory object must not inherit a stale DIR handle. Before the
 * fix, directory.handle is the 0xBE fill, which reads as a valid stream. */
static void
test_fd_object_new_NonDirectory_HandleInvalid(void **state)
{
    struct fixture *f = (struct fixture *)*state;
    struct fd_object *fo;

    g_filetype = __WASI_FILETYPE_CHARACTER_DEVICE;
    assert_true(fd_table_insert_existing(&f->ft, 4, 101, false));

    fo = f->ft.entries[4].object;
    assert_non_null(fo);
    assert_false(os_is_dir_stream_valid(&fo->directory.handle));
}

/* Releasing a directory must clear its handle before the object is freed.
 * The wasm_runtime_free() wrapper snapshots the handle at free time. */
static void
test_fd_object_release_Directory_ClearsHandle(void **state)
{
    struct fixture *f = (struct fixture *)*state;

    open_and_read_dir(f, 5);

    g_watch_fo = f->ft.entries[5].object;
    assert_int_equal(wasmtime_ssp_fd_close(TEST_EXEC_ENV, &f->ft, &f->pt, 5),
                     __WASI_ESUCCESS);

    assert_int_equal(g_closedir_calls, 1);
    assert_true(g_watch_snapshot_taken);
    assert_false(os_is_dir_stream_valid(&g_watch_handle_at_free));
}

/* Directory fd is closed, then a device fd is inserted. The new object must
 * not see the released DIR, and fd_readdir() on it must return ENOTDIR
 * without calling into the directory stream code. */
static void
test_close_ThenReuse_NoStaleHandle(void **state)
{
    struct fixture *f = (struct fixture *)*state;
    struct fd_object *fo;

    open_and_read_dir(f, 5);

    g_watch_fo = f->ft.entries[5].object;
    assert_int_equal(wasmtime_ssp_fd_close(TEST_EXEC_ENV, &f->ft, &f->pt, 5),
                     __WASI_ESUCCESS);
    assert_true(g_watch_snapshot_taken);
    assert_false(os_is_dir_stream_valid(&g_watch_handle_at_free));

    g_filetype = __WASI_FILETYPE_CHARACTER_DEVICE;
    g_fdopendir_calls = 0;
    g_readdir_calls = 0;
    assert_true(fd_table_insert_existing(&f->ft, 6, 102, false));

    fo = f->ft.entries[6].object;
    assert_false(os_is_dir_stream_valid(&fo->directory.handle));

    size_t bufused = 0;
    assert_int_equal(readdir_fd(f, 6, &bufused), __WASI_ENOTDIR);
    assert_int_equal(g_fdopendir_calls, 0);
    assert_int_equal(g_readdir_calls, 0);
}

/* FD_READDIR is granted to character devices. fd_readdir() must reject them
 * with ENOTDIR, leave the directory stream alone, and drop its reference. */
static void
test_readdir_OnCharDevice_ReturnsEnotdir(void **state)
{
    struct fixture *f = (struct fixture *)*state;
    size_t bufused = 0;

    g_filetype = __WASI_FILETYPE_CHARACTER_DEVICE;
    assert_true(fd_table_insert_existing(&f->ft, 4, 101, false));

    assert_int_equal(readdir_fd(f, 4, &bufused), __WASI_ENOTDIR);
    assert_int_equal(g_fdopendir_calls, 0);
    assert_int_equal(g_readdir_calls, 0);
    assert_int_equal(f->ft.entries[4].object->refcount.count, 1);
}

/* A real directory still reads entries. */
static void
test_readdir_OnDirectory_Works(void **state)
{
    struct fixture *f = (struct fixture *)*state;
    size_t bufused = 0;

    g_filetype = __WASI_FILETYPE_DIRECTORY;
    assert_true(fd_table_insert_existing(&f->ft, 5, 100, false));

    assert_int_equal(readdir_fd(f, 5, &bufused), __WASI_ESUCCESS);
    assert_int_equal(g_fdopendir_calls, 1);
    assert_int_equal(bufused, sizeof(__wasi_dirent_t) + 1);
    assert_int_equal(f->ft.entries[5].object->refcount.count, 1);
}

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            test_fd_object_new_NonDirectory_HandleInvalid, fixture_setup,
            fixture_teardown),
        cmocka_unit_test_setup_teardown(
            test_fd_object_release_Directory_ClearsHandle, fixture_setup,
            fixture_teardown),
        cmocka_unit_test_setup_teardown(test_close_ThenReuse_NoStaleHandle,
                                        fixture_setup, fixture_teardown),
        cmocka_unit_test_setup_teardown(
            test_readdir_OnCharDevice_ReturnsEnotdir, fixture_setup,
            fixture_teardown),
        cmocka_unit_test_setup_teardown(test_readdir_OnDirectory_Works,
                                        fixture_setup, fixture_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
