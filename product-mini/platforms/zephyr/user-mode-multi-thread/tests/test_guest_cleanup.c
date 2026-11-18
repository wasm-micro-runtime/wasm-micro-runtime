/*
 * Copyright (C) 2026 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

int
wamr_guest_main(void);
int
__real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *),
                      void *);
int
__real_pthread_join(pthread_t, void **);
int
__real_pthread_mutex_destroy(pthread_mutex_t *);
int
__real_pthread_cond_destroy(pthread_cond_t *);

static int fail_create;
static int create_calls;
static int joined_workers;
static int mutex_destroys;
static int cond_destroys;
static int mutex_destroy_result;
static int cond_destroy_result;

/* Inject only creation failure; synchronization and successful worker creation
 * and join use real POSIX primitives. Compile the actual guest source
 * unchanged.
 */
int
__wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                      void *(*start)(void *), void *arg)
{
    return ++create_calls == fail_create
               ? EAGAIN
               : __real_pthread_create(thread, attr, start, arg);
}

int
__wrap_pthread_join(pthread_t thread, void **result)
{
    int ret = __real_pthread_join(thread, result);

    if (ret == 0) {
        joined_workers++;
    }
    return ret;
}

int
__wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    mutex_destroys++;
    mutex_destroy_result = __real_pthread_mutex_destroy(mutex);
    return mutex_destroy_result;
}

int
__wrap_pthread_cond_destroy(pthread_cond_t *cond)
{
    cond_destroys++;
    cond_destroy_result = __real_pthread_cond_destroy(cond);
    return cond_destroy_result;
}

int
main(int argc, char **argv)
{
    int result;

    if (argc != 2 || (argv[1][0] != '1' && argv[1][0] != '2')
        || argv[1][1] != '\0') {
        return EXIT_FAILURE;
    }
    fail_create = argv[1][0] - '0';
    result = wamr_guest_main();
    if (result != 3 || create_calls != fail_create
        || joined_workers != fail_create - 1 || mutex_destroys != 1
        || cond_destroys != 1 || mutex_destroy_result != 0
        || cond_destroy_result != 0) {
        fprintf(stderr,
                "guest cleanup failed: result=%d, joined=%d, "
                "mutex destroy=%d, condition destroy=%d\n",
                result, joined_workers, mutex_destroy_result,
                cond_destroy_result);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
