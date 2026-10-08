/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-FileCopyrightText: 2024 Siemens AG (For Zephyr usermode changes)
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "zephyr_thread_pool.h"

#if defined(CONFIG_USERSPACE)
/* The public object-validity API does not expose registered stack capacity.
 * Confine this internal metadata dependency to supervisor pool preparation;
 * it mirrors the native create verifier, not a user-callable syscall handler.
 */
#include <zephyr/internal/syscall_handler.h>
#endif

/* clang-format off */
#define bh_assert(v) do {                                   \
    if (!(v)) {                                             \
        printf("\nASSERTION FAILED: %s, at %s, line %d\n",  \
               #v, __FILE__, __LINE__);                     \
        abort();                                            \
    }                                                       \
} while (0)
/* clang-format on */

#if defined(CONFIG_ARM_MPU) || defined(CONFIG_ARC_MPU) \
    || KERNEL_VERSION_NUMBER > 0x020300 /* version 2.3.0 */
#define BH_ENABLE_ZEPHYR_MPU_STACK 1
#elif !defined(BH_ENABLE_ZEPHYR_MPU_STACK)
#define BH_ENABLE_ZEPHYR_MPU_STACK 0
#endif

typedef enum {
    WAMR_THREAD_FREE,
    WAMR_THREAD_RESERVED,
    WAMR_THREAD_RUNNING,
    WAMR_THREAD_EXITED,
    WAMR_THREAD_DETACHED_RUNNING,
    WAMR_THREAD_JOINED,
} wamr_thread_state_t;

static wamr_zephyr_thread_pool_t wamr_thread_pool;
static k_tid_t wamr_thread_pool_owner;
static wamr_thread_state_t wamr_thread_pool_states[BH_ZEPHYR_MPU_STACK_COUNT];
static zmutex_t thread_pool_lock;

#if defined(CONFIG_USERSPACE)
static bool
thread_pool_storage_valid(const wamr_zephyr_thread_pool_t *pool)
{
    uintptr_t thread_start = (uintptr_t)pool->threads;
    uintptr_t stack_start = (uintptr_t)pool->stacks;
    size_t total_size;
    size_t i;

    if (pool->stack_size > SIZE_MAX - K_THREAD_STACK_RESERVED
        || pool->thread_count > SIZE_MAX / pool->stack_stride
        || thread_start
               > UINTPTR_MAX - pool->thread_count * sizeof(struct k_thread)
        || stack_start
               > UINTPTR_MAX - pool->thread_count * pool->stack_stride) {
        return false;
    }
    total_size = pool->stack_size + K_THREAD_STACK_RESERVED;

    for (i = 0; i < pool->thread_count; i++) {
        struct k_object *thread_object =
            k_object_find((void *)(thread_start + i * sizeof(struct k_thread)));
        struct k_object *stack_object =
            k_object_find((void *)(stack_start + i * pool->stack_stride));
        size_t stack_object_size;

        if (thread_object == NULL || thread_object->type != K_OBJ_THREAD
            || (thread_object->flags & K_OBJ_FLAG_INITIALIZED) != 0
            || stack_object == NULL
            || stack_object->type != K_OBJ_THREAD_STACK_ELEMENT
            || (stack_object->flags & K_OBJ_FLAG_INITIALIZED) != 0) {
            return false;
        }
#if defined(CONFIG_GEN_PRIV_STACKS)
        stack_object_size = stack_object->data.stack_data->size;
#else
        stack_object_size = stack_object->data.stack_size;
#endif
        if (pool->stack_stride != stack_object_size
            || total_size > stack_object_size) {
            return false;
        }
    }
    return true;
}
#endif

