/*
 * Copyright (C) 2026 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define WORKER_ROUNDS 6
#define MAX_WAIT_ATTEMPTS 100
#define RESULT_SENTINEL ((void *)(uintptr_t)0x12345678)

struct worker_state {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int completed;
    int failure;
};

static void *
self_join_worker(void *arg)
{
    struct worker_state *state = arg;
    void *result = RESULT_SENTINEL;
    int join_status = pthread_join(pthread_self(), &result);
    int detach_status = pthread_detach(pthread_self());

    pthread_mutex_lock(&state->mutex);
    if (join_status == 0 || result != RESULT_SENTINEL || detach_status != 0) {
        state->failure = 1;
    }
    state->completed++;
    pthread_cond_signal(&state->cond);
    pthread_mutex_unlock(&state->mutex);
    return NULL;
}

int
main(void)
{
    struct worker_state state = { 0 };
    int round;
    int result = 0;

    if (pthread_mutex_init(&state.mutex, NULL) != 0) {
        return 1;
    }
    if (pthread_cond_init(&state.cond, NULL) != 0) {
        pthread_mutex_destroy(&state.mutex);
        return 2;
    }
    pthread_mutex_lock(&state.mutex);

    /* More rounds than the sample's four native slots. A successful external
     * join would hide a poisoned waiter count, so workers only self-detach.
     */
    for (round = 0; round < WORKER_ROUNDS; round++) {
        pthread_t worker;
        int attempt;

        for (attempt = 0; attempt < MAX_WAIT_ATTEMPTS; attempt++) {
            if (pthread_create(&worker, NULL, self_join_worker, &state) == 0) {
                break;
            }
            /* Completion is published before native termination. Release the
             * mutex and yield through a bounded wait so cleanup can finish.
             */
            pthread_cond_timedwait(&state.cond, &state.mutex, 1000);
        }
        if (attempt == MAX_WAIT_ATTEMPTS) {
            printf("ERROR: worker capacity not reclaimed at round %d\n", round);
            result = 3;
            break;
        }

        for (attempt = 0;
             state.completed <= round && attempt < MAX_WAIT_ATTEMPTS;
             attempt++) {
            pthread_cond_timedwait(&state.cond, &state.mutex, 1000);
        }
        if (state.completed <= round) {
            /* Keep stack-owned state alive; the process timeout reports a
             * stuck worker instead of returning while it can still use it.
             */
            while (state.completed <= round) {
                pthread_cond_wait(&state.cond, &state.mutex);
            }
            result = 4;
            break;
        }
    }

    if (state.failure != 0) {
        printf("ERROR: failed self-join changed output or detach failed\n");
        result = 5;
    }
    pthread_mutex_unlock(&state.mutex);
    pthread_cond_destroy(&state.cond);
    pthread_mutex_destroy(&state.mutex);
    if (result == 0) {
        printf("PASS: failed self-join preserves output and worker capacity\n");
    }
    return result;
}
