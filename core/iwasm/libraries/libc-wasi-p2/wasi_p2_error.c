/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_error.h"
#include <stdlib.h>
#include <string.h>
#include <bh_common.h>
#include "wasi_p2_common.h" /* for wasi_network_error_code_to_string */

uint32_t
wasi_error_new(IOStreamType type, uint32_t code)
{
    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr_err =
        host_resource_create(WASI_P2_ERROR, sizeof(WasiErrorResource));

    if (!hr_err) {
        return 0;
    }

    ((WasiErrorResource *)hr_err->data)->error_code = (int32)code;
    ((WasiErrorResource *)hr_err->data)->type = type;

    uint32_t index_rep = host_resource_table_add(hr_table, hr_err);
    if (index_rep < 1) {
        destroy_host_resource(hr_err); // Clean up the HostResource on failure
        return 0;
    }

    return index_rep;
}

wit_value_t
get_stream_error_val(bool is_closed, uint32_t error_idx)
{

    wit_value_t ret = NULL;
    if (is_closed) {
        ret = wit_variant_ctor("closed", 6, NULL);
    }
    else {
        ret = wit_variant_ctor("last-operation-failed", 21,
                               wit_u32_ctor(error_idx));
    }
    return wit_result_ctor(true, ret);
}

wit_value_t
get_hr_stream_error_val(HostResource *hr, wasi_stream_error_t *err)
{
    IOStreamType stream_type = ((WasiErrorResource *)hr->data)->type;
    int32_t error = err->payload.error;
    uint32_t code = (stream_type == STREAM_TYPE_FILE)
                        ? errno_to_wasi_filesystem(error)
                        : errno_to_wasi_network(error);
    uint32_t new_err =
        wasi_error_new(((WasiErrorResource *)hr->data)->type, code);
    bool is_closed =
        (err->kind == WASI_STREAM_ERROR_KIND_CLOSED) ? true : false;
    return get_stream_error_val(is_closed, new_err);
}

const char *
wasi_error_to_debug_string(HostResource *hr_err)
{

    int32_t code = 0;
    if (((WasiErrorResource *)hr_err->data)->type == STREAM_TYPE_SOCKET) {
        code = ((WasiErrorResource *)hr_err->data)->error_code;
        return wasi_network_error_code_to_string(code);
    }
    else { // Treat general case as filesystem for now
        code = ((WasiErrorResource *)hr_err->data)->error_code;
        return wasi_filesystem_error_code_to_string(code);
    }
}
