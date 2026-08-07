/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdlib.h>
#include <string.h>
#include "bh_platform.h"
#include "bh_assert.h"
#include "bh_log.h"
#include "bh_queue.h"
#include "wasm_export.h"
/* Generated at build time from ../wasm-app/main.c, see ./CMakeLists.txt */
#include "test_wasm.h"

#if defined(BUILD_TARGET_RISCV64_LP64) || defined(BUILD_TARGET_RISCV32_ILP32)
#define CONFIG_GLOBAL_HEAP_BUF_SIZE 5120
#define CONFIG_APP_STACK_SIZE 512
#define CONFIG_APP_HEAP_SIZE 512
#else /* else of BUILD_TARGET_RISCV64_LP64 || BUILD_TARGET_RISCV32_ILP32 */
#define CONFIG_GLOBAL_HEAP_BUF_SIZE WASM_GLOBAL_HEAP_SIZE
#define CONFIG_APP_STACK_SIZE 8192
#define CONFIG_APP_HEAP_SIZE 8192
#endif /* end of BUILD_TARGET_RISCV64_LP64 || BUILD_TARGET_RISCV32_ILP32 */

/* Exit codes of the sample, see ../README.md */
#define EXIT_OK 0
#define EXIT_HOST 1
#define EXIT_WASM 2

/* Result of the user-mode thread, read by main() once it has joined */
int iwasm_result = EXIT_HOST;

/**
 * Look up the entry point of the module, call it and report what the module
 * returned.
 *
 * @param module_inst the WASM module instance
 *
 * @return EXIT_OK when the module ran to completion and returned 0,
 *         EXIT_WASM when it faulted or returned non-zero,
 *         EXIT_HOST when the runtime could not call it at all.
 */
static int
app_instance_main(wasm_module_inst_t module_inst)
{
    const char *exception;
    wasm_function_inst_t func;
    wasm_exec_env_t exec_env;
    uint32 argv[2] = { 0 };
    uint32 param_count, result_count;
    int module_ret;

    if (!(func = wasm_runtime_lookup_function(module_inst, "main"))
        && !(func =
                 wasm_runtime_lookup_function(module_inst, "__main_argc_argv"))
        && !(func = wasm_runtime_lookup_function(module_inst, "app_main"))) {
        os_printf("ERROR: failed to lookup function main or app_main\n");
        return EXIT_HOST;
    }

    if (!(exec_env = wasm_runtime_create_exec_env(module_inst,
                                                  CONFIG_APP_HEAP_SIZE))) {
        os_printf("ERROR: create exec env failed\n");
        return EXIT_HOST;
    }

    /* main() takes (argc, argv) and returns a status, app_main() takes and
       returns nothing; pass zeroed arguments either way */
    param_count = wasm_func_get_param_count(func, module_inst);
    result_count = wasm_func_get_result_count(func, module_inst);

    LOG_VERBOSE("Calling the module entry point\n");
    wasm_runtime_call_wasm(exec_env, func, param_count, argv);
    module_ret = result_count > 0 ? (int)argv[0] : 0;

    wasm_runtime_destroy_exec_env(exec_env);

    if ((exception = wasm_runtime_get_exception(module_inst))) {
        os_printf("ERROR: exception: %s\n", exception);
        return EXIT_WASM;
    }

    if (module_ret != 0) {
        os_printf("ERROR: the wasm module returned %d\n", module_ret);
        return EXIT_WASM;
    }

    return EXIT_OK;
}

#if WASM_ENABLE_GLOBAL_HEAP_POOL != 0
static char global_heap_buf[CONFIG_GLOBAL_HEAP_BUF_SIZE] = { 0 };
#endif

void
iwasm_main(void *arg1, void *arg2, void *arg3)
{
    int start, end;
    start = k_uptime_get_32();
    uint8 *wasm_file_buf = NULL;
    uint32 wasm_file_size;
    wasm_module_t wasm_module = NULL;
    wasm_module_inst_t wasm_module_inst = NULL;
    RuntimeInitArgs init_args;
    char error_buf[128];
#if WASM_ENABLE_LOG != 0
    int log_verbose_level = 2;
#endif

    (void)arg1;
    (void)arg2;
    (void)arg3;

    os_printf("User mode thread: start\n");

    memset(&init_args, 0, sizeof(RuntimeInitArgs));

#if WASM_ENABLE_GLOBAL_HEAP_POOL != 0
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
#elif (defined(CONFIG_COMMON_LIBC_MALLOC)            \
       && CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE != 0) \
    || defined(CONFIG_NEWLIB_LIBC)
    init_args.mem_alloc_type = Alloc_With_System_Allocator;
#else
#error "memory allocation scheme is not defined."
#endif

    /* initialize runtime environment */
    if (!wasm_runtime_full_init(&init_args)) {
        printf("ERROR: init runtime environment failed\n");
        return;
    }

    iwasm_result = EXIT_HOST;

#if WASM_ENABLE_LOG != 0
    bh_log_set_verbose_level(log_verbose_level);
#endif

    /* load WASM byte buffer from byte buffer of include file */
    wasm_file_buf = (uint8 *)wasm_test_file;
    wasm_file_size = sizeof(wasm_test_file);

    /* load WASM module */
    if (!(wasm_module = wasm_runtime_load(wasm_file_buf, wasm_file_size,
                                          error_buf, sizeof(error_buf)))) {
        printf("ERROR: %s\n", error_buf);
        goto fail1;
    }

    /* instantiate the module */
    if (!(wasm_module_inst = wasm_runtime_instantiate(
              wasm_module, CONFIG_APP_STACK_SIZE, CONFIG_APP_HEAP_SIZE,
              error_buf, sizeof(error_buf)))) {
        printf("ERROR: %s\n", error_buf);
        goto fail2;
    }

    /* invoke the main function */
    iwasm_result = app_instance_main(wasm_module_inst);
    if (iwasm_result == EXIT_OK)
        printf("PASS: the wasm module ran to completion in user mode\n");

    /* destroy the module instance */
    wasm_runtime_deinstantiate(wasm_module_inst);

fail2:
    /* unload the module */
    wasm_runtime_unload(wasm_module);

fail1:
    /* destroy runtime environment */
    wasm_runtime_destroy();

    end = k_uptime_get_32();

    os_printf("User mode thread: elapsed %d\n", (end - start));
}