int
wamr_zephyr_thread_pool_prepare(const wamr_zephyr_thread_pool_t *pool,
                                k_tid_t wamr_user_thread)
{
    size_t i;

    if (k_is_user_context() || pool == NULL || wamr_user_thread == NULL
        || pool->threads == NULL || pool->stacks == NULL
        || pool->thread_count == 0U
        || pool->thread_count > BH_ZEPHYR_MPU_STACK_COUNT
        || pool->stack_size == 0U || pool->stack_stride < pool->stack_size) {
        return BHT_ERROR;
    }

#if defined(CONFIG_USERSPACE)
    /* Validate the entire unused pool before granting or publishing any slot.
     * Identical re-preparation must not reject objects now used by workers.
     */
    if (wamr_thread_pool.threads == NULL && !thread_pool_storage_valid(pool)) {
        return BHT_ERROR;
    }
#endif

    /* Object validation checks the caller's permissions even in supervisor
     * mode. The provisioner may differ from the thread that created the root.
     * Grant uses registered-object lookup, so an unknown address is ignored.
     */
    k_object_access_grant(wamr_user_thread, k_current_get());
    if (!k_object_is_valid(wamr_user_thread, K_OBJ_THREAD)) {
        return BHT_ERROR;
    }

    if (wamr_thread_pool.threads != NULL) {
        return wamr_thread_pool_owner == wamr_user_thread
                       && wamr_thread_pool.threads == pool->threads
                       && wamr_thread_pool.stacks == pool->stacks
                       && wamr_thread_pool.thread_count == pool->thread_count
                       && wamr_thread_pool.stack_size == pool->stack_size
                       && wamr_thread_pool.stack_stride == pool->stack_stride
                   ? BHT_OK
                   : BHT_ERROR;
    }

    for (i = 0; i < pool->thread_count; i++) {
        k_object_access_grant(&pool->threads[i], wamr_user_thread);
        k_object_access_grant((uint8 *)pool->stacks + i * pool->stack_stride,
                              wamr_user_thread);
    }

    wamr_thread_pool = *pool;
    wamr_thread_pool_owner = wamr_user_thread;
    return BHT_OK;
}

#if BH_ENABLE_ZEPHYR_MPU_STACK != 0
static K_THREAD_STACK_ARRAY_DEFINE(mpu_stacks, BH_ZEPHYR_MPU_STACK_COUNT,
                                   BH_ZEPHYR_MPU_STACK_SIZE);
static wamr_thread_state_t mpu_stack_states[BH_ZEPHYR_MPU_STACK_COUNT];
#endif

typedef struct os_thread_wait_node {
    zsem_t sem;
    os_thread_wait_list next;
} os_thread_wait_node;

typedef struct os_thread_data {
    struct os_thread_data *next;
    korp_tid handle;
    k_tid_t tid;
    void *tlr;
    void *return_value;
    zmutex_t state_lock;
    wamr_thread_state_t state;
    bool join_claimed;
    bool uses_user_pool;
    size_t pool_index;
    unsigned stack_size;
    char *stack;
} os_thread_data;

typedef struct os_thread_obj {
    struct k_thread thread;
} os_thread_obj;

static bool is_thread_sys_inited = false;

/* Thread data of supervisor thread */
static os_thread_data supervisor_thread_data;

/* Thread data list */
static os_thread_data *thread_data_list = NULL;

/* Detached threads awaiting Zephyr termination before resource reuse. */
static os_thread_data *detached_thread_data_list = NULL;

/* Opaque WAMR identity; Zephyr thread objects are reusable pool storage. */
static uintptr_t next_thread_handle;

static void
thread_data_list_add_locked(os_thread_data *thread_data)
{
    if (!thread_data_list)
        thread_data_list = thread_data;
    else {
        /* If already in list, just return */
        os_thread_data *p = thread_data_list;
        while (p) {
            if (p == thread_data)
                return;
            p = p->next;
        }

        /* Set as head of list */
        thread_data->next = thread_data_list;
        thread_data_list = thread_data;
    }
}

static void
thread_data_list_remove_locked(os_thread_data *thread_data)
{
    if (thread_data_list) {
        if (thread_data_list == thread_data)
            thread_data_list = thread_data_list->next;
        else {
            /* Search and remove it from list */
            os_thread_data *p = thread_data_list;
            while (p && p->next != thread_data)
                p = p->next;
            if (p && p->next == thread_data)
                p->next = p->next->next;
        }
    }
}

static void
detached_thread_data_list_add_locked(os_thread_data *thread_data)
{
    thread_data->next = detached_thread_data_list;
    detached_thread_data_list = thread_data;
}

static wamr_thread_state_t *
thread_slot_state_locked(os_thread_data *thread_data)
{
    if (thread_data->uses_user_pool) {
        return &wamr_thread_pool_states[thread_data->pool_index];
    }
#if BH_ENABLE_ZEPHYR_MPU_STACK != 0
    return &mpu_stack_states[thread_data->pool_index];
#else
    return NULL;
#endif
}

