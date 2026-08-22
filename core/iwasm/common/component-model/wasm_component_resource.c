/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component_resource.h"
#include "wasm_runtime_common.h"
#include <string.h>

WASMResourceHandle *
wasm_create_resource_handle(WASMComponentResourceInstance *rt, uint32_t rep,
                            bool own)
{
    if (!rt) {
        return NULL;
    }

    if (rep < 1) {
        return NULL;
    }

    WASMResourceHandle *handle =
        wasm_runtime_malloc(sizeof(WASMResourceHandle));
    if (!handle) {
        return NULL;
    }

    memset(handle, 0, sizeof(WASMResourceHandle));

    handle->rt = rt;
    handle->own = own;
    handle->borrow_scope = NULL;
    handle->num_lends = 0;
    handle->rep = rep;

    return handle;
}

void
wasm_destroy_resource_handle(WASMResourceHandle *handle)
{
    if (!handle)
        return;

    // TODO: Call component destructor (handle->rt->dtor_func_idx)

    wasm_runtime_free(handle);
}