/*
 * Copyright (C) 2026 The WAMR Authors. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int
os_thread_signal_init(void (*handler)(void *));
extern void
os_thread_signal_destroy(void);

static volatile sig_atomic_t forwarded;

static void
previous_handler(int signo, siginfo_t *info, void *context)
{
    (void)signo;
    (void)info;
    (void)context;
    forwarded++;
}

static void
wamr_handler(void *address)
{
    (void)address;
}

static void *
worker(void *arg)
{
    (void)arg;
    if (os_thread_signal_init(wamr_handler) != 0)
        return (void *)1;
    raise(SIGSEGV);
    os_thread_signal_destroy();
    return NULL;
}

int
main(void)
{
    struct sigaction action;
    pthread_t thread;
    void *result;

    memset(&action, 0, sizeof(action));
    action.sa_sigaction = previous_handler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, NULL) != 0)
        return 1;
    if (os_thread_signal_init(wamr_handler) != 0)
        return 2;
    alarm(5);
    if (pthread_create(&thread, NULL, worker, NULL) != 0)
        return 3;
    if (pthread_join(thread, &result) != 0)
        return 4;
    alarm(0);
    os_thread_signal_destroy();

    if (forwarded != 1 || result != NULL) {
        fprintf(stderr, "previous handler called %d times; worker result %p\n",
                (int)forwarded, result);
        return 5;
    }
    return 0;
}