static void
thread_slot_set_state_locked(os_thread_data *thread_data,
                             wamr_thread_state_t state)
{
    wamr_thread_state_t *slot_state = thread_slot_state_locked(thread_data);

    if (slot_state != NULL) {
        *slot_state = state;
    }
}

static void
thread_generation_release_locked(os_thread_data *thread_data)
{
    wamr_thread_state_t *slot_state = thread_slot_state_locked(thread_data);

    thread_data->next = NULL;
    thread_data->handle = NULL;
    thread_data->tid = NULL;
    thread_data->tlr = NULL;
    thread_data->return_value = NULL;
    thread_data->state = WAMR_THREAD_FREE;
    thread_data->join_claimed = false;
    thread_data->uses_user_pool = false;
    thread_data->pool_index = 0U;
    thread_data->stack_size = 0U;
    thread_data->stack = NULL;
    if (slot_state != NULL) {
        *slot_state = WAMR_THREAD_FREE;
    }
}

static void
detached_thread_data_release(os_thread_data *thread_data)
{
#if BH_ENABLE_ZEPHYR_MPU_STACK == 0
    char *stack;
#endif
    bool uses_user_pool;
    k_tid_t tid;

    zmutex_lock(&thread_pool_lock, K_FOREVER);
    zmutex_lock(&thread_data->state_lock, K_FOREVER);
    uses_user_pool = thread_data->uses_user_pool;
    tid = thread_data->tid;
#if BH_ENABLE_ZEPHYR_MPU_STACK == 0
    stack = thread_data->stack;
#endif
    thread_generation_release_locked(thread_data);
    zmutex_unlock(&thread_data->state_lock);
    zmutex_unlock(&thread_pool_lock);

    if (!uses_user_pool) {
#if BH_ENABLE_ZEPHYR_MPU_STACK == 0
        BH_FREE(stack);
#endif
        BH_FREE((os_thread_obj *)tid);
    }
    BH_FREE(thread_data);
}

static os_thread_data *
thread_data_list_lookup_tid_locked(k_tid_t tid)
{
    if (thread_data_list) {
        os_thread_data *p = thread_data_list;
        while (p) {
            if (p->tid == tid)
                return p;
            p = p->next;
        }
    }
    return NULL;
}

static os_thread_data *
thread_data_list_lookup_handle_locked(korp_tid handle)
{
    os_thread_data *thread_data = thread_data_list;

    while (thread_data != NULL) {
        if (thread_data->handle == handle) {
            return thread_data;
        }
        thread_data = thread_data->next;
    }
    return NULL;
}

static korp_tid
thread_handle_alloc_locked(void)
{
    next_thread_handle++;
    if (next_thread_handle == 0U) {
        next_thread_handle++;
    }
    return (korp_tid)next_thread_handle;
}

static void
detached_thread_data_reap(void)
{
    os_thread_data *thread_data;
    os_thread_data *previous;

    while (true) {
        zmutex_lock(&thread_pool_lock, K_FOREVER);
        previous = NULL;
        thread_data = detached_thread_data_list;
        while (thread_data != NULL
               && k_thread_join(thread_data->tid, K_NO_WAIT) != 0) {
            previous = thread_data;
            thread_data = thread_data->next;
        }
        if (thread_data == NULL) {
            zmutex_unlock(&thread_pool_lock);
            return;
        }

        if (previous == NULL) {
            detached_thread_data_list = thread_data->next;
        }
        else {
            previous->next = thread_data->next;
        }
        zmutex_unlock(&thread_pool_lock);

        detached_thread_data_release(thread_data);
    }
}

static os_thread_data *
thread_data_list_claim_join(korp_tid handle)
{
    os_thread_data *claimed = NULL;

    zmutex_lock(&thread_pool_lock, K_FOREVER);
    if (thread_data_list) {
        os_thread_data *p = thread_data_list;

        while (p) {
            if (p->handle == handle) {
                zmutex_lock(&p->state_lock, K_FOREVER);
                if (!p->join_claimed
                    && (p->state == WAMR_THREAD_RUNNING
                        || p->state == WAMR_THREAD_EXITED)) {
                    p->join_claimed = true;
                    claimed = p;
                }
                zmutex_unlock(&p->state_lock);
                break;
            }
            p = p->next;
        }
    }
    zmutex_unlock(&thread_pool_lock);
    return claimed;
}

