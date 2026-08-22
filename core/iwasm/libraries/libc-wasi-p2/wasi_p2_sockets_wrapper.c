/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_sockets_wrapper.h"
#include "bh_common.h"
#include "bh_hashmap.h"
#include "wasm_runtime_common.h"

#include "wasi_p2_io.h"
#include "wasi_p2_common.h"
#include "wasi_p2_types.h"
#include "component-model/wasm_component_host_resource.h"
#include "component-model/wasm_component_resource.h"
#include "component-model/wasm_canonical_abi.h"
#include "component-model/wasm_component_canonical.h"
#include "component-model/wasm_component_flat.h"
#include "../../../product-mini/platforms/common/libc_wasi.h"
#include <sys/socket.h>
#include <pthread.h>
#include <limits.h>

// Helper function

/**
 * @brief Helper function to convert a WASI IP socket address struct into a WIT
 * Variant value.
 * @param address Pointer to the `wasi_ip_socket_address_t` struct to convert.
 * @return wit_value_t The constructed WIT Variant value representing an IPv4 or
 * IPv6 socket address.
 */
wit_value_t
get_ip_sock_addr_value(wasi_ip_socket_address_t *address)
{

    wit_value_t ip_sock_address;

    if (!address) {
        return NULL;
    }

    if (address->tag == WASI_IP_SOCKET_ADDRESS_TAG_IPV4) {

        wit_value_t *elems =
            (wit_value_t *)wasm_runtime_malloc(4 * sizeof(wit_value_t));
        elems[0] = wit_u8_ctor(address->val.ipv4.address.f0);
        elems[1] = wit_u8_ctor(address->val.ipv4.address.f1);
        elems[2] = wit_u8_ctor(address->val.ipv4.address.f2);
        elems[3] = wit_u8_ctor(address->val.ipv4.address.f3);
        wit_value_t ipv4_address = wit_tuple_ctor(elems, 4);

        ComponentWITRecordField *fields =
            (ComponentWITRecordField *)wasm_runtime_malloc(
                sizeof(ComponentWITRecordField) * 2);
        init_record_field(&fields[0], "port", 4,
                          wit_u16_ctor(address->val.ipv4.port));
        init_record_field(&fields[1], "address", 6, ipv4_address);
        wit_value_t ipv4_sock_address = wit_record_ctor(fields, 2);

        ip_sock_address = wit_variant_ctor("ipv4", 4, ipv4_sock_address);
    }
    else {
        wit_value_t *elems =
            (wit_value_t *)wasm_runtime_malloc(8 * sizeof(wit_value_t));
        elems[0] = wit_u16_ctor(address->val.ipv6.address.f0);
        elems[1] = wit_u16_ctor(address->val.ipv6.address.f1);
        elems[2] = wit_u16_ctor(address->val.ipv6.address.f2);
        elems[3] = wit_u16_ctor(address->val.ipv6.address.f3);
        elems[4] = wit_u16_ctor(address->val.ipv6.address.f4);
        elems[5] = wit_u16_ctor(address->val.ipv6.address.f5);
        elems[6] = wit_u16_ctor(address->val.ipv6.address.f6);
        elems[7] = wit_u16_ctor(address->val.ipv6.address.f7);
        wit_value_t ipv6_address = wit_tuple_ctor(elems, 8);

        ComponentWITRecordField *fields =
            (ComponentWITRecordField *)wasm_runtime_malloc(
                sizeof(ComponentWITRecordField) * 4);
        init_record_field(&fields[0], "port", 4,
                          wit_u16_ctor(address->val.ipv6.port));
        init_record_field(&fields[1], "flow-info", 9,
                          wit_u32_ctor(address->val.ipv6.flow_info));
        init_record_field(&fields[2], "address", 6, ipv6_address);
        init_record_field(&fields[3], "scope-id", 8,
                          wit_u32_ctor(address->val.ipv6.scope_id));
        wit_value_t ipv6_sock_address = wit_record_ctor(fields, 4);

        ip_sock_address = wit_variant_ctor("ipv6", 4, ipv6_sock_address);
    }

    return ip_sock_address;
}

/**
 * @brief Helper function to convert a WASI IP address struct into a WIT Option
 * value.
 * @param address Pointer to the `wasi_ip_address_t` struct to convert.
 * @return wit_value_t The constructed WIT Option value wrapping the IP address
 * Variant.
 */
wit_value_t
get_ip_addr_value(wasi_ip_address_t *address)
{

    wit_value_t ip_address;

    if (!address) {
        return NULL;
    }

    if (address->kind == IPv4) {

        wit_value_t *elems =
            (wit_value_t *)wasm_runtime_malloc(4 * sizeof(wit_value_t));
        elems[0] = wit_u8_ctor(address->addr.ip4.n0);
        elems[1] = wit_u8_ctor(address->addr.ip4.n1);
        elems[2] = wit_u8_ctor(address->addr.ip4.n2);
        elems[3] = wit_u8_ctor(address->addr.ip4.n3);
        wit_value_t ipv4_address = wit_tuple_ctor(elems, 4);

        ip_address = wit_variant_ctor("ipv4", 4, ipv4_address);
    }
    else {
        wit_value_t *elems =
            (wit_value_t *)wasm_runtime_malloc(8 * sizeof(wit_value_t));
        elems[0] = wit_u16_ctor(address->addr.ip6.n0);
        elems[1] = wit_u16_ctor(address->addr.ip6.n1);
        elems[2] = wit_u16_ctor(address->addr.ip6.n2);
        elems[3] = wit_u16_ctor(address->addr.ip6.n3);
        elems[4] = wit_u16_ctor(address->addr.ip6.h0);
        elems[5] = wit_u16_ctor(address->addr.ip6.h1);
        elems[6] = wit_u16_ctor(address->addr.ip6.h2);
        elems[7] = wit_u16_ctor(address->addr.ip6.h3);
        wit_value_t ipv6_address = wit_tuple_ctor(elems, 8);

        ip_address = wit_variant_ctor("ipv6", 4, ipv6_address);
    }

    return wit_option_ctor(ip_address);
}

/**
 * @brief Helper function to convert an incoming datagram struct into a WIT
 * Record value.
 * @param datagram Pointer to the `wasi_incoming_datagram_t` struct to convert.
 * @return wit_value_t The constructed WIT Record value containing the data and
 * remote address.
 */
wit_value_t
get_incoming_datagram_value(wasi_incoming_datagram_t *datagram)
{
    uint32_t idx;
    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(
        datagram->data_len * sizeof(wit_value_t));
    for (idx = 0; idx < datagram->data_len; idx++) {
        elems[idx] = wit_u8_ctor(datagram->data[idx]);
    }
    wit_value_t data = wit_list_ctor(elems, datagram->data_len);
    wit_value_t remote_address =
        get_ip_sock_addr_value(&datagram->remote_address);

    ComponentWITRecordField *fields =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            sizeof(ComponentWITRecordField) * 2);
    init_record_field(&fields[0], "data", 4, data);
    init_record_field(&fields[1], "remote-address", 14, remote_address);
    return wit_record_ctor(fields, 2);
}

