/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_random.h"

#include <stdlib.h>
#include <errno.h>
#include <sys/random.h>
#include <pthread.h>
#include "wasm_export.h"

// wasi:random

/**
 * @brief Get a number of cryptographically-secure random bytes.
 * @details Implements the `get-random-bytes` function from the
 * `wasi:random/random` interface. It uses the `getrandom` system call to ensure
 * a high level of entropy.
 * @note The caller is responsible for freeing the returned list (`ret->buf`).
 * @param len The number of random bytes to generate.
 * @param[out] ret A pointer to a list struct that will be populated with the
 *                 generated random bytes.
 */
void
wasi_random_get_random_bytes(uint64_t len, wasi_list_u8_t *ret)
{
    ssize_t s;
    size_t offset = 0;

    if (!ret) {
        return;
    }

    ret->buf = wasm_runtime_malloc(len);
    if (!ret->buf) {
        ret->buf_len = 0;
        return;
    }

    while (offset < len) {
        s = getrandom(ret->buf + offset, len - offset, 0);
        if (s < 0) {
            if (errno == EINTR) {
                continue;
            }
            wasm_runtime_free(ret->buf);
            ret->buf = NULL;
            ret->buf_len = 0;
            return;
        }
        offset += s;
    }
    ret->buf_len = len;
}

/**
 * @brief Get a cryptographically-secure random `u64` value.
 * @details Implements the `get-random-u64` function from the
 * `wasi:random/random` interface. It uses the `getrandom` system call for a
 * high level of entropy.
 * @return A random 64-bit unsigned integer.
 */
uint64_t
wasi_random_get_random_u64(void)
{
    uint64_t val;
    ssize_t s;
    size_t offset = 0;

    while (offset < sizeof(val)) {
        s = getrandom((uint8_t *)&val + offset, sizeof(val) - offset, 0);
        if (s < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        offset += s;
    }

    return val;
}

static unsigned int insecure_seed;
static bool insecure_seed_initialized = false;
static pthread_mutex_t insecure_seed_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Ensures that the seed for the insecure random number generator is
 * initialized.
 * @details This function is thread-safe. It uses a mutex to ensure that the
 *          seed is initialized only once. The seed is obtained from the
 *          cryptographically-secure random number generator.
 */
static void
ensure_insecure_seed_initialized()
{
    pthread_mutex_lock(&insecure_seed_mutex);
    if (!insecure_seed_initialized) {
        insecure_seed = (unsigned int)wasi_random_get_random_u64();
        insecure_seed_initialized = true;
    }
    pthread_mutex_unlock(&insecure_seed_mutex);
}

/**
 * @brief Get a number of insecure random bytes.
 * @details Implements the `get-insecure-random-bytes` function from the
 *          `wasi:random/insecure` interface. It uses the `rand_r` function,
 *          which is not cryptographically secure.
 * @note The caller is responsible for freeing the returned list (`ret->buf`).
 * @param len The number of random bytes to generate.
 * @param[out] ret A pointer to a list struct that will be populated with the
 *                 generated random bytes.
 */
void
wasi_random_get_insecure_random_bytes(uint64_t len, wasi_list_u8_t *ret)
{
    if (!ret) {
        return;
    }

    ensure_insecure_seed_initialized();

    ret->buf = wasm_runtime_malloc(len);
    if (!ret->buf) {
        ret->buf_len = 0;
        return;
    }
    for (uint64_t i = 0; i < len; i++) {
        ret->buf[i] = rand_r(&insecure_seed);
    }
    ret->buf_len = len;
}

/**
 * @brief Get an insecure random `u64` value.
 * @details Implements the `get-insecure-random-u64` function from the
 *          `wasi:random/insecure` interface. It uses the `rand_r` function.
 * @return A random 64-bit unsigned integer.
 */
uint64_t
wasi_random_get_insecure_random_u64(void)
{
    ensure_insecure_seed_initialized();

    uint64_t val;
    uint8_t *p = (uint8_t *)&val;
    for (size_t i = 0; i < sizeof(val); i++) {
        p[i] = rand_r(&insecure_seed);
    }
    return val;
}

/**
 * @brief Get a new seed for the insecure random number generator.
 * @details Implements the `insecure-seed` function from the
 *          `wasi:random/insecure-seed` interface. It returns two `u64` values
 *          that can be used to seed an insecure random number generator.
 * @param[out] seed1 A pointer to store the first 64-bit seed value.
 * @param[out] seed2 A pointer to store the second 64-bit seed value.
 */
void
wasi_random_get_insecure_seed(uint64_t *seed1, uint64_t *seed2)
{
    if (!seed1 || !seed2) {
        return;
    }
    *seed1 = wasi_random_get_random_u64();
    *seed2 = wasi_random_get_random_u64();
}