static bool
thread_pool_has_active_slots_locked(void)
{
    size_t i;

    if (thread_data_list != &supervisor_thread_data
        || supervisor_thread_data.next != NULL
        || detached_thread_data_list != NULL) {
        return true;
    }

    for (i = 0; i < BH_ZEPHYR_MPU_STACK_COUNT; i++) {
        if (wamr_thread_pool_states[i] != WAMR_THREAD_FREE) {
            return true;
        }
#if BH_ENABLE_ZEPHYR_MPU_STACK != 0
        if (mpu_stack_states[i] != WAMR_THREAD_FREE) {
            return true;
        }
#endif
    }

    return false;
}

int
os_thread_sys_init()
{
    if (is_thread_sys_inited)
        return BHT_OK;

    zmutex_init(&thread_pool_lock);
    memset(wamr_thread_pool_states, 0, sizeof(wamr_thread_pool_states));
#if BH_ENABLE_ZEPHYR_MPU_STACK != 0
    memset(mpu_stack_states, 0, sizeof(mpu_stack_states));
#endif

    /* Initialize supervisor thread data */
    memset(&supervisor_thread_data, 0, sizeof(supervisor_thread_data));
    supervisor_thread_data.tid = k_current_get();
    supervisor_thread_data.handle = thread_handle_alloc_locked();
    zmutex_init(&supervisor_thread_data.state_lock);
    supervisor_thread_data.state = WAMR_THREAD_RUNNING;
    /* Set as head of thread data list */
    thread_data_list = &supervisor_thread_data;

    is_thread_sys_inited = true;
    return BHT_OK;
}

void
os_thread_sys_destroy(void)
{
    if (!is_thread_sys_inited) {
        return;
    }

    detached_thread_data_reap();
    zmutex_lock(&thread_pool_lock, K_FOREVER);
    if (thread_pool_has_active_slots_locked()) {
        os_printf("WAMR Zephyr thread pool still has active slots\n");
        zmutex_unlock(&thread_pool_lock);
        return;
    }

    thread_data_list = NULL;
    detached_thread_data_list = NULL;
    memset(&supervisor_thread_data, 0, sizeof(supervisor_thread_data));
    memset(&wamr_thread_pool, 0, sizeof(wamr_thread_pool));
    wamr_thread_pool_owner = NULL;
    memset(wamr_thread_pool_states, 0, sizeof(wamr_thread_pool_states));
#if BH_ENABLE_ZEPHYR_MPU_STACK != 0
    memset(mpu_stack_states, 0, sizeof(mpu_stack_states));
#endif
    is_thread_sys_inited = false;
    zmutex_unlock(&thread_pool_lock);
}

static void
os_thread_complete(void *return_value)
{
    os_thread_data *thread_data;

    zmutex_lock(&thread_pool_lock, K_FOREVER);
    thread_data = thread_data_list_lookup_tid_locked(k_current_get());
    bh_assert(thread_data != NULL);
    zmutex_lock(&thread_data->state_lock, K_FOREVER);
    thread_data->return_value = return_value;
    if (thread_data->state == WAMR_THREAD_RUNNING) {
        thread_data->state = WAMR_THREAD_EXITED;
        thread_slot_set_state_locked(thread_data, WAMR_THREAD_EXITED);
    }
    else if (thread_data->state == WAMR_THREAD_DETACHED_RUNNING) {
        thread_data_list_remove_locked(thread_data);
        detached_thread_data_list_add_locked(thread_data);
    }
    zmutex_unlock(&thread_data->state_lock);
    zmutex_unlock(&thread_pool_lock);
}

static void
os_thread_wrapper(void *start, void *arg, void *thread_data)
{
    void *return_value;

    bh_assert(((os_thread_data *)thread_data)->tid == k_current_get());
    return_value = ((thread_start_routine_t)start)(arg);
    os_thread_complete(return_value);
}

int
os_thread_create(korp_tid *p_tid, thread_start_routine_t start, void *arg,
                 unsigned int stack_size)
{
    return os_thread_create_with_prio(p_tid, start, arg, stack_size,
                                      BH_THREAD_DEFAULT_PRIORITY);
}

