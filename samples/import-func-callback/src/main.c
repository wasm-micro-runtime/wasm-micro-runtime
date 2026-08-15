/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_export.h"
#include "bh_read_file.h"
#include "bh_getopt.h"
#include "assert.h"

typedef void (*wasm_func_type_callback_t)(const wasm_import_t *import_type);

const char *import_func_names[] = { "import_func1", "import_func2" };

void
import_func_type_callback(const wasm_import_t *import_type)
{
    int ret = 0;
    for (uint32_t i = 0;
         i < sizeof(import_func_names) / sizeof(import_func_names[0]); i++) {
        if (strcmp(import_type->name, import_func_names[i]) == 0) {
            ret = 1;
            break;
        }
    }
    assert(ret == 1);
    return;
}

/* Iterate over all import functions in the module */
void
wasm_runtime_for_each_import_func(const wasm_module_t module,
                                  wasm_func_type_callback_t callback)
{
    int32_t import_count = wasm_runtime_get_import_count(module);
    if (import_count <= 0)
        return;
    if (callback == NULL)
        return;

    for (int32_t i = 0; i < import_count; ++i) {
        wasm_import_t import_type;
        wasm_runtime_get_import_type(module, i, &import_type);

        if (import_type.kind != WASM_IMPORT_EXPORT_KIND_FUNC) {
            continue;
        }

        callback(&import_type);
    }
}

int
main(int argc, char *argv_main[])
{
    static char global_heap_buf[512 * 1024];
    wasm_module_t module = NULL;
    uint32 buf_size;
    char *buffer = NULL;
    const char *wasm_path = "wasm-apps/test.wasm";
    char error_buf[128];
    int ret = 0;

    RuntimeInitArgs init_args;
    memset(&init_args, 0, sizeof(RuntimeInitArgs));

    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);

    if (!wasm_runtime_full_init(&init_args)) {
        printf("Init runtime environment failed.\n");
        return -1;
    }
    buffer = bh_read_file_to_buffer(wasm_path, &buf_size);

    if (!buffer) {
        printf("Open wasm app file [%s] failed.\n", wasm_path);
        ret = -1;
        goto fail;
    }

    module = wasm_runtime_load((uint8 *)buffer, buf_size, error_buf,
                               sizeof(error_buf));
    if (!module) {
        printf("Load wasm app file [%s] failed.\n", wasm_path);
        ret = -1;
        goto fail;
    }

    wasm_runtime_for_each_import_func(module, import_func_type_callback);

fail:
    if (module)
        wasm_runtime_unload(module);
    if (buffer)
        BH_FREE(buffer);
    wasm_runtime_destroy();
    return ret;
}