/* wasi:sockets/instance-network */

/**
 * @brief Wrapper for the `instance-network` function of the
 * `wasi:sockets/instance-network` interface.
 * @param exec_env The execution environment.
 * @return A handle to the default network resource.
 */
uint32_t
wasi_sockets_instance_network_instance_network_wrapper(wasm_exec_env_t exec_env)
{
    wasi_network_t instance_network = wasi_sockets_instance_network();

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr =
        host_resource_create(WASI_P2_NETWORK, sizeof(instance_network));

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create network resource");
        return 0;
    }

    *((uint32_t *)hr->data) = instance_network;

    uint32_t out = host_resource_table_add(hr_table, hr);
    if (out < 1) {
        destroy_host_resource(hr); // Clean up the HostResource on failure
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not add network resource to HR table");
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(out);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &out);

    free_wit_value(out_val);
    return out;
}

/* wasi:sockets/ip-name-lookup */

/**
 * @brief Wrapper for the `resolve-addresses` function of the
 * `wasi:sockets/ip-name-lookup` interface.
 * @details This function resolves a hostname to a stream of IP addresses.
 * @param exec_env The execution environment.
 * @param network_handle The handle of the network resource.
 * @param name_ptr A pointer to the hostname string in the guest's memory.
 * @param name_len The length of the hostname string.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_ip_name_lookup_resolve_addresses_wrapper(wasm_exec_env_t exec_env,
                                                      uint32_t network_handle,
                                                      uint32_t name_ptr,
                                                      uint32_t name_len,
                                                      uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!lift_borrow(
            exec_env->cx, network_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->allow_ip_name_lookup) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    wasi_resolve_address_stream_t stream;
    int err = 0;

    wit_value_t name_val;
    if (!load_string_from_range(exec_env->cx, name_ptr, name_len, &name_val)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    char *name = name_val->value.string_value.chars;

    if (!name) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual network fd from the host resource
    wasi_network_t network_fd = *((uint32_t *)hr->data);

    wasi_sockets_resolve_addresses(network_fd, name, &stream, &err);

    wasm_runtime_free(name);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        HostResource *hr_stream = host_resource_create(
            WASI_P2_RESOLVE_ADDRESS_STREAM, sizeof(stream));

        if (!hr_stream) {
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        *((uint32_t *)hr_stream->data) = stream;
        host_resource_set_dtor(hr_stream, resolve_stream_dtor);

        uint32_t out = host_resource_table_add(hr_table, hr_stream);
        if (out < 1) {
            destroy_host_resource(
                hr_stream); // Clean up the HostResource on failure
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }
        wit_value_t out_val = wit_u32_ctor(out);
        result = wit_result_ctor(false, out_val);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `resolve-next-address` method of the
 * `wasi:sockets/ip-name-lookup.resolve-address-stream` resource.
 * @param exec_env The execution environment.
 * @param stream_handle The handle of the resolve-address-stream resource.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_ip_name_lookup_resolve_address_stream_resolve_next_address_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;
    wit_value_t ip_addr;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->allow_ip_name_lookup) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, stream_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_ip_address_t ip_address;
    bool is_some;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual stream fd from the host resource
    wasi_network_t stream_fd = *((uint32_t *)hr->data);

    wasi_sockets_resolve_next_address(stream_fd, &ip_address, &is_some, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    if (is_some) {
        ip_addr = get_ip_addr_value(&ip_address);
        result = wit_result_ctor(false, ip_addr);
        goto end;
    }
    else {
        result = wit_result_ctor(false, NULL);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `subscribe` method of the
 * `wasi:sockets/ip-name-lookup.resolve-address-stream` resource.
 * @param exec_env The execution environment.
 * @param stream_handle The rep of the resolve-address-stream resource.
 * @return A new pollable resource rep.
 */
uint32_t
wasi_sockets_ip_name_lookup_resolve_address_stream_subscribe_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;

    if (!lift_borrow(
            exec_env->cx, stream_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        return 0;
    }

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->allow_ip_name_lookup) {
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);
    free_wit_value(lifted_handle);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get stream resource");
        return 0;
    }

    // Get the actual stream fd from the host resource
    wasi_network_t stream_fd = *((uint32_t *)hr->data);

    wasi_pollable_context_t pollable =
        wasi_sockets_resolve_address_stream_subscribe(stream_fd);
    if (pollable.fd < 0) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get pollable FD");
        return 0;
    }

    HostResource *hr_poll =
        host_resource_create(WASI_P2_POLLABLE, sizeof(wasi_pollable_context_t));

    if (!hr_poll) {
        if (pollable.own_fd)
            close(pollable.fd);
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create pollable resource");
        return 0;
    }
    *((wasi_pollable_context_t *)hr_poll->data) = pollable;
    host_resource_set_dtor(hr_poll, pollable_dtor);

    uint32_t index_rep = host_resource_table_add(hr_table, hr_poll);
    if (index_rep < 1) {
        destroy_host_resource(hr_poll); // Clean up the HostResource on failure
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not add pollable resource to HR table");
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(index_rep);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &index_rep);

    free_wit_value(out_val);

    return index_rep;
}

/* wasi:sockets/tcp-create-socket */

/**
 * @brief Wrapper for the `create-tcp-socket` function of the
 * `wasi:sockets/tcp-create-socket` interface.
 * @param exec_env The execution environment.
 * @param address_family The IP address family.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_create_socket_create_tcp_socket_wrapper(
    wasm_exec_env_t exec_env, uint32_t address_family, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    wasi_tcp_socket_t socket;
    int err = 0;

    wasi_sockets_create_tcp_socket(address_family, &socket, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr =
        host_resource_create(WASI_P2_TCP_SOCKET, sizeof(wasi_socket_context_t));

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
        goto end;
    }

    ((wasi_socket_context_t *)hr->data)->family = address_family;
    ((wasi_socket_context_t *)hr->data)->fd = socket;
    ((wasi_socket_context_t *)hr->data)->tcp_listen_backlog = SOMAXCONN;
    host_resource_set_dtor(hr, tcp_socket_dtor);

    uint32_t index_rep = host_resource_table_add(hr_table, hr);
    if (index_rep < 1) {
        destroy_host_resource(hr); // Clean up the HostResource on failure
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
        goto end;
    }
    else {
        wit_value_t out_rep = wit_u32_ctor(index_rep);
        result = wit_result_ctor(false, out_rep);
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
}

/* wasi:sockets/tcp */

/**
 * @brief Helper function to convert flattened IP socket address fields into a
 * structure.
 */