int
os_thread_create_with_prio(korp_tid *p_tid, thread_start_routine_t start,
                           void *arg, unsigned int stack_size, int prio)
{
    os_thread_obj *thread_obj = NULL;
    os_thread_data *thread_data = NULL;
    k_tid_t tid = NULL;
    bool uses_user_pool = k_is_user_context();
    char *stack_to_free = NULL;
    size_t pool_index = 0U;
    size_t i;

    if (!p_tid || !start || !stack_size)
        return BHT_ERROR;

#if BH_ENABLE_ZEPHYR_MPU_STACK != 0
    if (!uses_user_pool && stack_size > BH_ZEPHYR_MPU_STACK_SIZE)
        return BHT_ERROR;
#endif

    if (uses_user_pool) {
        if (wamr_thread_pool.threads == NULL
            || stack_size > wamr_thread_pool.stack_size
            || prio < K_HIGHEST_APPLICATION_THREAD_PRIO
            || prio > K_LOWEST_APPLICATION_THREAD_PRIO
            || prio < k_thread_priority_get(k_current_get())) {
            return BHT_ERROR;
        }
    }

    detached_thread_data_reap();

    if (!uses_user_pool) {
        if (!(thread_obj = BH_MALLOC(sizeof(os_thread_obj)))) {
            return BHT_ERROR;
        }
        memset(thread_obj, 0, sizeof(*thread_obj));
        tid = &thread_obj->thread;
    }

    /* Create and initialize thread data */
    if (!(thread_data = BH_MALLOC(sizeof(os_thread_data)))) {
        goto fail;
    }

    memset(thread_data, 0, sizeof(*thread_data));
    zmutex_init(&thread_data->state_lock);
    thread_data->state = WAMR_THREAD_RESERVED;
    thread_data->uses_user_pool = uses_user_pool;

#if BH_ENABLE_ZEPHYR_MPU_STACK == 0
    if (!uses_user_pool) {
        if (stack_size < APP_THREAD_STACK_SIZE_MIN)
            stack_size = APP_THREAD_STACK_SIZE_MIN;
        if (!(thread_data->stack = BH_MALLOC(stack_size))) {
            goto fail;
        }
        thread_data->stack_size = stack_size;
    }
#endif

    zmutex_lock(&thread_pool_lock, K_FOREVER);
    if (uses_user_pool) {
        for (i = 0; i < wamr_thread_pool.thread_count; i++) {
            if (wamr_thread_pool_states[i] == WAMR_THREAD_FREE) {
                pool_index = i;
                break;
            }
        }
        if (i == wamr_thread_pool.thread_count) {
            zmutex_unlock(&thread_pool_lock);
            goto fail;
        }

        wamr_thread_pool_states[pool_index] = WAMR_THREAD_RESERVED;
        tid = &wamr_thread_pool.threads[pool_index];
        thread_data->stack = (char *)wamr_thread_pool.stacks
                             + pool_index * wamr_thread_pool.stack_stride;
        thread_data->stack_size = wamr_thread_pool.stack_size;
    }
#if BH_ENABLE_ZEPHYR_MPU_STACK != 0
    else {
        for (i = 0; i < BH_ZEPHYR_MPU_STACK_COUNT; i++) {
            if (mpu_stack_states[i] == WAMR_THREAD_FREE) {
                pool_index = i;
                break;
            }
        }
        if (i == BH_ZEPHYR_MPU_STACK_COUNT) {
            zmutex_unlock(&thread_pool_lock);
            goto fail;
        }

        mpu_stack_states[pool_index] = WAMR_THREAD_RESERVED;
        thread_data->stack = (char *)mpu_stacks[pool_index];
        thread_data->stack_size = BH_ZEPHYR_MPU_STACK_SIZE;
    }
#endif
    thread_data->pool_index = pool_index;
    thread_data->handle = thread_handle_alloc_locked();
    thread_data->tid = tid;
    thread_data_list_add_locked(thread_data);
    zmutex_unlock(&thread_pool_lock);

    if (!k_thread_create(
            tid, (k_thread_stack_t *)thread_data->stack,
            thread_data->stack_size, os_thread_wrapper, start, arg, thread_data,
            prio, uses_user_pool ? K_USER | K_INHERIT_PERMS : 0, K_FOREVER)) {
        goto fail3;
    }

    zmutex_lock(&thread_pool_lock, K_FOREVER);
    zmutex_lock(&thread_data->state_lock, K_FOREVER);
    thread_data->state = WAMR_THREAD_RUNNING;
    thread_slot_set_state_locked(thread_data, WAMR_THREAD_RUNNING);
    zmutex_unlock(&thread_data->state_lock);
    zmutex_unlock(&thread_pool_lock);
    k_thread_name_set(tid, "wasm-zephyr");
    *p_tid = thread_data->handle;
    k_thread_start(tid);
    return BHT_OK;

fail3:
    stack_to_free = thread_data->stack;
    zmutex_lock(&thread_pool_lock, K_FOREVER);
    thread_data_list_remove_locked(thread_data);
    thread_generation_release_locked(thread_data);
    zmutex_unlock(&thread_pool_lock);
fail:
    if (thread_data != NULL) {
#if BH_ENABLE_ZEPHYR_MPU_STACK == 0
        if (!uses_user_pool) {
            BH_FREE(stack_to_free != NULL ? stack_to_free : thread_data->stack);
        }
#endif
        BH_FREE(thread_data);
    }
    if (!uses_user_pool) {
        BH_FREE(thread_obj);
    }
    return BHT_ERROR;
}

