/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_filesystem_wrapper.h"
#include "wasi_p2_common.h"
#include "wasi_p2_types.h"
#include "wasm_runtime_common.h"
#include "wasi_p2_filesystem.h"
#include "component-model/wasm_component_host_resource.h"
#include "component-model/wasm_canonical_abi.h"
#include "component-model/wasm_component_canonical.h"
#include "../../../product-mini/platforms/common/libc_wasi.h"
#include <string.h>

#include "posix.h"
#include "errno.h"

/* wasi:filesystem/preopens */

/**
 * @brief Constructs an optional datetime wit_value_t.
 * @details Creates a WIT Option value from a wasi_optional_datetime_t
 * structure, representing it as a Record containing seconds and nanoseconds if
 * present.
 * @param datetime Pointer to the wasi_optional_datetime_t to convert.
 * @return A wit_value_t representing the Option<Datetime>.
 */

wit_value_t
get_optional_datetime_val(wasi_optional_datetime_t *datetime)
{
    if (datetime->has_value) {
        wit_value_t seconds_val = wit_u64_ctor(datetime->datetime.seconds);
        wit_value_t nanoseconds_val =
            wit_u32_ctor(datetime->datetime.nanoseconds);
        ComponentWITRecordField *datetime_fields =
            (ComponentWITRecordField *)wasm_runtime_malloc(
                2 * sizeof(ComponentWITRecordField));

        init_record_field(&datetime_fields[0], "seconds", 8, seconds_val);
        init_record_field(&datetime_fields[1], "nanoseconds", 12,
                          nanoseconds_val);

        wit_value_t datetime_val = wit_record_ctor(datetime_fields, 2);
        return wit_option_ctor(datetime_val);
    }
    return wit_option_ctor(NULL);
}

/**
 * @brief Wrapper for the `get-directories` function of the
 * `wasi:filesystem/preopens` interface.
 * @details This function retrieves the list of pre-opened directories and
 * copies it into the WebAssembly guest's memory.
 * @param exec_env The execution environment.
 * @param[out] offset_addr Memory's offset where list struct populated with
 *                        the offset and length of the resulting list of tuples
 * will be stored
 */
void
wasi_filesystem_get_directories_wrapper(wasm_exec_env_t exec_env,
                                        uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);
    struct fd_prestats *prestats = NULL;
    uint32_t count = 0;

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    uint32_t opened_dirs = 0;
    wit_value_t *elems = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = wit_list_ctor(NULL, 0);
        goto end;
    }

    bh_assert(wasi_ctx);
    prestats = wasi_ctx->prestats;
    bh_assert(prestats);

    if (prestats->size) {

        elems = (wit_value_t *)wasm_runtime_malloc(sizeof(wit_value_t)
                                                   * prestats->size);

        struct fd_table *curfds = wasi_ctx->curfds;
        for (uint32_t i = 3; i < prestats->size; i++) {
            if (!prestats->prestats[i].dir)
                continue;

            os_file_handle host_fd;
            if (!fd_table_get_host_handle(curfds, i, &host_fd))
                continue;

            uint32_t dir_len = strlen(prestats->prestats[i].dir);
            char *dir = (char *)wasm_runtime_malloc(sizeof(char) * dir_len);
            strcpy(dir, prestats->prestats[i].dir);
            HostResourceTable *hr_table = get_global_host_resource_table();
            HostResource *hr = host_resource_create(
                WASI_P2_FILESYSTEM_DESCRIPTOR, sizeof(uint32_t));

            // FD opened from initialization, no resource destructor needed
            *((wasi_descriptor_t *)hr->data) = (wasi_descriptor_t)host_fd;

            uint32_t fs_rep = host_resource_table_add(hr_table, hr);

            wit_value_t *tuple_elems =
                (wit_value_t *)wasm_runtime_malloc(2 * sizeof(wit_value_t));
            tuple_elems[0] = wit_u32_ctor(fs_rep);

            StringEncoding encoding = wasm_get_string_encoding(exec_env);
            uint8_t *encoded_str = NULL;
            uint32_t encoded_str_len = 0;
            uint32_t encoded_code_units = 0;
            encode_string(exec_env->cx, dir, strlen(dir), encoding,
                          &encoded_str, &encoded_str_len, &encoded_code_units);
            tuple_elems[1] =
                wit_string_ctor((char *)encoded_str, encoded_str_len,
                                encoded_code_units, encoding);
            elems[opened_dirs] = wit_tuple_ctor(tuple_elems, 2);

            opened_dirs++;
        }
    }

    result = wit_list_ctor(elems, opened_dirs);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
}

/* wasi:filesystem/types */

/* descriptor */

/**
 * @brief Wrapper for the `read-via-stream` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param offset The offset to start reading from.
 * @param[out] offset_addr Memory's offset where new input stream handle or an
 * error code will be stored
 */