static void
read_flattened_ip_socket_address(wasi_ip_socket_address_t *addr,
                                 uint32_t addr_tag, uint32_t p0, uint32_t p1,
                                 uint32_t p2, uint32_t p3, uint32_t p4,
                                 uint32_t p5, uint32_t p6, uint32_t p7,
                                 uint32_t p8, uint32_t p9, uint32_t p10)
{
    addr->tag = addr_tag;
    if (addr->tag == WASI_IP_SOCKET_ADDRESS_TAG_IPV4) {
        addr->val.ipv4.port = (uint16_t)p0;
        addr->val.ipv4.address.f0 = (uint8_t)p1;
        addr->val.ipv4.address.f1 = (uint8_t)p2;
        addr->val.ipv4.address.f2 = (uint8_t)p3;
        addr->val.ipv4.address.f3 = (uint8_t)p4;
    }
    else { /* IPV6 */
        addr->val.ipv6.port = (uint16_t)p0;
        addr->val.ipv6.flow_info = (uint32_t)p1;
        addr->val.ipv6.address.f0 = (uint16_t)p2;
        addr->val.ipv6.address.f1 = (uint16_t)p3;
        addr->val.ipv6.address.f2 = (uint16_t)p4;
        addr->val.ipv6.address.f3 = (uint16_t)p5;
        addr->val.ipv6.address.f4 = (uint16_t)p6;
        addr->val.ipv6.address.f5 = (uint16_t)p7;
        addr->val.ipv6.address.f6 = (uint16_t)p8;
        addr->val.ipv6.address.f7 = (uint16_t)p9;
        addr->val.ipv6.scope_id = (uint32_t)p10;
    }
}

/**
 * @brief Wrapper for the `start-bind` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param network_handle The handle of the network resource.
 * @param addr_tag The tag of the IP socket address.
 * @param ... The flattened fields of the IP socket address.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_start_bind_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t network_handle,
    uint32_t addr_tag, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3,
    uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8,
    uint32_t p9, uint32_t p10, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_network_handle = NULL;
    wit_value_t lifted_socket_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    wasi_ip_socket_address_t local_address;
    int err = 0;

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_socket_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, network_handle,
            func_type->params->params[1].type->type_specific.resource_handle,
            &lifted_network_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    read_flattened_ip_socket_address(&local_address, addr_tag, p0, p1, p2, p3,
                                     p4, p5, p6, p7, p8, p9, p10);

    HostResourceTable *hr_table = get_global_host_resource_table();

    HostResource *socket_hr = host_resource_table_get(
        hr_table, lifted_socket_handle->value.u32_value);
    HostResource *network_hr = host_resource_table_get(
        hr_table, lifted_network_handle->value.u32_value);

    if (!(socket_hr && network_hr)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd and network fd from the host resource
    wasi_network_t network_fd = *((uint32_t *)network_hr->data);
    wasi_tcp_socket_t socket_fd =
        ((wasi_socket_context_t *)socket_hr->data)->fd;

    err = wasi_sockets_tcp_start_bind(socket_fd, network_fd, &local_address);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        result = wit_result_ctor(false, NULL);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_socket_handle);
    free_wit_value(lifted_network_handle);
}

/**
 * @brief Wrapper for the `finish-bind` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_finish_bind_wrapper(wasm_exec_env_t exec_env,
                                                uint32_t socket_handle,
                                                uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *socket_hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!socket_hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd =
        ((wasi_socket_context_t *)socket_hr->data)->fd;

    int err = wasi_sockets_tcp_finish_bind(socket_fd);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Helper function to write an IP socket address back into WASM native
 * memory.
 */
static void
write_ip_socket_address(WASMExecEnv *exec_env,
                        const wasi_ip_socket_address_t *addr,
                        uint64_t wasm_addr_offset)
{
    char *addr_ptr =
        wasm_runtime_addr_app_to_native_p2(exec_env, wasm_addr_offset);
    memset(addr_ptr, 0, 4 + sizeof(wasi_ipv6_socket_address_t));
    *(uint8_t *)addr_ptr = addr->tag;
    if (addr->tag == WASI_IP_SOCKET_ADDRESS_TAG_IPV4) {
        *(wasi_ipv4_socket_address_t *)(addr_ptr + 4) = addr->val.ipv4;
    }
    else {
        *(wasi_ipv6_socket_address_t *)(addr_ptr + 4) = addr->val.ipv6;
    }
}