korp_tid
os_self_thread()
{
    os_thread_data *thread_data;
    korp_tid handle = NULL;

    if (!is_thread_sys_inited
        || zmutex_lock(&thread_pool_lock, K_FOREVER) != 0) {
        return NULL;
    }

    thread_data = thread_data_list_lookup_tid_locked(k_current_get());
    if (thread_data != NULL) {
        handle = thread_data->handle;
    }
    zmutex_unlock(&thread_pool_lock);
    return handle;
}

int
os_thread_join(korp_tid thread, void **value_ptr)
{
    os_thread_data *thread_data;
    k_tid_t tid;
#if BH_ENABLE_ZEPHYR_MPU_STACK == 0
    char *stack;
#endif
    bool uses_user_pool;

    thread_data = thread_data_list_claim_join(thread);
    if (thread_data == NULL) {
        return BHT_ERROR;
    }
    if (k_thread_join(thread_data->tid, K_FOREVER) != 0) {
        zmutex_lock(&thread_pool_lock, K_FOREVER);
        zmutex_lock(&thread_data->state_lock, K_FOREVER);
        thread_data->join_claimed = false;
        zmutex_unlock(&thread_data->state_lock);
        zmutex_unlock(&thread_pool_lock);
        return BHT_ERROR;
    }

    zmutex_lock(&thread_data->state_lock, K_FOREVER);
    if (value_ptr != NULL) {
        *value_ptr = thread_data->return_value;
    }
    thread_data->state = WAMR_THREAD_JOINED;
    zmutex_unlock(&thread_data->state_lock);
    zmutex_lock(&thread_pool_lock, K_FOREVER);
    uses_user_pool = thread_data->uses_user_pool;
    tid = thread_data->tid;
#if BH_ENABLE_ZEPHYR_MPU_STACK == 0
    stack = thread_data->stack;
#endif
    thread_data_list_remove_locked(thread_data);
    thread_slot_set_state_locked(thread_data, WAMR_THREAD_JOINED);
    thread_generation_release_locked(thread_data);
    zmutex_unlock(&thread_pool_lock);

    if (!uses_user_pool) {
#if BH_ENABLE_ZEPHYR_MPU_STACK == 0
        BH_FREE(stack);
#endif
        BH_FREE((os_thread_obj *)tid);
    }

    BH_FREE(thread_data);
    return BHT_OK;
}

int
os_mutex_init(korp_mutex *mutex)
{
    zmutex_init(mutex);
    return BHT_OK;
}

int
os_recursive_mutex_init(korp_mutex *mutex)
{
    zmutex_init(mutex);
    return BHT_OK;
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    return zmutex_lock(mutex, K_FOREVER);
}

int
os_mutex_unlock(korp_mutex *mutex)
{
#if KERNEL_VERSION_NUMBER >= 0x020200 /* version 2.2.0 */
    return zmutex_unlock(mutex);
#else
    zmutex_unlock(mutex);
    return 0;
#endif
}

