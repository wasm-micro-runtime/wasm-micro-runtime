/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
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

    int failed = 0;

    /* Each case needs a fresh process-wide signal handler state. */
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        pid_t pid;
        int status;

        fflush(NULL);
        pid = fork();
        if (pid == 0) {
            int result = _cmocka_run_group_tests(tests[i].name, &tests[i], 1,
                                                 NULL, NULL);
            fflush(NULL);
            _exit(result == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
        }
        if (pid < 0 || waitpid(pid, &status, 0) != pid || !WIFEXITED(status)
            || WEXITSTATUS(status) != EXIT_SUCCESS) {
            fprintf(stderr, "%s failed\n", tests[i].name);
            failed = 1;
        }
    }
    return failed;
}
