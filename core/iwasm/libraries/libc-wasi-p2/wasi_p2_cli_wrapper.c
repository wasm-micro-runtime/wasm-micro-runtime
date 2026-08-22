/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_cli_wrapper.h"
#include "wasi_p2_common.h"
#include "wasm_runtime_common.h"
#include "component-model/wasm_component_host_resource.h"
#include "component-model/wasm_component_canonical.h"
#include <string.h>
#include "../../../product-mini/platforms/common/libc_wasi.h"
#include "component-model/wasm_component_canonical.h"

// Helper function to covert environment variable list tuple to wit_value_t
static wit_value_t
get_environ_val(wasi_tuple_string_string_t *environ, uint32_t environ_count,
                wasm_exec_env_t exec_env)
{
    wit_value_t *elems =
        (wit_value_t *)wasm_runtime_malloc(2 * sizeof(wit_value_t));

    StringEncoding encoding = wasm_get_string_encoding(exec_env);
    uint8_t *encoded_str_key = NULL, *encoded_str_val = NULL;
    uint32_t encoded_str_len_key = 0, encoded_str_len_val = 0;
    uint32_t encoded_code_units_key = 0, encoded_code_units_val = 0;
    encode_string(exec_env->cx, environ->key, strlen(environ->key), encoding,
                  &encoded_str_key, &encoded_str_len_key,
                  &encoded_code_units_key);
    encode_string(exec_env->cx, environ->value, strlen(environ->value),
                  encoding, &encoded_str_val, &encoded_str_len_val,
                  &encoded_code_units_val);

    elems[0] = wit_string_ctor((char *)encoded_str_key, encoded_str_len_key,
                               encoded_code_units_key, encoding);
    elems[1] = wit_string_ctor((char *)encoded_str_val, encoded_str_len_val,
                               encoded_code_units_val, encoding);
    wit_value_t result_tuple = wit_tuple_ctor(elems, 2);
    return result_tuple;
}