int
os_cond_init(korp_cond *cond)
{
    zmutex_init(&cond->wait_list_lock);
    cond->thread_wait_list = NULL;
    return BHT_OK;
}

int
os_cond_destroy(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

static int
os_cond_wait_internal(korp_cond *cond, korp_mutex *mutex, bool timed, int mills)
{
    os_thread_wait_node *node;

    /* Create wait node and append it to wait list */
    if (!(node = BH_MALLOC(sizeof(os_thread_wait_node))))
        return BHT_ERROR;

    zsem_init(&node->sem, 0, 1);
    node->next = NULL;

    zmutex_lock(&cond->wait_list_lock, K_FOREVER);
    if (!cond->thread_wait_list)
        cond->thread_wait_list = node;
    else {
        /* Add to end of wait list */
        os_thread_wait_node *p = cond->thread_wait_list;
        while (p->next)
            p = p->next;
        p->next = node;
    }
    zmutex_unlock(&cond->wait_list_lock);

    /* Unlock mutex, wait sem and lock mutex again */
    zmutex_unlock(mutex);
    zsem_take(&node->sem, timed ? Z_TIMEOUT_MS(mills) : K_FOREVER);
    zmutex_lock(mutex, K_FOREVER);

    /* Remove wait node from wait list */
    zmutex_lock(&cond->wait_list_lock, K_FOREVER);
    if (cond->thread_wait_list == node)
        cond->thread_wait_list = node->next;
    else {
        /* Remove from the wait list */
        os_thread_wait_node *p = cond->thread_wait_list;
        while (p->next != node)
            p = p->next;
        p->next = node->next;
    }
    BH_FREE(node);
    zmutex_unlock(&cond->wait_list_lock);

    return BHT_OK;
}

int
os_cond_wait(korp_cond *cond, korp_mutex *mutex)
{
    return os_cond_wait_internal(cond, mutex, false, 0);
}

int
os_cond_reltimedwait(korp_cond *cond, korp_mutex *mutex, uint64 useconds)
{

    if (useconds == BHT_WAIT_FOREVER) {
        return os_cond_wait_internal(cond, mutex, false, 0);
    }
    else {
        uint64 mills_64 = useconds / 1000;
        int32 mills;

        if (mills_64 < (uint64)INT32_MAX) {
            mills = (int32)mills_64;
        }
        else {
            mills = INT32_MAX;
            os_printf("Warning: os_cond_reltimedwait exceeds limit, "
                      "set to max timeout instead\n");
        }
        return os_cond_wait_internal(cond, mutex, true, mills);
    }
}

int
os_cond_signal(korp_cond *cond)
{
    /* Signal the head wait node of wait list */
    zmutex_lock(&cond->wait_list_lock, K_FOREVER);
    if (cond->thread_wait_list)
        zsem_give(&cond->thread_wait_list->sem);
    zmutex_unlock(&cond->wait_list_lock);

    return BHT_OK;
}

uint8 *
os_thread_get_stack_boundary()
{
#if defined(CONFIG_THREAD_STACK_INFO) && !defined(CONFIG_USERSPACE)
    k_tid_t thread = k_current_get();
    return (uint8 *)thread->stack_info.start;
#else
    return NULL;
#endif
}

void
os_thread_jit_write_protect_np(bool enabled)
{
}

int
os_rwlock_init(korp_rwlock *lock)
{
    if (!lock) {
        return BHT_ERROR;
    }

    k_mutex_init(&lock->mtx);
    k_sem_init(&lock->sem, 0, K_SEM_MAX_LIMIT);
    lock->read_count = 0;

    return BHT_OK;
}

int
os_rwlock_rdlock(korp_rwlock *lock)
{
    /* Not implemented */
    return BHT_ERROR;
}

int
os_rwlock_wrlock(korp_rwlock *lock)
{
    // Acquire the mutex to ensure exclusive access
    if (k_mutex_lock(&lock->mtx, K_FOREVER) != 0) {
        return BHT_ERROR;
    }

    // Wait until there are no readers
    while (lock->read_count > 0) {
        // Release the mutex while we're waiting
        k_mutex_unlock(&lock->mtx);

        // Wait for a short time
        k_sleep(K_MSEC(1));

        // Re-acquire the mutex
        if (k_mutex_lock(&lock->mtx, K_FOREVER) != 0) {
            return BHT_ERROR;
        }
    }
    // At this point, we hold the mutex and there are no readers, so we have the
    // write lock
    return BHT_OK;
}

int
os_rwlock_unlock(korp_rwlock *lock)
{
    k_mutex_unlock(&lock->mtx);
    return BHT_OK;
}

int
os_rwlock_destroy(korp_rwlock *lock)
{
    /* Not implemented */
    return BHT_ERROR;
}

int
os_thread_detach(korp_tid thread)
{
    os_thread_data *thread_data;
    os_thread_data *exited_thread_data = NULL;
    int result = BHT_ERROR;

    zmutex_lock(&thread_pool_lock, K_FOREVER);
    thread_data = thread_data_list_lookup_handle_locked(thread);
    if (thread_data != NULL) {
        zmutex_lock(&thread_data->state_lock, K_FOREVER);
        if (!thread_data->join_claimed
            && thread_data->state == WAMR_THREAD_RUNNING) {
            thread_data->state = WAMR_THREAD_DETACHED_RUNNING;
            thread_slot_set_state_locked(thread_data,
                                         WAMR_THREAD_DETACHED_RUNNING);
            result = BHT_OK;
        }
        else if (!thread_data->join_claimed
                 && thread_data->state == WAMR_THREAD_EXITED) {
            thread_data->state = WAMR_THREAD_DETACHED_RUNNING;
            thread_slot_set_state_locked(thread_data,
                                         WAMR_THREAD_DETACHED_RUNNING);
            thread_data_list_remove_locked(thread_data);
            exited_thread_data = thread_data;
            result = BHT_OK;
        }
        else if (thread_data->state == WAMR_THREAD_DETACHED_RUNNING) {
            result = BHT_OK;
        }
        zmutex_unlock(&thread_data->state_lock);
    }
    zmutex_unlock(&thread_pool_lock);

    if (exited_thread_data != NULL) {
        if (k_thread_join(exited_thread_data->tid, K_FOREVER) == 0) {
            detached_thread_data_release(exited_thread_data);
        }
        else {
            zmutex_lock(&thread_pool_lock, K_FOREVER);
            zmutex_lock(&exited_thread_data->state_lock, K_FOREVER);
            detached_thread_data_list_add_locked(exited_thread_data);
            zmutex_unlock(&exited_thread_data->state_lock);
            zmutex_unlock(&thread_pool_lock);
            result = BHT_ERROR;
        }
    }
    return result;
}

void
os_thread_exit(void *retval)
{
    os_thread_complete(retval);
    k_thread_abort(k_current_get());
}

int
os_cond_broadcast(korp_cond *cond)
{
    os_thread_wait_node *node;
    zmutex_lock(&cond->wait_list_lock, K_FOREVER);
    node = cond->thread_wait_list;
    while (node) {
        os_thread_wait_node *next = node->next;
        zsem_give(&node->sem);
        node = next;
    }
    zmutex_unlock(&cond->wait_list_lock);
    return BHT_OK;
}

korp_sem *
os_sem_open(const char *name, int oflags, int mode, int val)
{
    /* Not implemented */
    return NULL;
}

int
os_sem_close(korp_sem *sem)
{
    /* Not implemented */
    return BHT_ERROR;
}

int
os_sem_wait(korp_sem *sem)
{
    /* Not implemented */
    return BHT_ERROR;
}

int
os_sem_trywait(korp_sem *sem)
{
    /* Not implemented */
    return BHT_ERROR;
}

int
os_sem_post(korp_sem *sem)
{
    /* Not implemented */
    return BHT_ERROR;
}

int
os_sem_getvalue(korp_sem *sem, int *sval)
{
    /* Not implemented */
    return BHT_ERROR;
}

int
os_sem_unlink(const char *name)
{
    /* Not implemented */
    return BHT_ERROR;
}

int
os_blocking_op_init()
{
    /* Not implemented */
    return BHT_ERROR;
}

void
os_begin_blocking_op()
{
    /* Not implemented */
}

void
os_end_blocking_op()
{
    /* Not implemented */
}

int
os_wakeup_blocking_op(korp_tid tid)
{
    /* Not implemented */
    return BHT_ERROR;
}
