/*
 * Copyright (C) 2026 KylinSoft.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#define CONFIGURE_INIT
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_MAXIMUM_TASKS 16
#define CONFIGURE_MAXIMUM_POSIX_THREADS 16
#define CONFIGURE_MAXIMUM_POSIX_KEYS 16
#define CONFIGURE_MAXIMUM_POSIX_KEY_VALUE_PAIRS 16
#define CONFIGURE_MAXIMUM_POSIX_SEMAPHORES 16
#define CONFIGURE_MAXIMUM_SEMAPHORES 16
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES 0
#define CONFIGURE_MAXIMUM_PARTITIONS 0
#define CONFIGURE_MAXIMUM_REGIONS 0
#define CONFIGURE_MAXIMUM_TIMERS 4
#define CONFIGURE_MAXIMUM_FILE_DESCRIPTORS 32

#include <rtems.h>
#include <rtems/bspcmdline.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rtems/confdefs.h>

#include "bh_read_file.h"
#include "wasm_export.h"
#include "test_wasm.h"

#define STACK_SIZE (8 * 1024)
#define HEAP_SIZE  (8 * 1024)

#if WASM_ENABLE_LIBC_WASI != 0
#include "../common/libc_wasi.c"
#endif

#include "../common/wasm_proposal.c"

static char module_path[256];

static const char *
get_module_path(void)
{
    const char *cmdline = rtems_bsp_cmdline_get();

    if (cmdline == NULL || cmdline[0] == '\0')
        return NULL;

    if (sscanf(cmdline, "%255s", module_path) == 1)
        return module_path;

    return NULL;
}

rtems_task
Init(rtems_task_argument ignored)
{
    RuntimeInitArgs init_args = { 0 };
    wasm_module_t module = NULL;
    wasm_module_inst_t module_inst = NULL;
    uint8_t *wasm_file_buf = NULL;
    uint32 wasm_file_buf_size = 0;
    char *file_buffer = NULL;
    char error_buf[128] = { 0 };
    const char *path = get_module_path();

    (void)ignored;

    init_args.mem_alloc_type = Alloc_With_System_Allocator;

    if (!wasm_runtime_full_init(&init_args)) {
        os_printf("wasm_runtime_full_init failed\n");
        goto end;
    }

    if (path != NULL) {
        file_buffer = bh_read_file_to_buffer(path, &wasm_file_buf_size);
        if (file_buffer == NULL) {
            os_printf("failed to read %s\n", path);
            goto end;
        }
        wasm_file_buf = (uint8_t *)file_buffer;
    }
    else {
        wasm_file_buf = (uint8_t *)wasm_test_file;
        wasm_file_buf_size = sizeof(wasm_test_file);
    }

    module = wasm_runtime_load(wasm_file_buf, wasm_file_buf_size, error_buf,
                               sizeof(error_buf));
    if (module == NULL) {
        os_printf("wasm_runtime_load failed: %s\n", error_buf);
        goto end;
    }

    module_inst = wasm_runtime_instantiate(module, STACK_SIZE, HEAP_SIZE,
                                           error_buf, sizeof(error_buf));
    if (module_inst == NULL) {
        os_printf("wasm_runtime_instantiate failed: %s\n", error_buf);
        goto end;
    }

    wasm_application_execute_main(module_inst, 0, NULL);

end:
    if (module_inst != NULL)
        wasm_runtime_deinstantiate(module_inst);
    if (module != NULL)
        wasm_runtime_unload(module);
    if (file_buffer != NULL)
        BH_FREE(file_buffer);

    wasm_runtime_destroy();
    rtems_shutdown_executive(0);
}
