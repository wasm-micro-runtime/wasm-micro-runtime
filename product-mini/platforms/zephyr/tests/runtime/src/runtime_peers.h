/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_RUNTIME_PEERS_H
#define WAMR_ZEPHYR_RUNTIME_PEERS_H

#include <zephyr/kernel.h>

/*
 * Two persistent application-created Zephyr threads (K_USER in the userspace
 * build) that WAMR did not create. The Ztest thread drives them one step at a
 * time, so their WAMR object lifetimes overlap deterministically.
 */
#define RUNTIME_PEER_COUNT 2U

/* Grants the application must provide before a native thread uses WAMR, e.g.
 * memory-domain membership and synchronization-pool objects. May be NULL.
 */
typedef void (*runtime_peer_grant_t)(k_tid_t tid);

void
runtime_peers_start(runtime_peer_grant_t grant);
void
runtime_peers_stop(void);
/* Releases whatever a failed scenario left behind; use as the after hook. */
void
runtime_peers_cleanup(void);

/* Scenario bodies; they assert with zassert_*(). */
void
runtime_peers_check_wasm_identities(void);
void
runtime_peers_check_c_api_stores(void);

#endif /* WAMR_ZEPHYR_RUNTIME_PEERS_H */