static bool
is_wasi_env_variable(const char *host_var,
                     const wasi_tuple_string_string_t *wasi_vars,
                     uint32_t wasi_var_count)
{
    uint32_t idx = 0;

    for (idx = 0; idx < wasi_var_count; idx++) {
        if (!strcmp(host_var, wasi_vars[idx].key)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Wrapper for the `get-environment` function of the
 * `wasi:cli/environment` interface.
 * @details This function retrieves the environment variables and copies them
 *          into the WebAssembly guest's memory.
 * @param exec_env The execution environment.
 * @param[out] offset_addr Memory's offset where output is stored
 */
void
wasi_cli_get_environment_wrapper(wasm_exec_env_t exec_env, uint32_t offset_addr)
{
    WASMModuleInstanceCommon *module_inst =
        wasm_runtime_get_module_inst(exec_env);
    uint32_t host_environ_count = 0;
    wasi_tuple_string_string_t *host_environ = NULL;
    uint32_t wasi_environ_count = 0;
    wasi_tuple_string_string_t *wasi_environ = NULL;
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t result = NULL;
    wit_value_t *aux_list = NULL;
    uint32_t total_env_count = 0;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

    wasi_environ_count = wasi_ctx->argv_environ->environ_count;
    wasi_environ = wasi_cli_environment_split_str(
        wasi_ctx->argv_environ->environ_list, wasi_environ_count);

    if (wasi_ctx->wasi_options->inherit_env) {
        host_environ = wasi_cli_get_environment(&host_environ_count);
    }

    if (!host_environ && !wasi_environ) {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

    if (host_environ_count + wasi_environ_count > 0) {
        aux_list = (wit_value_t *)wasm_runtime_malloc(
            (host_environ_count + wasi_environ_count) * sizeof(wit_value_t));
        if (!aux_list) {
            result = wit_list_ctor(NULL, 0);
            goto end;
        }

        for (uint32_t index = 0; index < host_environ_count; index++) {
            if (is_wasi_env_variable(host_environ[index].key, wasi_environ,
                                     wasi_environ_count)) {
                continue;
            }
            aux_list[total_env_count] = get_environ_val(
                &host_environ[index], host_environ_count, exec_env);
            total_env_count++;
        }

        for (uint32_t index = 0; index < wasi_environ_count; index++) {
            aux_list[total_env_count] = get_environ_val(
                &wasi_environ[index], wasi_environ_count, exec_env);
            total_env_count++;
        }
        result = wit_list_ctor(aux_list, total_env_count);
    }
    else {
        result = wit_list_ctor(NULL, 0);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    if (host_environ) {
        for (uint32_t j = 0; j < host_environ_count; j++) {
            wasm_runtime_free(host_environ[j].key);
            wasm_runtime_free(host_environ[j].value);
        }
        wasm_runtime_free(host_environ);
    }
    if (wasi_environ) {
        for (uint32_t j = 0; j < wasi_environ_count; j++) {
            wasm_runtime_free(wasi_environ[j].key);
            wasm_runtime_free(wasi_environ[j].value);
        }
        wasm_runtime_free(wasi_environ);
    }

    free_wit_value(result);
}

/**
 * @brief Wrapper for the `get-arguments` function of the `wasi:cli/environment`
 * interface.
 * @details This function retrieves the command-line arguments and copies them
 *          into the WebAssembly guest's memory.
 * @param exec_env The execution environment.
 * @param[out] offset_addr Memory's offset where output is stored
 */
void
wasi_cli_get_arguments_wrapper(wasm_exec_env_t exec_env, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    uint32_t argc = 0;
    char **argv = NULL;
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    wit_value_t result = NULL;
    wit_value_t *aux_list = NULL;

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

    argc = wasi_ctx->argv_environ->argc;
    argv = wasi_ctx->argv_environ->argv_list;

    if (!argv) {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

    if (argc > 0) {
        aux_list =
            (wit_value_t *)wasm_runtime_malloc(argc * sizeof(wit_value_t));
        if (!aux_list) {
            result = wit_list_ctor(NULL, 0);
            goto end;
        }

        StringEncoding encoding = wasm_get_string_encoding(exec_env);
        for (uint32_t index = 0; index < argc; index++) {
            uint8_t *encoded_str = NULL;
            uint32_t encoded_str_len = 0;
            uint32_t encoded_code_units = 0;
            encode_string(exec_env->cx, argv[index], strlen(argv[index]),
                          encoding, &encoded_str, &encoded_str_len,
                          &encoded_code_units);
            aux_list[index] =
                wit_string_ctor((char *)encoded_str, encoded_str_len,
                                encoded_code_units, encoding);
        }
        result = wit_list_ctor(aux_list, argc);
    }
    else {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
}

/**
 * @brief Wrapper for the `initial-cwd` function of the `wasi:cli/environment`
 * interface.
 * @param exec_env The execution environment.
 * @param[out] offset_addr Memory's offset where output is stored
 */
void
wasi_cli_initial_cwd_wrapper(wasm_exec_env_t exec_env, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    wit_value_t optional_result = NULL;
    char *cwd = NULL;

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    bool is_some;
    cwd = wasi_cli_initial_cwd(&is_some);

    if (!cwd || !is_some) {
        optional_result = wit_option_ctor(NULL);
        goto end;
    }
    StringEncoding encoding = wasm_get_string_encoding(exec_env);
    uint8_t *encoded_str = NULL;
    uint32_t encoded_str_len = 0;
    uint32_t encoded_code_units = 0;
    encode_string(exec_env->cx, cwd, strlen(cwd), encoding, &encoded_str,
                  &encoded_str_len, &encoded_code_units);
    wit_value_t string_result = wit_string_ctor(
        (char *)encoded_str, encoded_str_len, encoded_code_units, encoding);
    optional_result = wit_option_ctor(string_result);

end:
    store(exec_env->cx, offset_addr, func_type->results->result,
          optional_result);
    if (cwd)
        wasm_runtime_free(cwd);
}

/**
 * @brief Wrapper for the `exit` function of the `wasi:cli/exit` interface.
 * @param exec_env The execution environment.
 * @param status A result indicating success (0) or error (1).
 */
void
wasi_cli_exit_wrapper(wasm_exec_env_t exec_env, int32_t status)
{
    wasi_cli_exit(status);
}

/**
 * @brief Wrapper for the `get-stdin` function of the `wasi:cli/stdin`
 * interface.
 * @param exec_env The execution environment.
 * @return The resource handle for stdin.
 */
int32_t
wasi_cli_get_stdin_wrapper(wasm_exec_env_t exec_env)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        return 0;
    }

    wasi_input_stream_t _stdin = wasi_cli_get_stdin();

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_create(WASI_P2_IO_INPUT_STREAM,
                                            sizeof(StreamResourceType));

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create stdin resource");
        return 0;
    }

    ((StreamResourceType *)hr->data)->fd = _stdin;
    ((StreamResourceType *)hr->data)->type = STREAM_TYPE_GENERIC;

    uint32_t index_rep = host_resource_table_add(hr_table, hr);
    if (index_rep < 1) {
        destroy_host_resource(hr); // Clean up the HostResource on failure
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not add stdin resource to HR table");
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(index_rep);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &index_rep);
    free_wit_value(out_val);

    return (int32_t)index_rep;
}

/**
 * @brief Wrapper for the `get-stdout` function of the `wasi:cli/stdout`
 * interface.
 * @param exec_env The execution environment.
 * @return The resource handle for stdout.
 */
int32_t
wasi_cli_get_stdout_wrapper(wasm_exec_env_t exec_env)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        return 0;
    }

    wasi_output_stream_t _stdout = wasi_cli_get_stdout();

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_create(WASI_P2_IO_OUTPUT_STREAM,
                                            sizeof(StreamResourceType));

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create stdout resource");
        return 0;
    }

    ((StreamResourceType *)hr->data)->fd = _stdout;
    ((StreamResourceType *)hr->data)->type = STREAM_TYPE_GENERIC;

    uint32_t index_rep = host_resource_table_add(hr_table, hr);
    if (index_rep < 1) {
        destroy_host_resource(hr); // Clean up the HostResource on failure
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not add stdout resource to HR table");
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(index_rep);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &index_rep);
    free_wit_value(out_val);

    return (int32_t)index_rep;
}

/**
 * @brief Wrapper for the `get-stderr` function of the `wasi:cli/stderr`
 * interface.
 * @param exec_env The execution environment.
 * @return The resource handle for stderr.
 */
int32_t
wasi_cli_get_stderr_wrapper(wasm_exec_env_t exec_env)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        return 0;
    }

    wasi_output_stream_t _stderr = wasi_cli_get_stderr();

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_create(WASI_P2_IO_OUTPUT_STREAM,
                                            sizeof(StreamResourceType));

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create stderr resource");
        return 0;
    }

    ((StreamResourceType *)hr->data)->fd = _stderr;
    ((StreamResourceType *)hr->data)->type = STREAM_TYPE_GENERIC;

    uint32_t index_rep = host_resource_table_add(hr_table, hr);
    if (index_rep < 1) {
        destroy_host_resource(hr); // Clean up the HostResource on failure
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not add stderr resource to HR table");
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(index_rep);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &index_rep);
    free_wit_value(out_val);

    return (int32_t)index_rep;
}

