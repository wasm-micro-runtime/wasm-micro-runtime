/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "bh_platform.h"
#include "wasm_export.h"
#include "test_wasm.h"

#define CONFIG_GLOBAL_HEAP_BUF_SIZE WASM_GLOBAL_HEAP_SIZE
#define CONFIG_APP_STACK_SIZE 4096
#define CONFIG_APP_HEAP_SIZE 8192

#define EXIT_OK 0
#define EXIT_HOST 1
#define EXIT_WASM 2

int wamr_runtime_result = EXIT_HOST;

#if WASM_ENABLE_GLOBAL_HEAP_POOL != 0
static char global_heap_buf[CONFIG_GLOBAL_HEAP_BUF_SIZE] = { 0 };
#endif

static int
run_guest_main(wasm_module_inst_t module_inst)
{
    uint32_t argv[2] = { 0 };
    uint32_t param_count;
    uint32_t result_count;
    int module_ret = 0;
    wasm_exec_env_t exec_env;
    wasm_function_inst_t func;
    const char *exception;

    func = wasm_runtime_lookup_function(module_inst, "main");
    if (func == NULL) {
        func = wasm_runtime_lookup_function(module_inst, "__main_argc_argv");
    }
    if (func == NULL) {
        printf("ERROR: failed to lookup guest main\n");
        return EXIT_HOST;
    }

    exec_env = wasm_runtime_create_exec_env(module_inst, CONFIG_APP_STACK_SIZE);
    if (exec_env == NULL) {
        printf("ERROR: failed to create guest exec env\n");
        return EXIT_HOST;
    }

    param_count = wasm_func_get_param_count(func, module_inst);
    result_count = wasm_func_get_result_count(func, module_inst);
    if (!wasm_runtime_call_wasm(exec_env, func, param_count, argv)) {
        wasm_runtime_destroy_exec_env(exec_env);
        exception = wasm_runtime_get_exception(module_inst);
        if (exception != NULL) {
            printf("ERROR: exception: %s\n", exception);
            return EXIT_WASM;
        }
        printf("ERROR: failed to invoke guest main\n");
        return EXIT_HOST;
    }

    if (result_count > 0U) {
        module_ret = (int)argv[0];
    }

    wasm_runtime_destroy_exec_env(exec_env);

    exception = wasm_runtime_get_exception(module_inst);
    if (exception != NULL) {
        printf("ERROR: exception: %s\n", exception);
        return EXIT_WASM;
    }

    if (module_ret != 0) {
        printf("ERROR: the wasm module returned %d\n", module_ret);
        return EXIT_WASM;
    }

    return EXIT_OK;
}

void
wamr_runtime_main(void *arg1, void *arg2, void *arg3)
{
    uint32_t wasm_file_size = sizeof(wasm_test_file);
    uint8_t *wasm_file_buf = (uint8_t *)wasm_test_file;
    wasm_module_t module = NULL;
    wasm_module_inst_t module_inst = NULL;
    RuntimeInitArgs init_args;
    char error_buf[128] = { 0 };
    int32_t start;
    int32_t end;

    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    start = k_uptime_get_32();
    os_printf("User mode thread: start\n");
    wamr_runtime_result = EXIT_HOST;

    memset(&init_args, 0, sizeof(init_args));
#if WASM_ENABLE_GLOBAL_HEAP_POOL != 0
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
#else
#error "Zephyr user-mode sample requires the global heap pool"
#endif

    if (!wasm_runtime_full_init(&init_args)) {
        printf("ERROR: init runtime environment failed\n");
        goto out;
    }

    module = wasm_runtime_load(wasm_file_buf, wasm_file_size, error_buf,
                               sizeof(error_buf));
    if (module == NULL) {
        printf("ERROR: %s\n", error_buf);
        goto destroy_runtime;
    }

    module_inst = wasm_runtime_instantiate(module, CONFIG_APP_STACK_SIZE,
                                           CONFIG_APP_HEAP_SIZE, error_buf,
                                           sizeof(error_buf));
    if (module_inst == NULL) {
        printf("ERROR: %s\n", error_buf);
        goto unload_module;
    }

    wamr_runtime_result = run_guest_main(module_inst);
    if (wamr_runtime_result == EXIT_OK) {
        printf(
            "PASS: two guest pthread workers completed in Zephyr user mode\n");
    }

    wasm_runtime_deinstantiate(module_inst);
    module_inst = NULL;

unload_module:
    if (module != NULL) {
        wasm_runtime_unload(module);
        module = NULL;
    }

destroy_runtime:
    wasm_runtime_destroy();

out:
    end = k_uptime_get_32();
    os_printf("User mode thread: elapsed %d\n", (int)(end - start));
}