/**
 * @brief Wrapper for the `start-connect` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param network_handle The handle of the network resource.
 * @param addr_tag The tag of the IP socket address.
 * @param ... The flattened fields of the IP socket address.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_start_connect_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t network_handle,
    uint32_t addr_tag, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3,
    uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8,
    uint32_t p9, uint32_t p10, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_network_handle = NULL;
    wit_value_t lifted_socket_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_socket_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, network_handle,
            func_type->params->params[1].type->type_specific.resource_handle,
            &lifted_network_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_ip_socket_address_t remote_address;
    int err = 0;

    read_flattened_ip_socket_address(&remote_address, addr_tag, p0, p1, p2, p3,
                                     p4, p5, p6, p7, p8, p9, p10);

    HostResourceTable *hr_table = get_global_host_resource_table();

    HostResource *socket_hr = host_resource_table_get(
        hr_table, lifted_socket_handle->value.u32_value);
    HostResource *network_hr = host_resource_table_get(
        hr_table, lifted_network_handle->value.u32_value);

    if (!(socket_hr && network_hr)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd and network fd from the host resource
    wasi_network_t network_fd = *((wasi_network_t *)network_hr->data);
    uint32_t socket_fd = ((wasi_socket_context_t *)socket_hr->data)->fd;

    err =
        wasi_sockets_tcp_start_connect(socket_fd, network_fd, &remote_address);

    if (err != 0 && err != EINPROGRESS) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_socket_handle);
    free_wit_value(lifted_network_handle);
}

/**
 * @brief Wrapper for the `finish-connect` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_finish_connect_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle,
                                                   uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    wasi_input_stream_t input_stream_fd;
    wasi_output_stream_t output_stream_fd;
    int err = 0;

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_finish_connect(socket_fd, &input_stream_fd,
                                    &output_stream_fd, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        HostResource *hr_input_stream = host_resource_create(
            WASI_P2_IO_INPUT_STREAM, sizeof(StreamResourceType));
        ((StreamResourceType *)hr_input_stream->data)->fd = input_stream_fd;
        ((StreamResourceType *)hr_input_stream->data)->type =
            STREAM_TYPE_SOCKET;
        uint32_t input_stream =
            host_resource_table_add(hr_table, hr_input_stream);

        HostResource *hr_output_stream = host_resource_create(
            WASI_P2_IO_OUTPUT_STREAM, sizeof(StreamResourceType));
        ((StreamResourceType *)hr_output_stream->data)->fd = output_stream_fd;
        ((StreamResourceType *)hr_output_stream->data)->type =
            STREAM_TYPE_SOCKET;
        uint32_t output_stream =
            host_resource_table_add(hr_table, hr_output_stream);

        wit_value_t elems[2];
        elems[0] = wit_u32_ctor(input_stream);
        elems[1] = wit_u32_ctor(output_stream);
        wit_value_t result_tuple = wit_tuple_ctor(elems, 2);
        result = wit_result_ctor(false, result_tuple);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `start-listen` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_start_listen_wrapper(wasm_exec_env_t exec_env,
                                                 uint32_t socket_handle,
                                                 uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    uint64_t backlog = ((wasi_socket_context_t *)hr->data)->tcp_listen_backlog;
    int err = wasi_sockets_tcp_start_listen(socket_fd, backlog);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `finish-listen` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_finish_listen_wrapper(wasm_exec_env_t exec_env,
                                                  uint32_t socket_handle,
                                                  uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_tcp_finish_listen(socket_fd);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `accept` method of the `wasi:sockets/tcp.tcp-socket`
 * resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the listening TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_accept_wrapper(wasm_exec_env_t exec_env,
                                           uint32_t socket_handle,
                                           uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_tcp_socket_t new_socket;
    wasi_input_stream_t input_stream;
    wasi_output_stream_t output_stream;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_accept(socket_fd, &new_socket, &input_stream,
                            &output_stream, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        HostResource *hr_new = host_resource_create(
            WASI_P2_TCP_SOCKET, sizeof(wasi_socket_context_t));

        if (!hr_new) {
            result = get_result_error_val(
                WASI_NETWORK_ERROR_CODE_ADDRESS_NOT_BINDABLE);
            goto end;
        }

        ((wasi_socket_context_t *)hr_new->data)->family =
            ((wasi_socket_context_t *)hr->data)->family;
        ((wasi_socket_context_t *)hr_new->data)->fd = new_socket;
        ((wasi_socket_context_t *)hr_new->data)->tcp_listen_backlog = 0;
        host_resource_set_dtor(hr_new, tcp_socket_dtor);

        uint32_t out = host_resource_table_add(hr_table, hr_new);
        if (out < 1) {
            destroy_host_resource(
                hr_new); // Clean up the HostResource on failure
            result = get_result_error_val(
                WASI_NETWORK_ERROR_CODE_ADDRESS_NOT_BINDABLE);
            goto end;
        }

        // In stream
        HostResource *hr_incoming_stream = host_resource_create(
            WASI_P2_IO_INPUT_STREAM, sizeof(StreamResourceType));
        ((StreamResourceType *)hr_incoming_stream->data)->fd = input_stream;
        ((StreamResourceType *)hr_incoming_stream->data)->type =
            STREAM_TYPE_SOCKET;
        host_resource_set_dtor(hr_incoming_stream, tcp_owned_stream_dtor);

        if (!hr_incoming_stream) {
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        ((StreamResourceType *)hr_incoming_stream->data)->fd = input_stream;
        ((StreamResourceType *)hr_incoming_stream->data)->type =
            STREAM_TYPE_SOCKET;

        uint32_t incoming_rep =
            host_resource_table_add(hr_table, hr_incoming_stream);
        if (incoming_rep < 1) {
            destroy_host_resource(hr_incoming_stream);
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        // Out stream
        HostResource *hr_outgoing_stream = host_resource_create(
            WASI_P2_IO_OUTPUT_STREAM, sizeof(StreamResourceType));
        ((StreamResourceType *)hr_outgoing_stream->data)->fd = output_stream;
        ((StreamResourceType *)hr_outgoing_stream->data)->type =
            STREAM_TYPE_SOCKET;
        host_resource_set_dtor(hr_outgoing_stream, tcp_owned_stream_dtor);

        if (!hr_outgoing_stream) {
            destroy_host_resource(hr_incoming_stream);
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        ((StreamResourceType *)hr_outgoing_stream->data)->fd = output_stream;
        ((StreamResourceType *)hr_outgoing_stream->data)->type =
            STREAM_TYPE_SOCKET;

        uint32_t outgoing_rep =
            host_resource_table_add(hr_table, hr_outgoing_stream);
        if (outgoing_rep < 1) {
            destroy_host_resource(hr_incoming_stream);
            destroy_host_resource(hr_outgoing_stream);
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        wit_value_t *elems =
            (wit_value_t *)wasm_runtime_malloc(3 * sizeof(wit_value_t));
        elems[0] = wit_s32_ctor(out);
        elems[1] = wit_s32_ctor(incoming_rep);
        elems[2] = wit_s32_ctor(outgoing_rep);
        wit_value_t result_tuple = wit_tuple_ctor(elems, 3);
        result = wit_result_ctor(false, result_tuple);

        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `local-address` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_local_address_wrapper(wasm_exec_env_t exec_env,
                                                  uint32_t socket_handle,
                                                  uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_ip_socket_address_t local_address;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_local_address(socket_fd, &local_address, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        wit_value_t ip_sock_addr = get_ip_sock_addr_value(&local_address);
        result = wit_result_ctor(false, ip_sock_addr);
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `remote-address` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_remote_address_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle,
                                                   uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    wasi_ip_socket_address_t remote_address;
    int err = 0;

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_remote_address(socket_fd, &remote_address, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        wit_value_t ip_sock_addr = get_ip_sock_addr_value(&remote_address);
        result = wit_result_ctor(false, ip_sock_addr);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `is-listening` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @return `true` if the socket is listening, `false` otherwise.
 */
uint32_t
wasi_sockets_tcp_tcp_socket_is_listening_wrapper(wasm_exec_env_t exec_env,
                                                 uint32_t socket_handle)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        return 0;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not lift tcp socket resource");
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);
    free_wit_value(lifted_handle);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get tcp socket resource");
        return 0;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;

    return wasi_sockets_tcp_is_listening(socket_fd);
}

/**
 * @brief Wrapper for the `address-family` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @return The IP address family of the socket.
 */
uint32_t
wasi_sockets_tcp_tcp_socket_address_family_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        return 0;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not lift tcp socket resource");
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();

    HostResource *socket_hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);
    free_wit_value(lifted_handle);

    if (!socket_hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get tcp socket resource");
        return WASI_IP_ADDRESS_FAMILY_IPV4;
    }

    // Get the actual family from the host resource
    wasi_ip_address_family_t family =
        ((wasi_socket_context_t *)socket_hr->data)->family;
    return family;
}

