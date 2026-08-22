/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_clocks_wrapper.h"
#include "wasi_p2_io.h"
#include "component-model/wasm_component_host_resource.h"
#include "wasm_runtime_common.h"
#include "../../../product-mini/platforms/common/libc_wasi.h"
#include "component-model/wasm_canonical_abi.h"
#include "wasi_p2_common.h"
#include "component-model/wasm_component_canonical.h"
/**
 * @brief Wrapper for the `now` function of the `wasi:clocks/wall-clock`
 * interface.
 * @details This function retrieves the current wall-clock time and writes it
 *          back to the WebAssembly guest's memory.
 * @param exec_env The execution environment.
 * @param offset_addr Memory's offset for the built and stored result
 * `wasi_datetime_t`
 */
void
wasi_wall_clock_now_wrapper(wasm_exec_env_t exec_env, uint32_t offset_addr)
{

    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_datetime(0, 0);
        goto end;
    }

    wasi_datetime_t now = wasi_wall_clock_now();

    result = get_datetime(now.seconds, now.nanoseconds);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
}

/**
 * @brief Wrapper for the `resolution` function of the `wasi:clocks/wall-clock`
 * interface.
 * @details This function retrieves the wall-clock's resolution and writes it
 *          back to the WebAssembly guest's memory.
 * @param exec_env The execution environment.
 * @param offset_addr Memory's offset for the built and stored result
 * `wasi_datetime_t`
 */
void
wasi_wall_clock_resolution_wrapper(wasm_exec_env_t exec_env,
                                   uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_datetime(0, 0);
        goto end;
    }

    wasi_datetime_t resolution = wasi_wall_clock_resolution();

    result = get_datetime(resolution.seconds, resolution.nanoseconds);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
}

/**
 * @brief Wrapper for the `now` function of the `wasi:clocks/monotonic-clock`
 * interface.
 * @param exec_env The execution environment.
 * @return The current value of the monotonic clock as a `wasi_instant_t`.
 */
uint64_t
wasi_monotonic_clock_now_wrapper(wasm_exec_env_t exec_env)
{
    return wasi_monotonic_clock_now();
}

/**
 * @brief Wrapper for the `resolution` function of the
 * `wasi:clocks/monotonic-clock` interface.
 * @param exec_env The execution environment.
 * @return The resolution of the monotonic clock as a `wasi_duration_t`.
 */
uint64_t
wasi_monotonic_clock_resolution_wrapper(wasm_exec_env_t exec_env)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        return 0;
    }

    return wasi_monotonic_clock_resolution();
}

/**
 * @brief Wrapper for the `subscribe-instant` function of the
 * `wasi:clocks/monotonic-clock` interface.
 * @param exec_env The execution environment.
 * @param when The absolute time (`wasi_instant_t`) at which the pollable should
 * resolve.
 * @return The pollable resource rep (index).
 */
int32_t
wasi_monotonic_clock_subscribe_instant_wrapper(wasm_exec_env_t exec_env,
                                               int64_t when)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        return 0;
    }

    wasi_pollable_context_t pollable =
        wasi_monotonic_clock_subscribe_instant(when);
    if (pollable.fd < 0) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create pollable timer");
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr =
        host_resource_create(WASI_P2_POLLABLE, sizeof(wasi_pollable_context_t));

    if (!hr) {
        if (pollable.own_fd)
            close(pollable.fd);
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get pollable resource");
        return 0;
    }

    *((wasi_pollable_context_t *)hr->data) = pollable;
    host_resource_set_dtor(hr, pollable_dtor);

    uint32_t out = host_resource_table_add(hr_table, hr);
    if (out < 1) {
        destroy_host_resource(hr); // Clean up the HostResource on failure
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(out);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &out);

    free_wit_value(out_val);

    return (int32_t)out;
}

/**
 * @brief Wrapper for the `subscribe-duration` function of the
 * `wasi:clocks/monotonic-clock` interface.
 * @param exec_env The execution environment.
 * @param when The relative duration (`wasi_duration_t`) after which the
 * pollable should resolve.
 * @return The pollable resource rep (index).
 */
int32_t
wasi_monotonic_clock_subscribe_duration_wrapper(wasm_exec_env_t exec_env,
                                                int64_t when)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        return 0;
    }

    wasi_pollable_context_t pollable =
        wasi_monotonic_clock_subscribe_duration(when);
    if (pollable.fd < 0) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create pollable timer");
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr =
        host_resource_create(WASI_P2_POLLABLE, sizeof(wasi_pollable_context_t));

    if (!hr) {
        if (pollable.own_fd)
            close(pollable.fd);
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get pollable resource");
        return 0;
    }

    *((wasi_pollable_context_t *)hr->data) = pollable;
    host_resource_set_dtor(hr, pollable_dtor);

    uint32_t out = host_resource_table_add(hr_table, hr);
    if (out < 1) {
        destroy_host_resource(hr); // Clean up the HostResource on failure
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(out);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &out);

    free_wit_value(out_val);

    return (int32_t)out;
}
