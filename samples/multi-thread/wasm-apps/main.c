/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>

static pthread_mutex_t mutex;
static pthread_cond_t cond;
static sem_t *sem;

static void *
thread(void *arg)
{
    int *num = (int *)arg;

    pthread_mutex_lock(&mutex);
    printf("thread start \n");

    for (int i = 0; i < 10; i++) {
        *num = *num + 1;
        printf("num: %d\n", *num);
    }

    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
    sem_post(sem);

    printf("thread exit \n");

    return NULL;
}

int
main(int argc, char *argv[])
{
    pthread_t tid;
    int num = 0, ret = -1;

    if (pthread_mutex_init(&mutex, NULL) != 0) {
        printf("Failed to init mutex.\n");
        /* iwasm does not propagate the return value of a libc-builtin wasm
         * main; make any failure an explicit runtime exception so the host
         * exits non-zero. */
        __builtin_trap();
    }
    if (pthread_cond_init(&cond, NULL) != 0) {
        printf("Failed to init cond.\n");
        __builtin_trap();
    }

    // O_CREAT and S_IRGRPS_IRGRP | S_IWGRP on linux (glibc), initial value is 0

    if (!(sem = sem_open("tessstsem", 0100, 0x10 | 0x20, 0))) {
        printf("Failed to open sem. %p\n", sem);
        __builtin_trap();
    }

    pthread_mutex_lock(&mutex);
    if (pthread_create(&tid, NULL, thread, &num) != 0) {
        printf("Failed to create thread.\n");
        pthread_mutex_unlock(&mutex);
        __builtin_trap();
    }

    printf("cond wait start\n");
    pthread_cond_wait(&cond, &mutex);
    pthread_mutex_unlock(&mutex);
    printf("cond wait success.\n");

    if (sem_wait(sem) != 0) {
        printf("Failed to wait sem.\n");
        __builtin_trap();
    }
    else {
        printf("sem wait success.\n");
    }

    if (pthread_join(tid, NULL) != 0) {
        printf("Failed to join thread.\n");
        __builtin_trap();
    }

    ret = 0;

    sem_close(sem);
    sem_unlink("tessstsem");
    pthread_cond_destroy(&cond);
    pthread_mutex_destroy(&mutex);

    return ret;
}