/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_random_wrapper.h"
#include "wasi_p2_common.h"
#include "wasm_runtime_common.h"
#include "../../../product-mini/platforms/common/libc_wasi.h"
#include "component-model/wasm_canonical_abi.h"
#include "component-model/wasm_component_canonical.h"
#include <string.h>

/**
 * @brief Wrapper for the `get-random-bytes` function of the
 * `wasi:random/random` interface.
 * @details This function retrieves cryptographically-secure random bytes and
 *          copies them into the WebAssembly guest's memory.
 * @param exec_env The execution environment.
 * @param len The number of random bytes to generate.
 * @param[out] offset_addr The address where that will be populated with the
 * resulting byte array
 */
void
wasi_random_get_random_bytes_wrapper(wasm_exec_env_t exec_env, uint64_t len,
                                     uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t result = NULL;
    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

    wasi_list_u8_t list;
    wit_value_t *aux_list = NULL;

    wasi_random_get_random_bytes(len, &list);

    if (list.buf_len > 0) {
        aux_list = (wit_value_t *)wasm_runtime_malloc(list.buf_len
                                                      * sizeof(wit_value_t));
        if (!aux_list) {
            result = wit_list_ctor(NULL, 0);
            goto end;
        }
        for (uint32_t index = 0; index < list.buf_len; index++) {
            wit_value_t elem = wit_u8_ctor(list.buf[index]);
            aux_list[index] = elem;
        }
        result = wit_list_ctor(aux_list, list.buf_len);
    }
    else {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    wasm_runtime_free(list.buf);
    list.buf = NULL;
    free_wit_value(result);
}

/**
 * @brief Wrapper for the `get-random-u64` function of the `wasi:random/random`
 * interface.
 * @param exec_env The execution environment.
 * @return A cryptographically-secure random 64-bit unsigned integer.
 */
uint64_t
wasi_random_get_random_u64_wrapper(wasm_exec_env_t exec_env)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        return 0;
    }
    return wasi_random_get_random_u64();
}

/**
 * @brief Wrapper for the `get-insecure-random-bytes` function of the
 * `wasi:random/insecure` interface.
 * @details This function retrieves insecure random bytes and copies them into
 *          the WebAssembly guest's memory.
 * @param exec_env The execution environment.
 * @param len The number of random bytes to generate.
 * @param[out] offset_addr The address where that will be populated with the
 * resulting byte array
 */
void
wasi_random_get_insecure_random_bytes_wrapper(wasm_exec_env_t exec_env,
                                              uint64_t len,
                                              uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

    wasi_list_u8_t list;
    wit_value_t *aux_list = NULL;

    wasi_random_get_insecure_random_bytes(len, &list);

    if (list.buf_len > 0) {
        aux_list = (wit_value_t *)wasm_runtime_malloc(list.buf_len
                                                      * sizeof(wit_value_t));
        if (!aux_list) {
            result = wit_list_ctor(NULL, 0);
            goto end;
        }
        for (uint32_t index = 0; index < list.buf_len; index++) {
            wit_value_t elem = wit_u8_ctor(list.buf[index]);
            aux_list[index] = elem;
        }
        result = wit_list_ctor(aux_list, list.buf_len);
    }
    else {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    wasm_runtime_free(list.buf);
    list.buf = NULL;
    free_wit_value(result);
}

/**
 * @brief Wrapper for the `get-insecure-random-u64` function of the
 * `wasi:random/insecure` interface.
 * @param exec_env The execution environment.
 * @return An insecure random 64-bit unsigned integer.
 */
uint64_t
wasi_random_get_insecure_random_u64_wrapper(wasm_exec_env_t exec_env)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        return 0;
    }
    return wasi_random_get_insecure_random_u64();
}

/**
 * @brief Wrapper for the `insecure-seed` function of the
 `wasi:random/insecure-seed` interface.
 * @details This function retrieves a new seed for the insecure random number
 *          generator and writes it back to the WebAssembly guest's memory.
 * @param exec_env The execution environment.
 @param[out] offset_addr The address where the resulting
 *                        tuple of two `u64` values will be written.
 */
void
wasi_random_insecure_seed_wrapper(wasm_exec_env_t exec_env,
                                  uint32_t offset_addr)
{

    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t result = NULL;
    wit_value_t *aux_tuple = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = wit_tuple_ctor(NULL, 0);
        goto end;
    }
    aux_tuple = (wit_value_t *)wasm_runtime_malloc(2 * sizeof(wit_value_t));
    if (!aux_tuple) {
        result = wit_tuple_ctor(NULL, 0);
        goto end;
    }

    uint64_t seed1 = 0, seed2 = 0;
    wasi_random_get_insecure_seed(&seed1, &seed2);
    aux_tuple[0] = wit_u64_ctor(seed1);
    aux_tuple[1] = wit_u64_ctor(seed2);
    result = wit_tuple_ctor(aux_tuple, 2);
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
}