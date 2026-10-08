/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/app_memory/app_memdomain.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "zephyr_sync_pool.h"
#include "zephyr_thread_pool.h"

#define EXIT_OK 0
#define EXIT_HOST 1
#define WAMR_ROOT_PRIORITY 5

WAMR_ZEPHYR_THREAD_POOL_DEFINE(wamr_threads, 4, 4096);
WAMR_ZEPHYR_SYNC_POOL_DEFINE(wamr_sync, 16, 8);
K_APPMEM_PARTITION_DEFINE(wamr_partition);
K_THREAD_STACK_DEFINE(wamr_root_stack, 8192);

static struct k_thread wamr_root_thread;
static struct k_mem_domain wamr_domain;

extern struct k_mem_partition z_libc_partition;
extern int wamr_runtime_result;
extern void
wamr_runtime_main(void *, void *, void *);

static int
run_wamr_root(void)
{
    struct k_mem_partition *parts[] = {
        &wamr_partition,
        &z_libc_partition,
    };
    k_tid_t root;

    printk("wamr_partition start addr: %ld, size: %zu\n", wamr_partition.start,
           wamr_partition.size);

    root = k_thread_create(&wamr_root_thread, wamr_root_stack,
                           K_THREAD_STACK_SIZEOF(wamr_root_stack),
                           wamr_runtime_main, NULL, NULL, NULL,
                           WAMR_ROOT_PRIORITY, K_USER, K_FOREVER);
    if (root == NULL) {
        printk("ERROR: failed to create suspended WAMR root thread\n");
        return EXIT_HOST;
    }

    if (k_mem_domain_init(&wamr_domain, ARRAY_SIZE(parts), parts) != 0) {
        printk("ERROR: failed to initialize WAMR memory domain\n");
        return EXIT_HOST;
    }

    if (k_mem_domain_add_thread(&wamr_domain, root) != 0) {
        printk("ERROR: failed to add WAMR root thread to memory domain\n");
        return EXIT_HOST;
    }

    if (wamr_zephyr_thread_pool_prepare(&wamr_threads, root) != BHT_OK) {
        printk("ERROR: failed to prepare WAMR user thread pool\n");
        return EXIT_HOST;
    }

    if (wamr_zephyr_sync_pool_prepare(&wamr_sync, root) != BHT_OK) {
        printk("ERROR: failed to prepare WAMR user sync pool\n");
        return EXIT_HOST;
    }

    wamr_runtime_result = EXIT_HOST;
    k_thread_start(root);
    if (k_thread_join(root, K_FOREVER) != 0) {
        printk("ERROR: failed to join WAMR root thread\n");
        return EXIT_HOST;
    }

    return wamr_runtime_result;
}

int
main(void)
{
    return run_wamr_root();
}