/**
 * @brief Wrapper for the `set-listen-backlog-size` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param value The listen backlog size.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_set_listen_backlog_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    if (value <= 0) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    if (value > SOMAXCONN)
        value = SOMAXCONN;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    if (wasi_sockets_tcp_is_listening(socket_fd)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    ((wasi_socket_context_t *)hr->data)->tcp_listen_backlog = value;
    result = wit_result_ctor(false, NULL);

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `keep-alive-enabled` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_keep_alive_enabled_wrapper(wasm_exec_env_t exec_env,
                                                       uint32_t socket_handle,
                                                       uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    bool enabled;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_keep_alive_enabled(socket_fd, &enabled, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        wit_value_t ret_val = wit_bool_ctor(enabled);
        result = wit_result_ctor(false, ret_val);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-keep-alive-enabled` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param value A boolean indicating whether to enable keep-alive.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_set_keep_alive_enabled_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_tcp_set_keep_alive_enabled(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `keep-alive-idle-time` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_keep_alive_idle_time_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_duration_t time;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_keep_alive_idle_time(socket_fd, &time, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_u64_ctor(time);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-keep-alive-idle-time` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param value The keep-alive idle time.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_set_keep_alive_idle_time_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_tcp_set_keep_alive_idle_time(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `keep-alive-interval` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_keep_alive_interval_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    wasi_duration_t time;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_keep_alive_interval(socket_fd, &time, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        result = wit_u64_ctor(time);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-keep-alive-interval` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param value The keep-alive interval.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_set_keep_alive_interval_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_tcp_set_keep_alive_interval(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `keep-alive-count` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_keep_alive_count_wrapper(wasm_exec_env_t exec_env,
                                                     uint32_t socket_handle,
                                                     uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    uint32_t count;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_keep_alive_count(socket_fd, &count, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, wit_u32_ctor(count));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-keep-alive-count` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param value The keep-alive count.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_set_keep_alive_count_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_tcp_set_keep_alive_count(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `hop-limit` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_hop_limit_wrapper(wasm_exec_env_t exec_env,
                                              uint32_t socket_handle,
                                              uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    uint8_t limit;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_hop_limit(socket_fd, &limit, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, wit_u8_ctor(limit));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-hop-limit` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param value The hop limit.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_set_hop_limit_wrapper(wasm_exec_env_t exec_env,
                                                  uint32_t socket_handle,
                                                  uint32_t value,
                                                  uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_tcp_set_hop_limit(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `receive-buffer-size` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_receive_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    uint64_t size;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_receive_buffer_size(socket_fd, &size, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, wit_u64_ctor(size));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-receive-buffer-size` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param value The receive buffer size.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_set_receive_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_tcp_set_receive_buffer_size(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `send-buffer-size` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_send_buffer_size_wrapper(wasm_exec_env_t exec_env,
                                                     uint32_t socket_handle,
                                                     uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    uint64_t size;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_tcp_send_buffer_size(socket_fd, &size, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, wit_u64_ctor(size));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-send-buffer-size` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param value The send buffer size.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_set_send_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_tcp_set_send_buffer_size(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `subscribe` method of the
 * `wasi:sockets/tcp.tcp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The rep of the TCP socket resource.
 * @return A new pollable resource rep.
 */
uint32_t
wasi_sockets_tcp_tcp_socket_subscribe_wrapper(wasm_exec_env_t exec_env,
                                              uint32_t socket_handle)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        return 0;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle))
        return 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get tcp socket resource");
        return 0;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    HostResource *hr_poll =
        host_resource_create(WASI_P2_POLLABLE, sizeof(wasi_pollable_context_t));

    if (!hr_poll) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create pollable stream resource");
        return 0;
    }

    host_resource_set_dtor(hr_poll, pollable_dtor);
    SET_POLLABLE_CTX((wasi_pollable_context_t *)hr_poll->data, socket_fd, false,
                     WASI_POLLABLE_SOCK);

    uint32_t index_rep = host_resource_table_add(hr_table, hr_poll);
    if (index_rep < 1) {
        destroy_host_resource(hr_poll); // Clean up the HostResource on failure
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not add output stream resource to HR table");
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(index_rep);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &index_rep);

    free_wit_value(out_val);
    return index_rep;
}

/**
 * @brief Wrapper for the `shutdown` method of the `wasi:sockets/tcp.tcp-socket`
 * resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the TCP socket.
 * @param shutdown_type The type of shutdown to perform.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_tcp_tcp_socket_shutdown_wrapper(wasm_exec_env_t exec_env,
                                             uint32_t socket_handle,
                                             uint32_t shutdown_type,
                                             uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->tcp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_tcp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_tcp_shutdown(socket_fd, shutdown_type);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/* wasi:sockets/udp-create-socket */

/**
 * @brief Wrapper for the `create-udp-socket` function of the
 * `wasi:sockets/udp-create-socket` interface.
 * @param exec_env The execution environment.
 * @param address_family The IP address family.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_create_socket_create_udp_socket_wrapper(
    wasm_exec_env_t exec_env, uint32_t address_family, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    wasi_udp_socket_t socket;
    int err = 0;

    wasi_sockets_create_udp_socket(address_family, &socket, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        HostResourceTable *hr_table = get_global_host_resource_table();
        HostResource *hr = host_resource_create(WASI_P2_UDP_SOCKET,
                                                sizeof(wasi_socket_context_t));

        if (!hr) {
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        ((wasi_socket_context_t *)hr->data)->family = address_family;
        ((wasi_socket_context_t *)hr->data)->fd = socket;
        host_resource_set_dtor(hr, udp_socket_dtor);

        uint32_t index_rep = host_resource_table_add(hr_table, hr);
        if (index_rep < 1) {
            destroy_host_resource(hr); // Clean up the HostResource on failure
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }
        wit_value_t out_rep = wit_u32_ctor(index_rep);
        result = wit_result_ctor(false, out_rep);
        goto end;
    }
end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
}

/* wasi:sockets/udp */

