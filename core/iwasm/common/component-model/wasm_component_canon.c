/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component_canon.h"
#include "wasm_component_runtime.h"
#include "wasm_component_task.h"
#include "wasm_component_host_resource.h"
#include "wasm_runtime.h"
#include "../libraries/libc-wasi-p2/wasi_p2_common.h"
#include "bh_log.h"

bool
canon_resource_new(WASMComponentResourceInstance *rt,
                   WASMComponentInstance *inst, uint32_t rep,
                   uint32_t *out_index)
{
    if (!rt || !inst || !out_index) {
        LOG_ERROR("canon resource.new: invalid arguments");
        return false;
    }

    if (!inst->may_leave) {
        LOG_ERROR("canon resource.new: component instance may not leave");
        return false;
    }

    WASMResourceHandle *handle = wasm_create_resource_handle(rt, rep, true);
    if (!handle) {
        LOG_ERROR("canon resource.new: failed to create resource handle");
        return false;
    }

    if (!wasm_component_table_add(inst->table, handle,
                                  WASM_TABLE_ELEM_RESOURCE_HANDLE, out_index)) {
        wasm_destroy_resource_handle(handle);
        LOG_ERROR("canon resource.new: failed to add handle to table");
        return false;
    }

    return true;
}

bool
canon_resource_rep(const WASMComponentResourceInstance *rt,
                   WASMComponentInstance *inst, uint32_t handle_index,
                   uint32_t *out_rep)
{
    if (!rt || !inst || !out_rep) {
        LOG_ERROR("canon resource.rep: invalid arguments");
        return false;
    }

    const WASMResourceHandle *handle =
        (WASMResourceHandle *)wasm_component_table_get(
            inst->table, handle_index, WASM_TABLE_ELEM_RESOURCE_HANDLE);
    if (!handle) {
        LOG_ERROR("canon resource.rep: invalid handle index %u", handle_index);
        return false;
    }

    if (handle->rt != rt) {
        LOG_ERROR("canon resource.rep: resource type mismatch");
        return false;
    }

    *out_rep = handle->rep;
    return true;
}

bool
canon_resource_drop(const WASMComponentResourceInstance *rt,
                    WASMComponentInstance *inst, uint32_t handle_index)
{
    if (!rt || !inst) {
        LOG_ERROR("canon resource.drop: invalid arguments");
        return false;
    }

    if (!inst->may_leave) {
        LOG_ERROR("canon resource.drop: component instance may not leave");
        return false;
    }

    WASMResourceHandle *handle = (WASMResourceHandle *)wasm_component_table_get(
        inst->table, handle_index, WASM_TABLE_ELEM_RESOURCE_HANDLE);
    if (!handle) {
        LOG_ERROR("canon resource.drop: invalid handle index %u", handle_index);
        return false;
    }

    if (handle->rt != rt) {
        LOG_ERROR("canon resource.drop: resource type mismatch");
        return false;
    }

    if (handle->num_lends != 0) {
        LOG_ERROR("canon resource.drop: handle still has %u active borrows",
                  handle->num_lends);
        return false;
    }

    bool own = handle->own;
    uint32_t rep = handle->rep;
    Task *borrow_scope = handle->borrow_scope;

    if (!wasm_component_table_remove(inst->table, handle_index)) {
        LOG_ERROR("canon resource.drop: failed to remove handle from table");
        return false;
    }

    if (own) {
        if (rt->is_wasi) {

            HostResourceTable *hr_table = get_global_host_resource_table();
            if (!hr_table) {
                LOG_ERROR("canon resource.drop: failed to retrieve host "
                          "resource table");
                return false;
            }

            HostResource *hr = host_resource_table_get(hr_table, rep);
            if (!hr) {
                LOG_ERROR("canon resource.drop: failed to retrieve host "
                          "resource handle");
                return false;
            }

            if (!host_resource_table_delete(hr_table, rep)) {
                LOG_ERROR("canon resource.drop: failed to remove handle from "
                          "host table");
                return false;
            }
        }

        if (rt->dtor_method) {
            WASMExecEnv *dtor_exec_env = wasm_runtime_get_exec_env_singleton(
                (WASMModuleInstanceCommon *)rt->dtor_method->module_instance);
            if (!dtor_exec_env) {
                LOG_ERROR("canon resource.drop: no exec_env for dtor");
                return false;
            }

            WASMModuleInstanceCommon *saved_inst =
                wasm_runtime_get_module_inst(dtor_exec_env);
            wasm_exec_env_set_module_inst(
                dtor_exec_env,
                (WASMModuleInstanceCommon *)rt->dtor_method->module_instance);

            wasm_val_t arg = { .kind = WASM_I32, .of.i32 = (int32_t)rep };

#ifdef OS_ENABLE_HW_BOUND_CHECK
            WASMExecEnv *saved_tls = wasm_runtime_get_exec_env_tls();
            wasm_runtime_set_exec_env_tls(NULL);
#endif
            if (!wasm_runtime_call_wasm_a(
                    dtor_exec_env,
                    (WASMFunctionInstanceCommon *)rt->dtor_method, 0, NULL, 1,
                    &arg)) {
                const char *ex = wasm_runtime_get_exception(
                    (WASMModuleInstanceCommon *)
                        rt->dtor_method->module_instance);
                LOG_ERROR("canon resource.drop: dtor call failed: %s",
                          ex ? ex : "(unknown)");
#ifdef OS_ENABLE_HW_BOUND_CHECK
                wasm_runtime_set_exec_env_tls(saved_tls);
#endif
                wasm_exec_env_restore_module_inst(dtor_exec_env, saved_inst);
                return false;
            }
#ifdef OS_ENABLE_HW_BOUND_CHECK
            wasm_runtime_set_exec_env_tls(saved_tls);
#endif
            wasm_exec_env_restore_module_inst(dtor_exec_env, saved_inst);
        }
    }
    else {
        if (borrow_scope) {
            borrow_scope->num_borrows--;
        }
    }

    return true;
}