void
wasi_filesystem_read_via_stream_wrapper(wasm_exec_env_t exec_env,
                                        wasi_descriptor_t fd,
                                        wasi_filesize_t offset,
                                        uint32_t offset_addr)
{
    wasi_input_stream_t stream;
    int err = 0;
    HostResourceTable *hr_table = get_global_host_resource_table();

    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_read_via_stream(descriptor_fd, offset, &stream, &err);
    if (err == 0) {
        HostResource *hr_stream = host_resource_create(
            WASI_P2_IO_INPUT_STREAM, sizeof(StreamResourceType));

        if (!hr_stream) {
            wasm_runtime_set_exception(
                exec_env->module_inst,
                "Could not create stream input resource");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
            goto end;
        }

        ((StreamResourceType *)hr_stream->data)->fd = stream;
        ((StreamResourceType *)hr_stream->data)->type = STREAM_TYPE_FILE;
        host_resource_set_dtor(hr_stream, file_stream_dtor);

        uint32_t index_rep = host_resource_table_add(hr_table, hr_stream);
        if (index_rep < 1) {
            destroy_host_resource(
                hr_stream); // Clean up the HostResource on failure
            wasm_runtime_set_exception(
                exec_env->module_inst,
                "Could not add stream input resource to HR table");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
            goto end;
        }
        wit_value_t index_val = wit_u32_ctor(index_rep);
        result = wit_result_ctor(false, index_val);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `write-via-stream` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param offset The offset to start writing to.
 * @param[out] offset_addr Memory's offset where new output stream handle or an
 * error code will be stored
 */
void
wasi_filesystem_write_via_stream_wrapper(wasm_exec_env_t exec_env,
                                         wasi_descriptor_t fd,
                                         wasi_filesize_t offset,
                                         uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_input_stream_t stream;
    int err = 0;
    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_write_via_stream(descriptor_fd, offset, &stream, &err);
    if (err == 0) {
        HostResource *hr_stream = host_resource_create(
            WASI_P2_IO_OUTPUT_STREAM, sizeof(StreamResourceType));

        if (!hr_stream) {
            wasm_runtime_set_exception(
                exec_env->module_inst,
                "Could not create stream input resource");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
            goto end;
        }

        ((StreamResourceType *)hr_stream->data)->fd = stream;
        ((StreamResourceType *)hr_stream->data)->type = STREAM_TYPE_FILE;
        host_resource_set_dtor(hr_stream, file_stream_dtor);

        uint32_t index_rep = host_resource_table_add(hr_table, hr_stream);
        if (index_rep < 1) {
            destroy_host_resource(
                hr_stream); // Clean up the HostResource on failure
            wasm_runtime_set_exception(
                exec_env->module_inst,
                "Could not add stream input resource to HR table");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
            goto end;
        }
        wit_value_t index_val = wit_u32_ctor(index_rep);
        result = wit_result_ctor(false, index_val);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `append-via-stream` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param[out] offset_addr Memory's offset where new output stream handle or an
 * error code will be stored
 */
void
wasi_filesystem_append_via_stream_wrapper(wasm_exec_env_t exec_env,
                                          wasi_descriptor_t fd,
                                          uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_output_stream_t stream = 0;
    int err = 0;
    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_append_via_stream(descriptor_fd, &stream, &err);
    if (err == 0) {
        HostResource *hr_stream = host_resource_create(
            WASI_P2_IO_OUTPUT_STREAM, sizeof(StreamResourceType));

        if (!hr_stream) {
            wasm_runtime_set_exception(
                exec_env->module_inst,
                "Could not create stream input resource");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
            goto end;
        }

        ((StreamResourceType *)hr_stream->data)->fd = stream;
        ((StreamResourceType *)hr_stream->data)->type = STREAM_TYPE_FILE;
        host_resource_set_dtor(hr_stream, file_stream_dtor);

        uint32_t index_rep = host_resource_table_add(hr_table, hr_stream);
        if (index_rep < 1) {
            destroy_host_resource(
                hr_stream); // Clean up the HostResource on failure
            wasm_runtime_set_exception(
                exec_env->module_inst,
                "Could not add stream input resource to HR table");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
            goto end;
        }
        wit_value_t index_val = wit_u32_ctor(index_rep);
        result = wit_result_ctor(false, index_val);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `advise` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param offset The offset to start the advisory region from.
 * @param length The length of the advisory region.
 * @param advice The type of advisory to give.
 * @param[out] offset_addr Memory's offset where error code on failure will be
 * stored
 */
void
wasi_filesystem_advise_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                               wasi_filesize_t offset, wasi_filesize_t length,
                               uint32_t advice, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    int err = wasi_filesystem_advise(descriptor_fd, offset, length,
                                     (wasi_advice_t)advice);
    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `sync-data` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param[out] offset_addr Memory's offset where error code on failure will be
 * stored
 */
void
wasi_filesystem_sync_data_wrapper(wasm_exec_env_t exec_env,
                                  wasi_descriptor_t fd, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    int err = wasi_filesystem_sync_data(descriptor_fd);
    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `get-flags` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param[out] offset_addr Memory's offset where the descriptor flags or an
 * error code will be stored
 */
void
wasi_filesystem_get_flags_wrapper(wasm_exec_env_t exec_env,
                                  wasi_descriptor_t fd, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_descriptor_flags_t flags;
    int err = 0;
    wasi_filesystem_get_flags(descriptor_fd, &flags, &err);
    if (err == 0) {
        ComponentWITRecordField *flag_fields =
            (ComponentWITRecordField *)wasm_runtime_malloc(
                6 * sizeof(ComponentWITRecordField));

        init_record_field(&flag_fields[0], "read", 4,
                          wit_bool_ctor(flags & (1 << 0)));
        init_record_field(&flag_fields[1], "write", 6,
                          wit_bool_ctor(flags & (1 << 1)));
        init_record_field(&flag_fields[2], "file-integrity-sync", 20,
                          wit_bool_ctor(flags & (1 << 2)));
        init_record_field(&flag_fields[3], "data-integrity-sync", 20,
                          wit_bool_ctor(flags & (1 << 3)));
        init_record_field(&flag_fields[4], "requested-write-sync", 21,
                          wit_bool_ctor(flags & (1 << 4)));
        init_record_field(&flag_fields[5], "mutate-directory", 17,
                          wit_bool_ctor(flags & (1 << 5)));
        wit_value_t flags_val = wit_flag_ctor(flag_fields, 6);
        result = wit_result_ctor(false, flags_val);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `get-type` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param[out] offset_addr Memory's offset where the descriptor type or an error
 * code will be stored
 */
void
wasi_filesystem_get_type_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                                 uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_descriptor_type_t type;
    int err = 0;
    wasi_filesystem_get_type(descriptor_fd, &type, &err);
    if (err == 0) {
        result = wit_result_ctor(false, wit_enum_ctor(type));
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-size` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param size The new size of the file.
 * @param[out] offset_addr Memory's offset where result struct that will be
 * populated with an error code on failure will be stored
 */
void
wasi_filesystem_set_size_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                                 wasi_filesize_t size, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    int err = wasi_filesystem_set_size(descriptor_fd, size);
    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-times` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param data_access_timestamp_tag The tag for the new data access timestamp.
 * @param data_access_timestamp_sec The seconds part of the new data access
 * timestamp.
 * @param data_access_timestamp_nsec The nanoseconds part of the new data access
 * timestamp.
 * @param data_modification_timestamp_tag The tag for the new data modification
 * timestamp.
 * @param data_modification_timestamp_sec The seconds part of the new data
 * modification timestamp.
 * @param data_modification_timestamp_nsec The nanoseconds part of the new data
 * modification timestamp.
 * @param[out] offset_addr Memory's offset where result struct that will be
 * populated with an error code on failure will be stored
 */
void
wasi_filesystem_set_times_wrapper(wasm_exec_env_t exec_env,
                                  wasi_descriptor_t fd,
                                  uint32_t data_access_timestamp_tag,
                                  int64_t data_access_timestamp_sec,
                                  uint32_t data_access_timestamp_nsec,
                                  uint32_t data_modification_timestamp_tag,
                                  int64_t data_modification_timestamp_sec,
                                  uint32_t data_modification_timestamp_nsec,
                                  uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    wasi_new_timestamp_t data_access_timestamp;
    wasi_new_timestamp_t data_modification_timestamp;

    data_access_timestamp.tag =
        (wasi_new_timestamp_tag_t)data_access_timestamp_tag;
    data_access_timestamp.timestamp.seconds = data_access_timestamp_sec;
    data_access_timestamp.timestamp.nanoseconds = data_access_timestamp_nsec;

    data_modification_timestamp.tag =
        (wasi_new_timestamp_tag_t)data_modification_timestamp_tag;
    data_modification_timestamp.timestamp.seconds =
        data_modification_timestamp_sec;
    data_modification_timestamp.timestamp.nanoseconds =
        data_modification_timestamp_nsec;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    int err = wasi_filesystem_set_times(descriptor_fd, data_access_timestamp,
                                        data_modification_timestamp);
    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `read` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param length The maximum number of bytes to read.
 * @param offset The offset to start reading from.
 * @param[out] offset_addr Memory's offset where result struct that will be
 * populated with the list of bytes read and an end-of-stream flag, or an error
 * code will be stored
 */
void
wasi_filesystem_read_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                             wasi_filesize_t length, wasi_filesize_t offset,
                             uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    wasi_list_u8_t list;
    bool end_of_stream;
    int err = 0;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_read(descriptor_fd, length, offset, &list, &end_of_stream,
                         &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

    wit_value_t *elems =
        (wit_value_t *)wasm_runtime_malloc(list.buf_len * sizeof(wit_value_t));
    uint32_t idx;
    for (idx = 0; idx < list.buf_len; idx++) {
        elems[idx] = wit_u8_ctor(list.buf[idx]);
    }
    wit_value_t *tuple_elems =
        (wit_value_t *)wasm_runtime_malloc(2 * sizeof(wit_value_t));
    tuple_elems[0] = wit_list_ctor(elems, list.buf_len);
    tuple_elems[1] = wit_bool_ctor(end_of_stream);
    wit_value_t tuple_val = wit_tuple_ctor(tuple_elems, 2);
    result = wit_result_ctor(false, tuple_val);

end:

    if (list.buf) {
        wasm_runtime_free(list.buf);
    }
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `write` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param buffer_ptr A pointer to the data to write in the guest's memory.
 * @param buffer_len The length of the data to write.
 * @param offset The offset to start writing to.
 * @param[out] offset_addr Memory's offset where result struct that will be
 * populated with the number of bytes written or an error code will be stored
 */
void
wasi_filesystem_write_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                              uint32_t *buffer_ptr, uint32_t buffer_len,
                              wasi_filesize_t offset, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    wasi_filesize_t bytes_written;
    int err = 0;
    const uint8_t *buffer = (uint8_t *)buffer_ptr;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_write(descriptor_fd, buffer, buffer_len, offset,
                          &bytes_written, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

    result = wit_result_ctor(false, wit_u64_ctor(bytes_written));
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `read-directory` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param[out] offset_addr Memory's offset where result struct that will be
 * populated with the new directory entry stream handle or an error code will be
 * stored
 */
void
wasi_filesystem_read_directory_wrapper(wasm_exec_env_t exec_env,
                                       wasi_descriptor_t fd,
                                       uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    wasi_directory_entry_stream_t stream;
    int err = 0;
    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_read_directory(descriptor_fd, &stream, &err);
    if (err == 0) {
        HostResource *hr_stream = host_resource_create(
            WASI_P2_DIRECTORY_ENTRY_STREAM, sizeof(stream));

        if (!hr_stream) {
            wasm_runtime_set_exception(
                exec_env->module_inst,
                "Could not create dir entry stream resource");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
            goto end;
        }

        *((wasi_directory_entry_stream_t *)hr_stream->data) = stream;
        host_resource_set_dtor(hr_stream, directory_entry_stream_dtor);

        uint32_t index_rep = host_resource_table_add(hr_table, hr_stream);
        if (index_rep < 1) {
            destroy_host_resource(
                hr_stream); // Clean up the HostResource on failure
            wasm_runtime_set_exception(
                exec_env->module_inst,
                "Could not add dir entry stream resource to HR table");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
            goto end;
        }

        wit_value_t index_val = wit_u32_ctor(index_rep);
        result = wit_result_ctor(false, index_val);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `sync` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param[out] offset_addr Memory's offset where result struct that will be
 * populated with an error code on failure will be stored
 */
void
wasi_filesystem_sync_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                             uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    int err = wasi_filesystem_sync(descriptor_fd);
    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `create-directory-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param path_ptr A pointer to the path in the guest's memory.
 * @param path_len The length of the path.
 * @param[out] offset_addr Memory's offset where result struct that will be
 * populated with an error code on failure will be stored
 */
void
wasi_filesystem_create_directory_at_wrapper(wasm_exec_env_t exec_env,
                                            wasi_descriptor_t fd,
                                            uint32_t path_ptr,
                                            uint32_t path_len,
                                            uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    int err = 0;

    wit_value_t name_val;
    if (!load_string_from_range(exec_env->cx, path_ptr, path_len, &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *path = name_val->value.string_value.chars;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    err = wasi_filesystem_create_directory_at(descriptor_fd, path);
    wasm_runtime_free(path);

    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `stat` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param[out] offset_addr Memory's offset where result struct that will be
 * populated with the descriptor stat information or an error code will be
 * stored
 */
void
wasi_filesystem_stat_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                             uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    wasi_descriptor_stat_t *stat =
        (wasi_descriptor_stat_t *)wasm_runtime_malloc(
            sizeof(wasi_descriptor_stat_t));
    int err = 0;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_stat(descriptor_fd, stat, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

    ComponentWITRecordField *fields =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            6 * sizeof(ComponentWITRecordField));

    init_record_field(&fields[0], "type", 4, wit_enum_ctor(stat->type));
    init_record_field(&fields[1], "link-count", 11,
                      wit_u64_ctor(stat->link_count));
    init_record_field(&fields[2], "size", 4, wit_u64_ctor(stat->size));
    init_record_field(&fields[3], "data-access-timestamp", 22,
                      get_optional_datetime_val(&stat->data_access_timestamp));
    init_record_field(
        &fields[4], "data-modification-timestamp", 28,
        get_optional_datetime_val(&stat->data_modification_timestamp));
    init_record_field(
        &fields[5], "status-change-timestamp", 24,
        get_optional_datetime_val(&stat->status_change_timestamp));

    wit_value_t record_val = wit_record_ctor(fields, 6);
    result = wit_result_ctor(false, record_val);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `stat-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param path_flags The path flags.
 * @param path_ptr A pointer to the path in the guest's memory.
 * @param path_len The length of the path.
 * @param[out] offset_addr Memory's offset where result struct that will be
 * populated with the descriptor stat information or an error code will be
 * stored
 */
void
wasi_filesystem_stat_at_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                                uint32_t path_flags, uint32_t path_ptr,
                                uint32_t path_len, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    wit_value_t name_val;
    if (!load_string_from_range(exec_env->cx, path_ptr, path_len, &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *path = name_val->value.string_value.chars;

    wasi_descriptor_stat_t stat;
    int err = 0;
    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_stat_at(descriptor_fd, (wasi_path_flags_t)path_flags, path,
                            &stat, &err);

    wasm_runtime_free(path);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

    ComponentWITRecordField *fields =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            6 * sizeof(ComponentWITRecordField));

    init_record_field(&fields[0], "type", 4, wit_enum_ctor(stat.type));
    init_record_field(&fields[1], "link-count", 11,
                      wit_u64_ctor(stat.link_count));
    init_record_field(&fields[2], "size", 4, wit_u64_ctor(stat.size));
    init_record_field(&fields[3], "data-access-timestamp", 22,
                      get_optional_datetime_val(&stat.data_access_timestamp));
    init_record_field(
        &fields[4], "data-modification-timestamp", 28,
        get_optional_datetime_val(&stat.data_modification_timestamp));
    init_record_field(&fields[5], "status-change-timestamp", 24,
                      get_optional_datetime_val(&stat.status_change_timestamp));

    wit_value_t record_val = wit_record_ctor(fields, 6);
    result = wit_result_ctor(false, record_val);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-times-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Adjusts the access and modification timestamps of a file or
 * directory resolved relative to the given directory descriptor.
 * @param exec_env The execution environment.
 * @param fd The directory file descriptor.
 * @param path_flags Flags determining how the path is resolved (e.g., following
 * symlinks).
 * @param path_ptr Memory offset to the path string in the guest's memory.
 * @param path_len Length of the path string.
 * @param data_access_timestamp_tag Tag indicating how to interpret the access
 * timestamp.
 * @param data_access_timestamp_sec Seconds part of the access timestamp.
 * @param data_access_timestamp_nsec Nanoseconds part of the access timestamp.
 * @param data_modification_timestamp_tag Tag indicating how to interpret the
 * modification timestamp.
 * @param data_modification_timestamp_sec Seconds part of the modification
 * timestamp.
 * @param data_modification_timestamp_nsec Nanoseconds part of the modification
 * timestamp.
 * @param[out] offset_addr Memory offset where the Result struct (containing an
 * error code on failure) will be stored.
 */

void
wasi_filesystem_set_times_at_wrapper(
    wasm_exec_env_t exec_env, wasi_descriptor_t fd, uint32_t path_flags,
    uint32_t path_ptr, uint32_t path_len, uint32_t data_access_timestamp_tag,
    int64_t data_access_timestamp_sec, uint32_t data_access_timestamp_nsec,
    uint32_t data_modification_timestamp_tag,
    int64_t data_modification_timestamp_sec,
    uint32_t data_modification_timestamp_nsec, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    int err = 0;

    wit_value_t name_val;
    if (!load_string_from_range(exec_env->cx, path_ptr, path_len, &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *path = name_val->value.string_value.chars;

    wasi_new_timestamp_t data_access_timestamp;
    wasi_new_timestamp_t data_modification_timestamp;

    data_access_timestamp.tag =
        (wasi_new_timestamp_tag_t)data_access_timestamp_tag;
    data_access_timestamp.timestamp.seconds = data_access_timestamp_sec;
    data_access_timestamp.timestamp.nanoseconds = data_access_timestamp_nsec;

    data_modification_timestamp.tag =
        (wasi_new_timestamp_tag_t)data_modification_timestamp_tag;
    data_modification_timestamp.timestamp.seconds =
        data_modification_timestamp_sec;
    data_modification_timestamp.timestamp.nanoseconds =
        data_modification_timestamp_nsec;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    err = wasi_filesystem_set_times_at(
        descriptor_fd, (wasi_path_flags_t)path_flags, path,
        data_access_timestamp, data_modification_timestamp);
    wasm_runtime_free(path);

    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `link-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Creates a hard link.
 * @param exec_env The execution environment.
 * @param old_fd The directory descriptor from which the old path is resolved.
 * @param old_path_flags Flags determining how the old path is resolved.
 * @param old_path_ptr Memory offset to the old path string.
 * @param old_path_len Length of the old path string.
 * @param new_fd The directory descriptor from which the new path is resolved.
 * @param new_path_ptr Memory offset to the new path string.
 * @param new_path_len Length of the new path string.
 * @param[out] offset_addr Memory offset where the Result struct (containing an
 * error code on failure) will be stored.
 */
void
wasi_filesystem_link_at_wrapper(wasm_exec_env_t exec_env,
                                wasi_descriptor_t old_fd,
                                uint32_t old_path_flags, uint32_t old_path_ptr,
                                uint32_t old_path_len, wasi_descriptor_t new_fd,
                                uint32_t new_path_ptr, uint32_t new_path_len,
                                uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle_old = NULL;
    wit_value_t lifted_handle_new = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, old_fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle_old)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, new_fd,
            func_type->params->params[3].type->type_specific.resource_handle,
            &lifted_handle_new)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    int err = 0;

    wit_value_t name_val = NULL;
    if (!load_string_from_range(exec_env->cx, old_path_ptr, old_path_len,
                                &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *old_path = name_val->value.string_value.chars;

    if (!load_string_from_range(exec_env->cx, new_path_ptr, new_path_len,
                                &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *new_path = name_val->value.string_value.chars;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr1 =
        host_resource_table_get(hr_table, lifted_handle_old->value.u32_value);
    HostResource *hr2 =
        host_resource_table_get(hr_table, lifted_handle_new->value.u32_value);

    if (!(hr1 && hr2)) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fds resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fds from the host resource
    wasi_descriptor_t descriptor_fd1 = *((wasi_descriptor_t *)hr1->data);
    wasi_descriptor_t descriptor_fd2 = *((wasi_descriptor_t *)hr2->data);

    err = wasi_filesystem_link_at(descriptor_fd1,
                                  (wasi_path_flags_t)old_path_flags, old_path,
                                  descriptor_fd2, new_path);
    wasm_runtime_free(old_path);
    wasm_runtime_free(new_path);

    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle_old);
    free_wit_value(lifted_handle_new);
}

/**
 * @brief Wrapper for the `open-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Opens a file or directory resolved relative to the given directory
 * descriptor.
 * @param exec_env The execution environment.
 * @param fd The directory file descriptor.
 * @param path_flags Flags determining how the path is resolved (e.g., following
 * symlinks).
 * @param path_ptr Memory offset to the path string.
 * @param path_len Length of the path string.
 * @param open_flags Flags specifying how the file should be opened (e.g.,
 * create, truncate).
 * @param desc_flags Flags specifying the rights/permissions for the new
 * descriptor.
 * @param[out] offset_addr Memory offset where the Result struct (containing the
 * new descriptor handle or an error code) will be stored.
 */
void
wasi_filesystem_open_at_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                                uint32_t path_flags, uint32_t path_ptr,
                                uint32_t path_len, uint32_t open_flags,
                                uint32_t desc_flags, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wit_value_t path_val = NULL;
    if (!load_string_from_range(exec_env->cx, path_ptr, path_len, &path_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    const char *path = path_val->value.string_value.chars;

    wasi_descriptor_t new_fd;
    int err = 0;
    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_open_at(descriptor_fd, (wasi_path_flags_t)path_flags, path,
                            (wasi_open_flags_t)open_flags,
                            (wasi_descriptor_flags_t)desc_flags, 0666, &new_fd,
                            &err);

    if (err == 0) {
        HostResource *hr_new =
            host_resource_create(WASI_P2_FILESYSTEM_DESCRIPTOR, sizeof(new_fd));

        if (!hr_new) {
            wasm_runtime_set_exception(exec_env->module_inst,
                                       "Could not create descriptor resource");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
            goto end;
        }

        *((wasi_descriptor_t *)hr_new->data) = new_fd;
        host_resource_set_dtor(hr_new, filesystem_descriptor_dtor);

        uint32_t index_rep = host_resource_table_add(hr_table, hr_new);
        if (index_rep < 1) {
            destroy_host_resource(
                hr_new); // Clean up the HostResource on failure
            wasm_runtime_set_exception(
                exec_env->module_inst,
                "Could not add descriptor resource to HR table");
            result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
            goto end;
        }

        wit_value_t index_val = wit_u32_ctor(index_rep);
        result = wit_result_ctor(false, index_val);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
    free_wit_value(path_val);
}

/**
 * @brief Wrapper for the `readlink-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Reads the contents of a symbolic link resolved relative to the given
 * directory descriptor.
 * @param exec_env The execution environment.
 * @param fd The directory file descriptor.
 * @param path_ptr Memory offset to the symbolic link path string.
 * @param path_len Length of the symbolic link path string.
 * @param[out] offset_addr Memory offset where the Result struct (containing the
 * resolved link string or an error code) will be stored.
 */
void
wasi_filesystem_readlink_at_wrapper(wasm_exec_env_t exec_env,
                                    wasi_descriptor_t fd, uint32_t path_ptr,
                                    uint32_t path_len, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    wit_value_t name_val = NULL;
    if (!load_string_from_range(exec_env->cx, path_ptr, path_len, &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *path = name_val->value.string_value.chars;

    char *link_content;
    int err = 0;
    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_readlink_at(descriptor_fd, path, &link_content, &err);
    wasm_runtime_free(path);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

    StringEncoding encoding = wasm_get_string_encoding(exec_env);
    uint8_t *encoded_str;
    uint32_t encoded_str_len;
    uint32_t encoded_code_units;
    encode_string(exec_env->cx, link_content, strlen(link_content), encoding,
                  &encoded_str, &encoded_str_len, &encoded_code_units);
    wit_value_t str_val = wit_string_ctor((char *)encoded_str, encoded_str_len,
                                          encoded_code_units, encoding);
    result = wit_result_ctor(false, str_val);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
    wasm_runtime_free(link_content);
}

/**
 * @brief Wrapper for the `remove-directory-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Removes a directory resolved relative to the given directory
 * descriptor.
 * @param exec_env The execution environment.
 * @param fd The directory file descriptor.
 * @param path_ptr Memory offset to the directory path string.
 * @param path_len Length of the directory path string.
 * @param[out] offset_addr Memory offset where the Result struct (containing an
 * error code on failure) will be stored.
 */
void
wasi_filesystem_remove_directory_at_wrapper(wasm_exec_env_t exec_env,
                                            wasi_descriptor_t fd,
                                            uint32_t path_ptr,
                                            uint32_t path_len,
                                            uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    int err = 0;

    wit_value_t name_val = NULL;
    if (!load_string_from_range(exec_env->cx, path_ptr, path_len, &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *path = name_val->value.string_value.chars;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    err = wasi_filesystem_remove_directory_at(descriptor_fd, path);
    wasm_runtime_free(path);

    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `rename-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Renames a file or directory.
 * @param exec_env The execution environment.
 * @param old_fd The directory descriptor from which the old path is resolved.
 * @param old_path_ptr Memory offset to the old path string.
 * @param old_path_len Length of the old path string.
 * @param new_fd The directory descriptor from which the new path is resolved.
 * @param new_path_ptr Memory offset to the new path string.
 * @param new_path_len Length of the new path string.
 * @param[out] offset_addr Memory offset where the Result struct (containing an
 * error code on failure) will be stored.
 */
void
wasi_filesystem_rename_at_wrapper(wasm_exec_env_t exec_env,
                                  wasi_descriptor_t old_fd,
                                  uint32_t old_path_ptr, uint32_t old_path_len,
                                  wasi_descriptor_t new_fd,
                                  uint32_t new_path_ptr, uint32_t new_path_len,
                                  uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle_old = NULL;
    wit_value_t lifted_handle_new = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, old_fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle_old)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, new_fd,
            func_type->params->params[2].type->type_specific.resource_handle,
            &lifted_handle_new)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    int err = 0;

    wit_value_t name_val = NULL;
    if (!load_string_from_range(exec_env->cx, old_path_ptr, old_path_len,
                                &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *old_path = name_val->value.string_value.chars;

    if (!load_string_from_range(exec_env->cx, new_path_ptr, new_path_len,
                                &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *new_path = name_val->value.string_value.chars;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr1 =
        host_resource_table_get(hr_table, lifted_handle_old->value.u32_value);
    HostResource *hr2 =
        host_resource_table_get(hr_table, lifted_handle_new->value.u32_value);

    if (!(hr1 && hr2)) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd1 = *((wasi_descriptor_t *)hr1->data);
    wasi_descriptor_t descriptor_fd2 = *((wasi_descriptor_t *)hr2->data);

    err = wasi_filesystem_rename_at(descriptor_fd1, old_path, descriptor_fd2,
                                    new_path);
    wasm_runtime_free(old_path);
    wasm_runtime_free(new_path);

    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle_old);
    free_wit_value(lifted_handle_new);
}

/**
 * @brief Wrapper for the `symlink-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Creates a symbolic link.
 * @param exec_env The execution environment.
 * @param fd The directory descriptor from which the new symbolic link is
 * resolved.
 * @param old_path_ptr Memory offset to the target path string of the symbolic
 * link.
 * @param old_path_len Length of the target path string.
 * @param new_path_ptr Memory offset to the name of the new symbolic link.
 * @param new_path_len Length of the new symbolic link name.
 * @param[out] offset_addr Memory offset where the Result struct (containing an
 * error code on failure) will be stored.
 */
void
wasi_filesystem_symlink_at_wrapper(wasm_exec_env_t exec_env,
                                   wasi_descriptor_t fd, uint32_t old_path_ptr,
                                   uint32_t old_path_len, uint32_t new_path_ptr,
                                   uint32_t new_path_len, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    int err = 0;

    wit_value_t name_val = NULL;
    if (!load_string_from_range(exec_env->cx, old_path_ptr, old_path_len,
                                &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *old_path = name_val->value.string_value.chars;

    if (!load_string_from_range(exec_env->cx, new_path_ptr, new_path_len,
                                &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *new_path = name_val->value.string_value.chars;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    err = wasi_filesystem_symlink_at(descriptor_fd, old_path, new_path);
    wasm_runtime_free(old_path);
    wasm_runtime_free(new_path);

    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `unlink-file-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Unlinks (deletes) a file resolved relative to the given directory
 * descriptor.
 * @param exec_env The execution environment.
 * @param fd The directory file descriptor.
 * @param path_ptr Memory offset to the file path string.
 * @param path_len Length of the file path string.
 * @param[out] offset_addr Memory offset where the Result struct (containing an
 * error code on failure) will be stored.
 */
void
wasi_filesystem_unlink_file_at_wrapper(wasm_exec_env_t exec_env,
                                       wasi_descriptor_t fd, uint32_t path_ptr,
                                       uint32_t path_len, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    int err = 0;

    wit_value_t name_val = NULL;
    if (!load_string_from_range(exec_env->cx, path_ptr, path_len, &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *path = name_val->value.string_value.chars;

    if (!path) {
        err = WASI_FILESYSTEM_CODE_INSUFFICIENT_MEMORY;
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    err = wasi_filesystem_unlink_file_at(descriptor_fd, path);
    wasm_runtime_free(path);

    if (err == 0) {
        result = wit_result_ctor(false, NULL);
    }
    else {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `is-same-object` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Checks if two file descriptors refer to the exact same underlying
 * file system object.
 * @param exec_env The execution environment.
 * @param fd1 The first file descriptor.
 * @param fd2 The second file descriptor.
 * @return uint32_t Returns 1 if they point to the same object, 0 otherwise.
 */
uint32_t
wasi_filesystem_is_same_object_wrapper(wasm_exec_env_t exec_env,
                                       wasi_descriptor_t fd1,
                                       wasi_descriptor_t fd2)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle_1 = NULL;
    wit_value_t lifted_handle_2 = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        return 0;
    }

    if (!lift_borrow(
            exec_env->cx, fd1,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle_1)) {
        return 0;
    }

    if (!lift_borrow(
            exec_env->cx, fd2,
            func_type->params->params[1].type->type_specific.resource_handle,
            &lifted_handle_2)) {
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr1 =
        host_resource_table_get(hr_table, lifted_handle_1->value.u32_value);
    HostResource *hr2 =
        host_resource_table_get(hr_table, lifted_handle_2->value.u32_value);

    if (!(hr1 && hr2)) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        return 0;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd1 = *((wasi_descriptor_t *)hr1->data);
    wasi_descriptor_t descriptor_fd2 = *((wasi_descriptor_t *)hr2->data);

    return wasi_filesystem_is_same_object(descriptor_fd1, descriptor_fd2);
}

/**
 * @brief Wrapper for the `metadata-hash` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Calculates a hash of the file system object's metadata.
 * @param exec_env The execution environment.
 * @param fd The file descriptor.
 * @param[out] offset_addr Memory offset where the Result struct (containing the
 * upper and lower hash values, or an error code) will be stored.
 */
void
wasi_filesystem_metadata_hash_wrapper(wasm_exec_env_t exec_env,
                                      wasi_descriptor_t fd,
                                      uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    wasi_metadata_hash_value_t hash;
    int err = 0;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);

    wasi_filesystem_metadata_hash(descriptor_fd, &hash, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

    ComponentWITRecordField *fields =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            2 * sizeof(ComponentWITRecordField));

    init_record_field(&fields[0], "lower", 6, wit_u64_ctor(hash.lower));
    init_record_field(&fields[1], "upper", 6, wit_u64_ctor(hash.upper));

    wit_value_t record_val = wit_record_ctor(fields, 2);
    result = wit_result_ctor(false, record_val);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `metadata-hash-at` method of the
 * `wasi:filesystem/types.descriptor` resource.
 * @details Calculates a hash of the metadata for a file or directory resolved
 * relative to a directory descriptor.
 * @param exec_env The execution environment.
 * @param fd The directory file descriptor.
 * @param path_flags Flags determining how the path is resolved.
 * @param path_ptr Memory offset to the path string.
 * @param path_len Length of the path string.
 * @param[out] offset_addr Memory offset where the Result struct (containing the
 * upper and lower hash values, or an error code) will be stored.
 */
void
wasi_filesystem_metadata_hash_at_wrapper(wasm_exec_env_t exec_env,
                                         wasi_descriptor_t fd,
                                         uint32_t path_flags, uint32_t path_ptr,
                                         uint32_t path_len,
                                         uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    wit_value_t name_val = NULL;
    if (!load_string_from_range(exec_env->cx, path_ptr, path_len, &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *path = name_val->value.string_value.chars;

    wasi_metadata_hash_value_t hash;
    int err = 0;

    if (!lift_borrow(
            exec_env->cx, fd,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get descriptor fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual descriptor fd from the host resource
    wasi_descriptor_t descriptor_fd = *((wasi_descriptor_t *)hr->data);
    wasi_filesystem_metadata_hash_at(
        descriptor_fd, (wasi_path_flags_t)path_flags, path, &hash, &err);
    wasm_runtime_free(path);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

    ComponentWITRecordField *fields =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            2 * sizeof(ComponentWITRecordField));

    init_record_field(&fields[0], "lower", 6, wit_u64_ctor(hash.lower));
    init_record_field(&fields[1], "upper", 6, wit_u64_ctor(hash.upper));

    wit_value_t record_val = wit_record_ctor(fields, 2);
    result = wit_result_ctor(false, record_val);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `read-directory-entry` method of the
 * `wasi:filesystem/types.directory-entry-stream` resource.
 * @details Reads a single directory entry from an open directory entry stream.
 * @param exec_env The execution environment.
 * @param stream The directory entry stream handle.
 * @param[out] offset_addr Memory offset where the Result struct will be stored.
 * On success, contains an Option wrapping the directory entry (type and name).
 * If the stream is exhausted, the Option will be None.
 * On failure, contains an error code.
 */
void
wasi_filesystem_read_directory_entry_wrapper(wasm_exec_env_t exec_env,
                                             int64_t stream,
                                             uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_UNSUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, stream,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_directory_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    bool is_some;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not get dir entry stream fd fd resource");
        result = get_result_error_val(WASI_FILESYSTEM_CODE_INVALID);
        goto end;
    }

    // Get the actual dir entry stream fd from the host resource
    wasi_directory_entry_stream_t dir_entry_stream_fd =
        *((wasi_directory_entry_stream_t *)hr->data);

    wasi_filesystem_read_directory_entry(dir_entry_stream_fd, &entry, &is_some,
                                         &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_filesystem(err));
        goto end;
    }

    if (!err && !is_some) {
        // No more files in directory
        result = wit_result_ctor(false, wit_option_ctor(NULL));
        goto end;
    }

    if (!is_some) {
        result = get_result_error_val(WASI_FILESYSTEM_CODE_NO_ENTRY);
        goto end;
    }

    ComponentWITRecordField *fields =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            2 * sizeof(ComponentWITRecordField));

    init_record_field(&fields[0], "type", 4, wit_enum_ctor(entry.type));

    StringEncoding encoding = wasm_get_string_encoding(exec_env);
    uint8_t *encoded_str = NULL;
    uint32_t encoded_str_len = 0;
    uint32_t encoded_code_units = 0;
    encode_string(exec_env->cx, entry.name, strlen(entry.name), encoding,
                  &encoded_str, &encoded_str_len, &encoded_code_units);

    init_record_field(&fields[1], "name", 4,
                      wit_string_ctor((char *)encoded_str, encoded_str_len,
                                      encoded_code_units, encoding));

    wit_value_t directory_entry_val = wit_record_ctor(fields, 2);
    wit_value_t directory_entry_opt = wit_option_ctor(directory_entry_val);
    result = wit_result_ctor(false, directory_entry_opt);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
    if (entry.name)
        wasm_runtime_free(entry.name);
    return;
}

/**
 * @brief Wrapper for the `filesystem-error-code` method in
 * `wasi:filesystem/types`.
 * @details Retrieves the specific filesystem error code from a generic error
 * context, if available.
 * @param exec_env The execution environment.
 * @param err The opaque error context handle.
 * @param[out] offset_addr Memory offset where the Option struct (containing the
 * specific `error-code` Enum, or None) will be stored.
 */
void
wasi_filesystem_filesystem_error_code_wrapper(wasm_exec_env_t exec_env,
                                              uint32_t err,
                                              uint32_t offset_addr)
{

    wit_value_t result = NULL;
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    bool is_some;
    int error_code = wasi_filesystem_error_code(err, &is_some);
    if (is_some) {
        result = wit_option_ctor(wit_enum_ctor((uint32_t)error_code));
    }
    else {
        result = wit_option_ctor(NULL);
    }

    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
}