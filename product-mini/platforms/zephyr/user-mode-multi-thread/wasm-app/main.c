/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define WORKER_COUNT 2
#define WORKER_INCREMENT 21
#define EXPECTED_TOTAL (WORKER_COUNT * WORKER_INCREMENT)

struct worker_state {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int entered;
    int released;
    int finished;
    int counter;
};

static void *
worker_main(void *arg)
{
    struct worker_state *state = arg;

    if (pthread_mutex_lock(&state->mutex) != 0) {
        return (void *)(uintptr_t)1;
    }

    state->entered++;
    if (state->entered == WORKER_COUNT) {
        if (pthread_cond_signal(&state->cond) != 0) {
            pthread_mutex_unlock(&state->mutex);
            return (void *)(uintptr_t)2;
        }
    }

    while (!state->released) {
        if (pthread_cond_wait(&state->cond, &state->mutex) != 0) {
            pthread_mutex_unlock(&state->mutex);
            return (void *)(uintptr_t)3;
        }
    }

    state->counter += WORKER_INCREMENT;
    state->finished++;
    if (pthread_cond_signal(&state->cond) != 0) {
        pthread_mutex_unlock(&state->mutex);
        return (void *)(uintptr_t)4;
    }

    if (pthread_mutex_unlock(&state->mutex) != 0) {
        return (void *)(uintptr_t)5;
    }

    return NULL;
}

int
main(void)
{
    struct worker_state state = { 0 };
    pthread_t workers[WORKER_COUNT];
    int created = 0;
    int result = 11;
    int main_locked = 0;

    if (pthread_mutex_init(&state.mutex, NULL) != 0) {
        return 1;
    }
    if (pthread_cond_init(&state.cond, NULL) != 0) {
        result = 2;
        goto destroy_mutex;
    }

    if (pthread_mutex_lock(&state.mutex) != 0) {
        result = 4;
        goto destroy_cond;
    }
    main_locked = 1;

    for (created = 0; created < WORKER_COUNT; created++) {
        if (pthread_create(&workers[created], NULL, worker_main, &state) != 0) {
            result = 3;
            goto release_workers;
        }
    }

    while (state.entered != WORKER_COUNT) {
        if (pthread_cond_wait(&state.cond, &state.mutex) != 0) {
            if (main_locked) {
                pthread_mutex_unlock(&state.mutex);
                main_locked = 0;
            }
            result = 5;
            goto release_workers;
        }
    }

    state.released = 1;
    if (pthread_cond_broadcast(&state.cond) != 0) {
        if (main_locked) {
            pthread_mutex_unlock(&state.mutex);
            main_locked = 0;
        }
        result = 6;
        goto join_workers;
    }

    while (state.finished != WORKER_COUNT) {
        if (pthread_cond_wait(&state.cond, &state.mutex) != 0) {
            if (main_locked) {
                pthread_mutex_unlock(&state.mutex);
                main_locked = 0;
            }
            result = 7;
            goto join_workers;
        }
    }

    if (pthread_mutex_unlock(&state.mutex) != 0) {
        result = 8;
        goto join_workers;
    }
    main_locked = 0;

    result = state.counter == EXPECTED_TOTAL ? 0 : 9;
    goto join_workers;

release_workers:
    if (created > 0 && !main_locked && pthread_mutex_lock(&state.mutex) == 0) {
        main_locked = 1;
    }
    if (main_locked) {
        if (created > 0) {
            state.released = 1;
            pthread_cond_broadcast(&state.cond);
        }
        /* The first create can fail while main still owns this mutex. */
        pthread_mutex_unlock(&state.mutex);
        main_locked = 0;
    }

join_workers:
    while (created-- > 0) {
        void *worker_result = NULL;

        if (pthread_join(workers[created], &worker_result) != 0
            && result == 0) {
            result = 10;
        }
        else if (worker_result != NULL && result == 0) {
            result = 12;
        }
    }

destroy_cond:
    pthread_cond_destroy(&state.cond);
destroy_mutex:
    pthread_mutex_destroy(&state.mutex);
    return result;
}
