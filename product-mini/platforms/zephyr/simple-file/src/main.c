/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

// #include <autoconf.h>

#include <stdlib.h>
#include <string.h>
#include "bh_platform.h"
#include "bh_assert.h"
#include "bh_log.h"
#include "wasm_export.h"
#include "file.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>
#include <unistd.h>

#define CONFIG_HEAP_MEM_POOL_SIZE WASM_GLOBAL_HEAP_SIZE
#define CONFIG_APP_STACK_SIZE 16384
#define CONFIG_APP_HEAP_SIZE 16384

LOG_MODULE_REGISTER(main);

/* Exit codes of the sample, see ../README.md */
#define EXIT_OK 0
#define EXIT_HOST 1
#define EXIT_WASM 2

static char global_heap_buf[CONFIG_HEAP_MEM_POOL_SIZE] = { 0 };

static int app_argc;
static char **app_argv;

//-------------------------------------------------------------------------------------------//
static int
littlefs_flash_erase(unsigned int id)
{
    const struct flash_area *pfa;
    int rc;

    rc = flash_area_open(id, &pfa);
    if (rc < 0) {
        LOG_ERR("ERROR: unable to find flash area %u: %d\n", id, rc);
        return rc;
    }

    LOG_PRINTK("Area %u at 0x%x on %s for %u bytes\n", id,
               (unsigned int)pfa->fa_off, pfa->fa_dev->name,
               (unsigned int)pfa->fa_size);

    /* Optional wipe flash contents */
    if (IS_ENABLED(CONFIG_APP_WIPE_STORAGE)) {
        rc = flash_area_erase(pfa, 0, pfa->fa_size);
        LOG_ERR("Erasing flash area ... %d", rc);
    }

    flash_area_close(pfa);
    return rc;
}
#define PARTITION_NODE DT_NODELABEL(lfs1)

#if DT_NODE_EXISTS(PARTITION_NODE)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);
#else  /* PARTITION_NODE */
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = "/lfs",
};
#endif /* PARTITION_NODE */

struct fs_mount_t *mountpoint =
#if DT_NODE_EXISTS(PARTITION_NODE)
    &FS_FSTAB_ENTRY(PARTITION_NODE)
#else
    &lfs_storage_mnt
#endif
    ;

static int
littlefs_mount(struct fs_mount_t *mp)
{
    int rc;

    rc = littlefs_flash_erase((uintptr_t)mp->storage_dev);
    if (rc < 0) {
        return rc;
    }

    /* Do not mount if auto-mount has been enabled */
#if !DT_NODE_EXISTS(PARTITION_NODE) \
    || !(FSTAB_ENTRY_DT_MOUNT_FLAGS(PARTITION_NODE) & FS_MOUNT_FLAG_AUTOMOUNT)
    rc = fs_mount(mp);
    if (rc < 0) {
        LOG_PRINTK("ERROR: mount id %" PRIuPTR " at %s: %d\n",
                   (uintptr_t)mp->storage_dev, mp->mnt_point, rc);
        return rc;
    }
    LOG_PRINTK("%s mount: %d\n", mp->mnt_point, rc);
#else
    LOG_PRINTK("%s automounted\n", mp->mnt_point);
#endif

    return 0;
}

//-------------------------------------------------------------------------------------------//
int
main(void)
{
    int start, end;
    start = k_uptime_get_32();
    uint8 *wasm_file_buf = NULL;
    uint32 wasm_file_size;
    wasm_module_t wasm_module = NULL;
    wasm_module_inst_t wasm_module_inst = NULL;
    RuntimeInitArgs init_args;
    char error_buf[128];
    const char *exception;
    int rc;
    /* everything but a fully verified run is a failure */
    int ret = EXIT_HOST;

    int log_verbose_level = 2;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));

    rc = littlefs_mount(mountpoint);
    if (rc < 0) {
        LOG_ERR("ERROR: mounting %s: %d\n", mountpoint->mnt_point, rc);
        return ret;
    }

#if WASM_ENABLE_GLOBAL_HEAP_POOL != 0
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
    LOG_INF("global heap size: %d", sizeof(global_heap_buf));
#else
#error "memory allocation scheme is not defined."
#endif

    /* initialize runtime environment */
    if (!wasm_runtime_full_init(&init_args)) {
        LOG_ERR("ERROR: init runtime environment failed");
        return ret;
    }

    /* load WASM byte buffer from byte buffer of include file */
    wasm_file_buf = (uint8 *)wasm_test_file;
    wasm_file_size = sizeof(wasm_test_file);
    LOG_INF("Wasm file size: %d", wasm_file_size);

    /* load WASM module */
    if (!(wasm_module = wasm_runtime_load(wasm_file_buf, wasm_file_size,
                                          error_buf, sizeof(error_buf)))) {
        LOG_ERR("ERROR: failed to load module: %s", error_buf);
        goto fail1;
    }

    /* Set the WASI context */
#if WASM_ENABLE_LIBC_WASI != 0
#define DIR_LIST_SIZE 1
    const char *dir_list[DIR_LIST_SIZE] = {
        "/lfs",
    };
    /* No dir list => No file system
     * dir_cont = 0
     * No mapped dir list => No file system
     * map_dir_cont = 0
     * No environment variables
     * env_count = 0
     * No command line arguments
     * argv  0
     */
    wasm_runtime_set_wasi_args(wasm_module, dir_list, DIR_LIST_SIZE, NULL, 0,
                               NULL, 0, NULL, 0);
#endif

    /* instantiate the module */
    if (!(wasm_module_inst = wasm_runtime_instantiate(
              wasm_module, CONFIG_APP_STACK_SIZE, CONFIG_APP_HEAP_SIZE,
              error_buf, sizeof(error_buf)))) {
        LOG_ERR("ERROR: failed to instantiate module: %s", error_buf);
        goto fail2;
    }

    /* invoke the main function */
    if (!wasm_runtime_lookup_function(wasm_module_inst, "_start")
        && !wasm_runtime_lookup_function(wasm_module_inst, "__main_argc_argv")
        && !wasm_runtime_lookup_function(wasm_module_inst, "main")) {
        LOG_ERR("ERROR: failed to lookup function main");
        goto fail3;
    }

    LOG_INF("main found");
    if (!wasm_application_execute_main(wasm_module_inst, 0, NULL)) {
        LOG_ERR("ERROR: failed to execute main");
        ret = EXIT_WASM;
        goto fail3;
    }
    LOG_INF("main executed");

    if ((exception = wasm_runtime_get_exception(wasm_module_inst))) {
        LOG_ERR("ERROR: exception: %s", exception);
        ret = EXIT_WASM;
        goto fail3;
    }

    /* The module reports what went wrong through its exit code, see
       wasm-apps/file.c: 0 means every file operation was verified. */
    rc = wasm_runtime_get_wasi_exit_code(wasm_module_inst);
    LOG_INF("wasi exit code: %d", rc);
    if (rc != 0) {
        LOG_ERR("ERROR: the file operations reported code %d", rc);
        ret = EXIT_WASM;
        goto fail3;
    }

    LOG_INF("PASS: the file was written, read back and removed");
    ret = EXIT_OK;

fail3:
    /* destroy the module instance */
    wasm_runtime_deinstantiate(wasm_module_inst);

fail2:
    /* unload the module */
    wasm_runtime_unload(wasm_module);

fail1:
    /* destroy runtime environment */
    wasm_runtime_destroy();

    end = k_uptime_get_32();

    LOG_INF("elapsed: %dms", (end - start));

    return ret;
}