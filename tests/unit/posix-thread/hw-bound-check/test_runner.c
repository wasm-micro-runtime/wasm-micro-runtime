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

#include "posix_signal_test.c"

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_signal_init_destroy),
        cmocka_unit_test(test_signal_init_idempotent),
        cmocka_unit_test(test_signal_destroy_without_init),
        cmocka_unit_test(test_signal_inited_is_thread_local),
        cmocka_unit_test(test_signal_handler_invoked_on_segv),
        cmocka_unit_test(test_signal_handler_inherited_by_child),
        cmocka_unit_test(test_signal_forward_to_sigaction),
        cmocka_unit_test(test_signal_forward_to_plain_handler),
#if !defined(__SANITIZE_ADDRESS__)
        cmocka_unit_test(test_stack_guard_overflow),
#endif
        cmocka_unit_test(test_signal_unhandled_aborts),
        cmocka_unit_test(test_sigreturn_smoke),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