/**
 * @brief Wrapper for the `get-terminal-stdin` function of the
 * `wasi:cli/terminal-stdin` interface.
 * @param exec_env The execution environment.
 * @param[out] offset_addr Memory's offset for stored terminal input handle or
 * an empty value.
 */
void
wasi_cli_get_terminal_stdin_wrapper(wasm_exec_env_t exec_env,
                                    uint32_t offset_addr)
{
    bool is_some;
    wit_value_t optional_result = NULL;
    wasi_terminal_input_t handle = wasi_cli_get_terminal_stdin(&is_some);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!is_some) {
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        optional_result = wit_option_ctor(NULL);
        goto end;
    }
    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr =
        host_resource_create(WASI_P2_TERMINAL_INPUT, sizeof(handle));

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create terminal input resource");
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    *((wasi_terminal_input_t *)hr->data) = handle;

    uint32_t index_rep = host_resource_table_add(hr_table, hr);
    if (index_rep < 1) {
        destroy_host_resource(hr); // Clean up the HostResource on failure
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not add terminal input resource to HR table");
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    wit_value_t wrapped_index_rep = wit_u32_ctor(index_rep);
    optional_result = wit_option_ctor(wrapped_index_rep);

end:
    store(exec_env->cx, offset_addr, func_type->results->result,
          optional_result);
    free_wit_value(optional_result);
}

/**
 * @brief Wrapper for the `get-terminal-stdout` function of the
 * `wasi:cli/terminal-stdout` interface.
 * @param exec_env The execution environment.
 * @param[out] offset_addr Memory's offset for stored terminal output handle or
 * an empty value.
 */
void
wasi_cli_get_terminal_stdout_wrapper(wasm_exec_env_t exec_env,
                                     uint32_t offset_addr)
{
    bool is_some;
    wit_value_t optional_result = NULL;

    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    wasi_terminal_output_t handle = wasi_cli_get_terminal_stdout(&is_some);
    if (!is_some) {
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr =
        host_resource_create(WASI_P2_TERMINAL_OUTPUT, sizeof(handle));

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create terminal output resource");
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    *((wasi_terminal_output_t *)hr->data) = handle;

    int32_t index_rep = host_resource_table_add(hr_table, hr);
    if (index_rep < 1) {
        LOG_VERBOSE("NO index");
        destroy_host_resource(hr); // Clean up the HostResource on failure
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not add terminal output resource to HR table");
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    wit_value_t wrapped_index_rep = wit_u32_ctor(index_rep);
    optional_result = wit_option_ctor(wrapped_index_rep);

end:
    store(exec_env->cx, offset_addr, func_type->results->result,
          optional_result);
    free_wit_value(optional_result);
}

/**
 * @brief Wrapper for the `get-terminal-stderr` function of the
 * `wasi:cli/terminal-stderr` interface.
 * @param exec_env The execution environment.
 * @param[out] offset_addr Memory's offset for stored terminal output handle or
 * an empty value.
 */
void
wasi_cli_get_terminal_stderr_wrapper(wasm_exec_env_t exec_env,
                                     uint32_t offset_addr)
{
    bool is_some;
    wit_value_t optional_result = NULL;

    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    wasi_terminal_output_t handle = wasi_cli_get_terminal_stderr(&is_some);
    if (!is_some) {
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr =
        host_resource_create(WASI_P2_TERMINAL_OUTPUT, sizeof(handle));

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create terminal error resource");
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    *((wasi_terminal_output_t *)hr->data) = handle;

    uint32_t index_rep = host_resource_table_add(hr_table, hr);
    if (index_rep < 1) {
        LOG_VERBOSE("NO index");
        destroy_host_resource(hr); // Clean up the HostResource on failure
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not add terminal error resource to HR table");
        optional_result = wit_option_ctor(NULL);
        goto end;
    }

    wit_value_t wrapped_index_rep = wit_u32_ctor(index_rep);
    optional_result = wit_option_ctor(wrapped_index_rep);

end:
    store(exec_env->cx, offset_addr, func_type->results->result,
          optional_result);
    free_wit_value(optional_result);
}