/**
 * @brief Wrapper for the `start-bind` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param network_handle The handle of the network resource.
 * @param addr_tag The tag of the IP socket address.
 * @param ... The flattened fields of the IP socket address.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_start_bind_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t network_handle,
    uint32_t addr_tag, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3,
    uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8,
    uint32_t p9, uint32_t p10, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_network_handle = NULL;
    wit_value_t lifted_socket_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, network_handle,
            func_type->params->params[1].type->type_specific.resource_handle,
            &lifted_network_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_socket_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_ip_socket_address_t local_address;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();

    HostResource *socket_hr = host_resource_table_get(
        hr_table, lifted_socket_handle->value.u32_value);
    HostResource *network_hr = host_resource_table_get(
        hr_table, lifted_network_handle->value.u32_value);

    if (!(socket_hr && network_hr)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_network_t network_fd = *((uint32_t *)network_hr->data);
    wasi_udp_socket_t socket_fd =
        ((wasi_socket_context_t *)socket_hr->data)->fd;

    read_flattened_ip_socket_address(&local_address, addr_tag, p0, p1, p2, p3,
                                     p4, p5, p6, p7, p8, p9, p10);
    err = wasi_sockets_udp_start_bind(socket_fd, network_fd, &local_address);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_network_handle);
    free_wit_value(lifted_socket_handle);
}

/**
 * @brief Wrapper for the `finish-bind` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_finish_bind_wrapper(wasm_exec_env_t exec_env,
                                                uint32_t socket_handle,
                                                uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *socket_hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!socket_hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    // Get the actual socket fd from the host resource
    wasi_udp_socket_t socket_fd =
        ((wasi_socket_context_t *)socket_hr->data)->fd;

    int err = wasi_sockets_udp_finish_bind(socket_fd);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `stream` method of the `wasi:sockets/udp.udp-socket`
 * resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param remote_addr_tag The tag of the optional remote address.
 * @param ... The flattened fields of the optional remote address.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_stream_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t remote_addr_tag,
    uint32_t addr_tag, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3,
    uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8,
    uint32_t p9, uint32_t p10, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    wasi_ip_socket_address_t remote_address;
    wasi_incoming_datagram_stream_t incoming_stream;
    wasi_outgoing_datagram_stream_t outgoing_stream;
    int err = 0;

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *socket_hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!socket_hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t socket_fd =
        ((wasi_socket_context_t *)socket_hr->data)->fd;
    const wasi_ip_socket_address_t *remote_address_ptr = NULL;

    if (remote_addr_tag) {
        read_flattened_ip_socket_address(&remote_address, addr_tag, p0, p1, p2,
                                         p3, p4, p5, p6, p7, p8, p9, p10);
        remote_address_ptr = &remote_address;
    }

    wasi_sockets_udp_stream(socket_fd, remote_address_ptr, &incoming_stream,
                            &outgoing_stream, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        // In stream
        HostResource *hr_incoming_stream = host_resource_create(
            WASI_P2_UDP_INCOMING_DATAGRAM_STREAM, sizeof(incoming_stream));
        *((uint32_t *)hr_incoming_stream->data) = incoming_stream;
        host_resource_set_dtor(hr_incoming_stream, udp_datagram_stream_dtor);

        if (!hr_incoming_stream) {
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        *((uint32_t *)hr_incoming_stream->data) = incoming_stream;

        uint32_t incoming_rep =
            host_resource_table_add(hr_table, hr_incoming_stream);
        if (incoming_rep < 1) {
            destroy_host_resource(hr_incoming_stream);
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        // Out stream — fcntl(F_DUPFD_CLOEXEC) fd from udp_stream(), owned
        HostResource *hr_outgoing_stream = host_resource_create(
            WASI_P2_UDP_OUTGOING_DATAGRAM_STREAM, sizeof(outgoing_stream));
        *((uint32_t *)hr_outgoing_stream->data) = outgoing_stream;
        host_resource_set_dtor(hr_outgoing_stream, udp_datagram_stream_dtor);

        if (!hr_outgoing_stream) {
            destroy_host_resource(hr_incoming_stream);
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        *((uint32_t *)hr_outgoing_stream->data) = outgoing_stream;

        uint32_t outgoing_rep =
            host_resource_table_add(hr_table, hr_outgoing_stream);
        if (outgoing_rep < 1) {
            destroy_host_resource(hr_incoming_stream);
            destroy_host_resource(hr_outgoing_stream);
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }

        wit_value_t *elems = wasm_runtime_malloc(2 * sizeof(wit_value_t));
        elems[0] = wit_u32_ctor(incoming_rep);
        elems[1] = wit_u32_ctor(outgoing_rep);
        wit_value_t result_tuple = wit_tuple_ctor(elems, 2);
        result = wit_result_ctor(false, result_tuple);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `local-address` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_local_address_wrapper(wasm_exec_env_t exec_env,
                                                  uint32_t socket_handle,
                                                  uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_ip_socket_address_t local_address;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_udp_local_address(socket_fd, &local_address, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        wit_value_t ip_sock_addr = get_ip_sock_addr_value(&local_address);
        result = wit_result_ctor(false, ip_sock_addr);
        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `remote-address` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_remote_address_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle,
                                                   uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_ip_socket_address_t remote_address;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_udp_remote_address(socket_fd, &remote_address, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }
    else {
        wit_value_t ip_sock_addr = get_ip_sock_addr_value(&remote_address);
        result = wit_result_ctor(false, ip_sock_addr);

        goto end;
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `address-family` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @return The IP address family of the socket.
 */
uint32_t
wasi_sockets_udp_udp_socket_address_family_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();

    HostResource *socket_hr = host_resource_table_get(hr_table, socket_handle);

    if (socket_hr) {
        // Get the actual family from the host resource
        wasi_ip_address_family_t family =
            ((wasi_socket_context_t *)socket_hr->data)->family;
        return family;
    }

    // According to the wit, this should not fail
    return WASI_IP_ADDRESS_FAMILY_IPV4;
}

/**
 * @brief Wrapper for the `unicast-hop-limit` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_unicast_hop_limit_wrapper(wasm_exec_env_t exec_env,
                                                      uint32_t socket_handle,
                                                      uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    uint8_t limit;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_udp_unicast_hop_limit(socket_fd, &limit, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, wit_u8_ctor(limit));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-unicast-hop-limit` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param value The unicast hop limit.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_set_unicast_hop_limit_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_udp_set_unicast_hop_limit(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `receive-buffer-size` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_receive_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    uint64_t size;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_udp_receive_buffer_size(socket_fd, &size, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, wit_u64_ctor(size));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-receive-buffer-size` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param value The receive buffer size.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_set_receive_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_udp_set_receive_buffer_size(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `send-buffer-size` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_send_buffer_size_wrapper(wasm_exec_env_t exec_env,
                                                     uint32_t socket_handle,
                                                     uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    uint64_t size;
    int err = 0;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    wasi_sockets_udp_send_buffer_size(socket_fd, &size, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, wit_u64_ctor(size));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `set-send-buffer-size` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @param value The send buffer size.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_udp_socket_set_send_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;
    int err = wasi_sockets_udp_set_send_buffer_size(socket_fd, value);
    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, NULL);
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `subscribe` method of the
 * `wasi:sockets/udp.udp-socket` resource.
 * @param exec_env The execution environment.
 * @param socket_handle The handle of the UDP socket.
 * @return A new pollable handle.
 */
