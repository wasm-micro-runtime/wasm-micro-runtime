/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdint.h>
#include <stdlib.h>
#include <cmocka.h>

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

/* Stubs for the runtime hooks posix_thread.c references. */
static int malloc_fail_next;

void *
wasm_runtime_malloc(unsigned int size)
{
    if (malloc_fail_next) {
        malloc_fail_next = 0;
        return NULL;
    }
    return malloc(size);
}

void
wasm_runtime_free(void *ptr)
{
    free(ptr);
}

void
os_end_blocking_op(void)
{
}

#include "posix_thread_test.c"

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_mutex_init_lock_unlock_destroy),
        cmocka_unit_test(test_mutex_excludes_other_thread),
        cmocka_unit_test(test_mutex_contention_counter),
#if defined(__linux__)
        cmocka_unit_test(test_mutex_destroy_while_locked),
#endif
        cmocka_unit_test(test_recursive_mutex_relock),
        cmocka_unit_test(test_cond_init_destroy),
        cmocka_unit_test(test_cond_wait_signal),
        cmocka_unit_test(test_cond_broadcast_wakes_all),
        cmocka_unit_test(test_cond_reltimedwait_timeout),
        cmocka_unit_test(test_cond_reltimedwait_signaled),
        cmocka_unit_test(test_cond_reltimedwait_forever),
        cmocka_unit_test(test_cond_reltimedwait_sec_overflow),
        cmocka_unit_test(test_cond_reltimedwait_nsec_carry),
        cmocka_unit_test(test_sem_open_close_unlink),
        cmocka_unit_test(test_sem_wait_post_getvalue),
        cmocka_unit_test(test_sem_trywait),
        cmocka_unit_test(test_sem_wait_blocks_until_post),
        cmocka_unit_test(test_sem_open_existing_excl),
        cmocka_unit_test(test_sem_unlink_nonexistent),
        cmocka_unit_test(test_rwlock_init_destroy),
        cmocka_unit_test(test_rwlock_multiple_readers),
        cmocka_unit_test(test_rwlock_writer_excludes),
        cmocka_unit_test(test_rwlock_unlock_roundtrip),
        cmocka_unit_test(test_thread_create_join),
        cmocka_unit_test(test_thread_exit_retval),
        cmocka_unit_test(test_thread_create_with_prio),
        cmocka_unit_test(test_thread_create_invalid_stack),
        cmocka_unit_test(test_thread_create_malloc_fail),
        cmocka_unit_test(test_self_thread_identity),
        cmocka_unit_test(test_thread_detach),
        cmocka_unit_test(test_thread_join_null_retval),
        cmocka_unit_test(test_thread_create_join_many),
        cmocka_unit_test(test_stack_boundary_non_null),
        cmocka_unit_test(test_stack_boundary_cached),
        cmocka_unit_test(test_stack_boundary_in_created_thread),
        cmocka_unit_test(test_jit_write_protect_smoke),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
