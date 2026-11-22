/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
#include "soc/mmu.h"
#include "rom/cache.h"
#include "esp_mmu_map.h"

static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;

/* Heap memory in PSRAM is mapped on the data bus only. To run AOT code from
 * PSRAM, map the same physical pages on the instruction bus too. The code is
 * written through the data bus address (os_get_dbus_mirror) and run through
 * the instruction bus address that os_mmap returns. */
#define EXEC_MAP_MAX 8

typedef struct ExecMap {
    uint8 *ibus;
    uint8 *dbus;
    size_t size;
} ExecMap;

static ExecMap exec_maps[EXEC_MAP_MAX];

static void *
exec_mmap(size_t size)
{
    size_t page = CONFIG_MMU_PAGE_SIZE;
    size_t map_size = (size + page - 1) & ~(page - 1);
    esp_paddr_t paddr;
    mmu_target_t target;
    void *ibus = NULL;
    uint8 *dbus;
    esp_err_t err;
    int i;

    dbus = heap_caps_aligned_alloc(page, map_size, MALLOC_CAP_SPIRAM);
    if (!dbus) {
        os_printf("os_mmap: failed to allocate %u bytes of PSRAM for AOT "
                  "code\n",
                  (uint32)map_size);
        return NULL;
    }

    err = esp_mmu_vaddr_to_paddr(dbus, &paddr, &target);
    if (err != ESP_OK) {
        os_printf("os_mmap: esp_mmu_vaddr_to_paddr failed: %s\n",
                  esp_err_to_name(err));
        heap_caps_free(dbus);
        return NULL;
    }

    err = esp_mmu_map(paddr, map_size, target,
                      MMU_MEM_CAP_EXEC | MMU_MEM_CAP_READ | MMU_MEM_CAP_32BIT,
                      ESP_MMU_MMAP_FLAG_PADDR_SHARED, &ibus);
    if (err != ESP_OK) {
        os_printf("os_mmap: esp_mmu_map of %u bytes for AOT code failed: "
                  "%s\n",
                  (uint32)map_size, esp_err_to_name(err));
        heap_caps_free(dbus);
        return NULL;
    }

    portENTER_CRITICAL(&s_spinlock);
    for (i = 0; i < EXEC_MAP_MAX; i++) {
        if (!exec_maps[i].ibus) {
            exec_maps[i].ibus = ibus;
            exec_maps[i].dbus = dbus;
            exec_maps[i].size = map_size;
            break;
        }
    }
    portEXIT_CRITICAL(&s_spinlock);

    if (i == EXEC_MAP_MAX) {
        os_printf("os_mmap: more than %d AOT code blocks are mapped\n",
                  EXEC_MAP_MAX);
        esp_mmu_unmap(ibus);
        heap_caps_free(dbus);
        return NULL;
    }

    memset(dbus, 0, map_size);
    return ibus;
}

static bool
exec_munmap(void *ibus)
{
    uint8 *dbus = NULL;
    int i;

    portENTER_CRITICAL(&s_spinlock);
    for (i = 0; i < EXEC_MAP_MAX; i++) {
        if (exec_maps[i].ibus == ibus) {
            dbus = exec_maps[i].dbus;
            exec_maps[i].ibus = NULL;
            break;
        }
    }
    portEXIT_CRITICAL(&s_spinlock);

    if (!dbus) {
        return false;
    }
    esp_mmu_unmap(ibus);
    heap_caps_free(dbus);
    return true;
}
#endif

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    if (prot & MMAP_PROT_EXEC) {
#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
        return exec_mmap(size);
#else
        uint32_t mem_caps = MALLOC_CAP_EXEC;

        // Memory allocation with MALLOC_CAP_EXEC will return 4-byte aligned
        // Reserve extra 4 byte to fixup alignment and size for the pointer to
        // the originally allocated address
        void *buf_origin =
            heap_caps_malloc(size + 4 + sizeof(uintptr_t), mem_caps);
        if (!buf_origin) {
            return NULL;
        }
        void *buf_fixed = buf_origin + sizeof(void *);
        if ((uintptr_t)buf_fixed & (uintptr_t)0x7) {
            buf_fixed = (void *)((uintptr_t)(buf_fixed + 4) & (~(uintptr_t)7));
        }

        uintptr_t *addr_field = buf_fixed - sizeof(uintptr_t);
        *addr_field = (uintptr_t)buf_origin;
        memset(buf_fixed, 0, size);
        return buf_fixed;
#endif
    }
    else {
#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
        uint32_t mem_caps = MALLOC_CAP_SPIRAM;
#else
        uint32_t mem_caps = MALLOC_CAP_8BIT;
#endif
        void *buf_origin =
            heap_caps_malloc(size + 4 + sizeof(uintptr_t), mem_caps);
        if (!buf_origin) {
            return NULL;
        }

        // Memory allocation with MALLOC_CAP_SPIRAM or MALLOC_CAP_8BIT will
        // return 4-byte aligned Reserve extra 4 byte to fixup alignment and
        // size for the pointer to the originally allocated address
        void *buf_fixed = buf_origin + sizeof(void *);
        if ((uintptr_t)buf_fixed & (uintptr_t)0x7) {
            buf_fixed = (void *)((uintptr_t)(buf_fixed + 4) & (~(uintptr_t)7));
        }

        uintptr_t *addr_field = buf_fixed - sizeof(uintptr_t);
        *addr_field = (uintptr_t)buf_origin;

        memset(buf_fixed, 0, size);
        return buf_fixed;
    }
}

void *
os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    return os_mremap_slow(old_addr, old_size, new_size);
}

void
os_munmap(void *addr, size_t size)
{
    char *ptr = (char *)addr;

#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
    if (exec_munmap(ptr)) {
        return;
    }
#endif
    // We don't need special handling of the executable allocations
    // here, free() of esp-idf handles it properly
    return os_free(ptr);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    return 0;
}

void
#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
    IRAM_ATTR
#endif
    os_dcache_flush()
{
#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
    uint32_t preload;
    extern void Cache_WriteBack_All(void);

    portENTER_CRITICAL(&s_spinlock);

    Cache_WriteBack_All();
    preload = Cache_Disable_ICache();
    Cache_Enable_ICache(preload);

    portEXIT_CRITICAL(&s_spinlock);
#endif
}

void
os_icache_flush(void *start, size_t len)
{
}

#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
void *
os_get_dbus_mirror(void *ibus)
{
    void *dbus = ibus;
    int i;

    portENTER_CRITICAL(&s_spinlock);
    for (i = 0; i < EXEC_MAP_MAX; i++) {
        if (exec_maps[i].ibus && (uint8 *)ibus >= exec_maps[i].ibus
            && (uint8 *)ibus < exec_maps[i].ibus + exec_maps[i].size) {
            dbus = exec_maps[i].dbus + ((uint8 *)ibus - exec_maps[i].ibus);
            break;
        }
    }
    portEXIT_CRITICAL(&s_spinlock);
    return dbus;
}
#endif