uint32_t
wasi_sockets_udp_udp_socket_subscribe_wrapper(wasm_exec_env_t exec_env,
                                              uint32_t socket_handle)
{
    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;

    if (!lift_borrow(
            exec_env->cx, socket_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not lift udp socket resource");
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);
    free_wit_value(lifted_handle);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get udp socket resource");
        return 0;
    }

    wasi_udp_socket_t socket_fd = ((wasi_socket_context_t *)hr->data)->fd;

    HostResource *hr_poll =
        host_resource_create(WASI_P2_POLLABLE, sizeof(wasi_pollable_context_t));

    if (!hr_poll) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create pollable stream resource");
        return 0;
    }

    host_resource_set_dtor(hr_poll, pollable_dtor);
    SET_POLLABLE_CTX((wasi_pollable_context_t *)hr_poll->data, socket_fd, false,
                     WASI_POLLABLE_SOCK);

    uint32_t index_rep = host_resource_table_add(hr_table, hr_poll);
    if (index_rep < 1) {
        destroy_host_resource(hr_poll); // Clean up the HostResource on failure
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not add output stream resource to HR table");
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(index_rep);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &index_rep);

    free_wit_value(out_val);
    return index_rep;
}

/**
 * @brief Wrapper for the `receive` method of the
 * `wasi:sockets/udp.incoming-datagram-stream` resource.
 * @param exec_env The execution environment.
 * @param stream_handle The handle of the incoming datagram stream.
 * @param max_results The maximum number of datagrams to receive.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_incoming_datagram_stream_receive_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle, int64_t max_results,
    uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    if (!lift_borrow(
            exec_env->cx, stream_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_incoming_datagram_t *datagrams = NULL;
    uint64_t datagrams_len = 0;
    int err = 0;
    uint64_t i;

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t stream_fd = ((wasi_socket_context_t *)hr->data)->fd;

    wasi_sockets_udp_receive(stream_fd, max_results, &datagrams, &datagrams_len,
                             &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
        goto end;
    }

    wit_value_t *elems = NULL;
    if (datagrams_len)
        elems = (wit_value_t *)wasm_runtime_malloc(datagrams_len
                                                   * sizeof(wit_value_t));
    for (uint32_t idx = 0; idx < datagrams_len; idx++) {
        elems[idx] = get_incoming_datagram_value(&datagrams[idx]);
    }
    wit_value_t data = wit_list_ctor(elems, datagrams_len);
    result = wit_result_ctor(false, data);
    goto end;

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    if (datagrams) {
        for (i = 0; i < datagrams_len; i++) {
            if (datagrams[i].data) {
                wasm_runtime_free(datagrams[i].data);
            }
        }
        wasm_runtime_free(datagrams);
    }
    free_wit_value(result);
}

/**
 * @brief Wrapper for the `subscribe` method of the
 * `wasi:sockets/udp.incoming-datagram-stream` resource.
 * @param exec_env The execution environment.
 * @param stream_handle The handle of the incoming datagram stream.
 * @return A new pollable handle.
 */
uint32_t
wasi_sockets_udp_incoming_datagram_stream_subscribe_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        return 0;
    }

    wit_value_t lifted_handle = NULL;

    if (!lift_borrow(
            exec_env->cx, stream_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not get udp stream incoming resource");
        return 0;
    }

    wasi_udp_socket_t stream_fd = ((wasi_socket_context_t *)hr->data)->fd;

    HostResource *hr_poll =
        host_resource_create(WASI_P2_POLLABLE, sizeof(wasi_pollable_context_t));

    if (!hr_poll) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create new pollable resource");
        return 0;
    }

    host_resource_set_dtor(hr_poll, pollable_dtor);
    SET_INPUT_POLLABLE((wasi_pollable_context_t *)hr_poll->data, stream_fd,
                       false);

    uint32_t index_rep = host_resource_table_add(hr_table, hr_poll);
    if (index_rep < 1) {
        destroy_host_resource(hr_poll); // Clean up the HostResource on failure
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not add pollable resource to HR table");
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(index_rep);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &index_rep);

    free_wit_value(out_val);
    return index_rep;
}

/**
 * @brief Wrapper for the `check-send` method of the
 * `wasi:sockets/udp.outgoing-datagram-stream` resource.
 * @param exec_env The execution environment.
 * @param stream_handle The handle of the outgoing datagram stream.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_outgoing_datagram_stream_check_send_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle, uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t lifted_handle = NULL;
    wit_value_t result = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    uint64_t count;
    int err = 0;

    if (!lift_borrow(
            exec_env->cx, stream_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t stream_fd = ((wasi_socket_context_t *)hr->data)->fd;

    wasi_sockets_udp_check_send(stream_fd, &count, &err);

    if (err != 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, wit_u64_ctor(count));
    }

end:
    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `send` method of the
 * `wasi:sockets/udp.outgoing-datagram-stream` resource.
 * @param exec_env The execution environment.
 * @param stream_handle The handle of the outgoing datagram stream.
 * @param datagrams_ptr A pointer to a list of outgoing datagrams in the guest's
 * memory.
 * @param datagrams_len The number of datagrams in the list.
 * @param[out] offset_addr Memory offset where the result will be stored.
 */
