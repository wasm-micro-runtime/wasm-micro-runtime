/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

void *
os_malloc(unsigned size)
{
    void *buf_origin;
    void *buf_fixed;
    uintptr_t *addr_field;

    buf_origin = malloc(size + 8 + sizeof(uintptr_t));
    if (!buf_origin) {
        return NULL;
    }
    buf_fixed = buf_origin + sizeof(void *);
    if ((uintptr_t)buf_fixed & (uintptr_t)0x7) {
        buf_fixed = (void *)((uintptr_t)(buf_fixed + 8) & (~(uintptr_t)7));
    }

    addr_field = buf_fixed - sizeof(uintptr_t);
    *addr_field = (uintptr_t)buf_origin;

    return buf_fixed;
}

void *
os_realloc(void *ptr, unsigned size)
{
    void *mem_origin;
    void *mem_new;
    void *mem_new_fixed;
    uintptr_t *addr_field;
    uintptr_t old_offset;

    if (!ptr) {
        return os_malloc(size);
    }

    addr_field = ptr - sizeof(uintptr_t);
    mem_origin = (void *)(*addr_field);
    old_offset = (uintptr_t)ptr - (uintptr_t)mem_origin;
    mem_new = realloc(mem_origin, size + 8 + sizeof(uintptr_t));
    if (!mem_new) {
        return NULL;
    }

    if (mem_origin != mem_new) {
        mem_new_fixed = mem_new + sizeof(uintptr_t);
        if ((uint32)mem_new_fixed & 0x7) {
            mem_new_fixed =
                (void *)((uintptr_t)(mem_new + 8) & (~(uintptr_t)7));
        }

        /* realloc() keeps the payload at its old offset from the start of
         * the block (4 or 8, see os_malloc); move it if the aligned position
         * in the new block differs. The ranges may overlap, and both stay
         * within the size + 8 + sizeof(uintptr_t) bytes allocated. */
        if ((uintptr_t)mem_new_fixed - (uintptr_t)mem_new != old_offset) {
            memmove(mem_new_fixed, mem_new + old_offset, size);
        }

        addr_field = mem_new_fixed - sizeof(uintptr_t);
        *addr_field = (uintptr_t)mem_new;

        return mem_new_fixed;
    }

    return ptr;
}

void
os_free(void *ptr)
{
    void *mem_origin;
    uintptr_t *addr_field;

    if (ptr) {
        addr_field = ptr - sizeof(uintptr_t);
        mem_origin = (void *)(*addr_field);

        free(mem_origin);
    }
}

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    return -1;
}