void
wasi_sockets_udp_outgoing_datagram_stream_send_wrapper(wasm_exec_env_t exec_env,
                                                       uint32_t stream_handle,
                                                       uint32_t datagrams_ptr,
                                                       uint32_t datagrams_len,
                                                       uint32_t offset_addr)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    wit_value_t result = NULL;
    wit_value_t lifted_handle = NULL;

    wasi_outgoing_datagram_t *datagrams_native = NULL;

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
        goto end;
    }

    uint64_t sent_count = 0;
    int err = 0;
    uint64_t i = 0;

    wit_value_t datagrams_val;
    load_list_from_range(
        exec_env->cx, datagrams_ptr, datagrams_len,
        func_type->params->params[1].type->type_specific.list->element_type,
        &datagrams_val);

    if (datagrams_len > 0) {
        uint32_t datagrams_native_size =
            datagrams_len * sizeof(wasi_outgoing_datagram_t);
        datagrams_native = wasm_runtime_malloc(datagrams_native_size);
        if (!datagrams_native) {
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY);
            goto end;
        }
        if (datagrams_len != datagrams_val->value.list_value.size) {
            result =
                get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
            goto end;
        }
        memset(datagrams_native, 0, datagrams_native_size);
    }

    for (i = 0; i < datagrams_val->value.list_value.size; i++) {
        ComponentWITRecord outgoing_datagram =
            datagrams_val->value.list_value.elems[i]->value.record_value;
        uint32_t data_size =
            outgoing_datagram.fields[0].value->value.list_value.size;
        uint32_t data_idx;
        uint8_t *data = wasm_runtime_malloc(data_size * sizeof(uint8_t));
        for (data_idx = 0; data_idx < data_size; data_idx++) {
            data[data_idx] = outgoing_datagram.fields[0]
                                 .value->value.list_value.elems[data_idx]
                                 ->value.u8_value;
        }
        datagrams_native[i].data_len = data_size;
        datagrams_native[i].data = data;

        ComponentWITVariant remote_address =
            outgoing_datagram.fields[1]
                .value->value.option_value.optional_elem->value.variant_value;
        if (!strncmp(remote_address.discriminator, "ipv4",
                     remote_address.discriminator_size)) {
            datagrams_native[i].remote_address =
                wasm_runtime_malloc(sizeof(wasi_ip_socket_address_t));
            datagrams_native[i].remote_address->tag =
                WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
            datagrams_native[i].remote_address->val.ipv4.port =
                remote_address.value->value.record_value.fields[0]
                    .value->value.u16_value;
            datagrams_native[i].remote_address->val.ipv4.address.f0 =
                remote_address.value->value.record_value.fields[1]
                    .value->value.tuple_value.elems[0]
                    ->value.u8_value;
            datagrams_native[i].remote_address->val.ipv4.address.f1 =
                remote_address.value->value.record_value.fields[1]
                    .value->value.tuple_value.elems[1]
                    ->value.u8_value;
            datagrams_native[i].remote_address->val.ipv4.address.f2 =
                remote_address.value->value.record_value.fields[1]
                    .value->value.tuple_value.elems[2]
                    ->value.u8_value;
            datagrams_native[i].remote_address->val.ipv4.address.f3 =
                remote_address.value->value.record_value.fields[1]
                    .value->value.tuple_value.elems[3]
                    ->value.u8_value;
        }
        else if (!strncmp(remote_address.discriminator, "ipv6",
                          remote_address.discriminator_size)) {
            datagrams_native[i].remote_address =
                wasm_runtime_malloc(sizeof(wasi_ip_socket_address_t));
            datagrams_native[i].remote_address->tag =
                WASI_IP_SOCKET_ADDRESS_TAG_IPV6;
            datagrams_native[i].remote_address->val.ipv6.port =
                remote_address.value->value.record_value.fields[0]
                    .value->value.u16_value;
            datagrams_native[i].remote_address->val.ipv6.flow_info =
                remote_address.value->value.record_value.fields[1]
                    .value->value.u32_value;
            datagrams_native[i].remote_address->val.ipv6.address.f0 =
                remote_address.value->value.record_value.fields[2]
                    .value->value.tuple_value.elems[0]
                    ->value.u16_value;
            datagrams_native[i].remote_address->val.ipv6.address.f1 =
                remote_address.value->value.record_value.fields[2]
                    .value->value.tuple_value.elems[1]
                    ->value.u16_value;
            datagrams_native[i].remote_address->val.ipv6.address.f2 =
                remote_address.value->value.record_value.fields[2]
                    .value->value.tuple_value.elems[2]
                    ->value.u16_value;
            datagrams_native[i].remote_address->val.ipv6.address.f3 =
                remote_address.value->value.record_value.fields[2]
                    .value->value.tuple_value.elems[3]
                    ->value.u16_value;
            datagrams_native[i].remote_address->val.ipv6.address.f4 =
                remote_address.value->value.record_value.fields[2]
                    .value->value.tuple_value.elems[4]
                    ->value.u16_value;
            datagrams_native[i].remote_address->val.ipv6.address.f5 =
                remote_address.value->value.record_value.fields[2]
                    .value->value.tuple_value.elems[5]
                    ->value.u16_value;
            datagrams_native[i].remote_address->val.ipv6.address.f6 =
                remote_address.value->value.record_value.fields[2]
                    .value->value.tuple_value.elems[6]
                    ->value.u16_value;
            datagrams_native[i].remote_address->val.ipv6.address.f7 =
                remote_address.value->value.record_value.fields[2]
                    .value->value.tuple_value.elems[7]
                    ->value.u16_value;
            datagrams_native[i].remote_address->val.ipv6.scope_id =
                remote_address.value->value.record_value.fields[3]
                    .value->value.u32_value;
        }
    }

    if (!lift_borrow(
            exec_env->cx, stream_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        result = get_result_error_val(WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT);
        goto end;
    }

    wasi_udp_socket_t stream_fd = ((wasi_socket_context_t *)hr->data)->fd;

    wasi_sockets_udp_send(stream_fd, datagrams_native, datagrams_len,
                          &sent_count, &err);

    if (err >= 0) {
        result = get_result_error_val(errno_to_wasi_network(err));
    }
    else {
        result = wit_result_ctor(false, wit_u64_ctor(sent_count));
    }

end:
    if (datagrams_native) {
        for (uint64_t j = 0; j < i; j++) {
            if (datagrams_native[j].remote_address) {
                wasm_runtime_free((void *)datagrams_native[j].remote_address);
            }
        }
        wasm_runtime_free(datagrams_native);
    }

    store(exec_env->cx, offset_addr, func_type->results->result, result);
    free_wit_value(result);
    free_wit_value(lifted_handle);
}

/**
 * @brief Wrapper for the `subscribe` method of the
 * `wasi:sockets/udp.outgoing-datagram-stream` resource.
 * @param exec_env The execution environment.
 * @param stream_handle The handle of the outgoing datagram stream.
 * @return A new pollable handle.
 */
uint32_t
wasi_sockets_udp_outgoing_datagram_stream_subscribe_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle)
{

    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    const WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(module_inst);

    WASMComponentFuncTypeInstance *func_type =
        wasm_get_component_func_type(exec_env);

    if (!wasi_ctx->wasi_options->cli || !wasi_ctx->wasi_options->common
        || !wasi_ctx->wasi_options->inherit_network
        || !wasi_ctx->wasi_options->udp) {
        return 0;
    }

    wit_value_t lifted_handle = NULL;

    if (!lift_borrow(
            exec_env->cx, stream_handle,
            func_type->params->params[0].type->type_specific.resource_handle,
            &lifted_handle)) {
        return 0;
    }

    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(
        hr_table, lifted_handle->value.resource_value.value);

    if (!hr) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not get resource outgoing stream");
        return 0;
    }

    wasi_udp_socket_t stream_fd = ((wasi_socket_context_t *)hr->data)->fd;

    HostResource *hr_poll =
        host_resource_create(WASI_P2_POLLABLE, sizeof(wasi_pollable_context_t));

    if (!hr_poll) {
        wasm_runtime_set_exception(exec_env->module_inst,
                                   "Could not create pollable resource");
        return 0;
    }

    host_resource_set_dtor(hr_poll, pollable_dtor);
    SET_OUTPUT_POLLABLE((wasi_pollable_context_t *)hr_poll->data, stream_fd,
                        false);

    uint32_t index_rep = host_resource_table_add(hr_table, hr_poll);
    if (index_rep < 1) {
        wasm_runtime_set_exception(
            exec_env->module_inst,
            "Could not add pollable resource to HR table");
        destroy_host_resource(hr_poll); // Clean up the HostResource on failure
        return 0;
    }

    wit_value_t out_val = wit_u32_ctor(index_rep);
    lower_own(exec_env->cx,
              func_type->results->result[0].type_specific.resource_handle,
              out_val, &index_rep);

    free_wit_value(out_val);

    return index_rep;
}