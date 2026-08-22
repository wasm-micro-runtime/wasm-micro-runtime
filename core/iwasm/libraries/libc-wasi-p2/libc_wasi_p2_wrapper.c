/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* splice(2) below is a GNU extension; request it before any system header
   (see wasi_p2_io.c). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "libc_wasi_p2_wrapper.h"
#include "wasm_native.h"
#include "wasi_p2_cli_wrapper.h"
#include "wasi_p2_clocks_wrapper.h"
#include "wasi_p2_filesystem_wrapper.h"
#include "wasi_p2_random_wrapper.h"
#include "wasi_p2_io_wrapper.h"
#include "wasi_p2_sockets_wrapper.h"
#include "wasm_export.h"
#include <errno.h>

static bool
is_module_equal(const char *module_name, const char *registered_module_name)
{
    const char *at_module = strchr(module_name, '@');
    size_t module_len =
        at_module ? (size_t)(at_module - module_name) : strlen(module_name);

    const char *at_registered = strchr(registered_module_name, '@');
    size_t registered_len =
        at_registered ? (size_t)(at_registered - registered_module_name)
                      : strlen(registered_module_name);

    if (module_len != registered_len) {
        return false;
    }

    return strncmp(module_name, registered_module_name, module_len) == 0;
}

/**
 * @brief A comparison function for qsort to sort native symbols by name.
 * @param native_symbol1 The first native symbol.
 * @param native_symbol2 The second native symbol.
 * @return An integer less than, equal to, or greater than zero if the first
 *         symbol is found, respectively, to be less than, to match, or be
 *         greater than the second.
 */
static int
native_symbol_cmp(const void *native_symbol1, const void *native_symbol2)
{
    return strcmp(((const NativeSymbol *)native_symbol1)->symbol,
                  ((const NativeSymbol *)native_symbol2)->symbol);
}

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A macro to define a native symbol for a WASI P2 function.
 * @details This macro creates a NativeSymbol struct with the given name, a
 *          pointer to the wrapper function, and the function signature.
 * @param name The name of the WASI function.
 * @param func The name of the wrapper function.
 * @param sig The function signature.
 */
#define REG_WASI_P2_FUNCTION(name, func, sig)   \
    {                                           \
        name, (void *)func##_wrapper, sig, NULL \
    }

/**
 * @brief A macro to define a WASI P2 module.
 * @details This macro creates a wasi_p2_module_t struct with the module name,
 *          a pointer to the native symbols array, and the number of symbols.
 * @param name The name of the module.
 * @param name_str The string representation of the module name.
 */
#define WASI_P2_MODULE(name, name_str, ver)                    \
    {                                                          \
        "wasi:" name_str, ver, (NativeSymbol *)name##_symbols, \
            sizeof(name##_symbols) / sizeof(NativeSymbol)      \
    }

static NativeSymbol cli_environment_symbols[] = {
    REG_WASI_P2_FUNCTION("get-environment", wasi_cli_get_environment, "(i)"),
    REG_WASI_P2_FUNCTION("get-arguments", wasi_cli_get_arguments, "(i)"),
    REG_WASI_P2_FUNCTION("initial-cwd", wasi_cli_initial_cwd, "(i)"),
};

static NativeSymbol cli_exit_symbols[] = {
    REG_WASI_P2_FUNCTION("exit", wasi_cli_exit, "(i)"),
};

static NativeSymbol cli_stdin_symbols[] = {
    REG_WASI_P2_FUNCTION("get-stdin", wasi_cli_get_stdin, "()i"),
};

static NativeSymbol cli_stdout_symbols[] = {
    REG_WASI_P2_FUNCTION("get-stdout", wasi_cli_get_stdout, "()i"),
};

static NativeSymbol cli_stderr_symbols[] = {
    REG_WASI_P2_FUNCTION("get-stderr", wasi_cli_get_stderr, "()i"),
};

static NativeSymbol cli_terminal_stdin_symbols[] = {
    REG_WASI_P2_FUNCTION("get-terminal-stdin", wasi_cli_get_terminal_stdin,
                         "(i)"),
};

static NativeSymbol cli_terminal_stdout_symbols[] = {
    REG_WASI_P2_FUNCTION("get-terminal-stdout", wasi_cli_get_terminal_stdout,
                         "(i)"),
};

static NativeSymbol cli_terminal_stderr_symbols[] = {
    REG_WASI_P2_FUNCTION("get-terminal-stderr", wasi_cli_get_terminal_stderr,
                         "(i)"),
};

static NativeSymbol clocks_monotonic_clock_symbols[] = {
    REG_WASI_P2_FUNCTION("now", wasi_monotonic_clock_now, "()I"),
    REG_WASI_P2_FUNCTION("resolution", wasi_monotonic_clock_resolution, "()I"),
    REG_WASI_P2_FUNCTION("subscribe-instant",
                         wasi_monotonic_clock_subscribe_instant, "(I)i"),
    REG_WASI_P2_FUNCTION("subscribe-duration",
                         wasi_monotonic_clock_subscribe_duration, "(I)i"),
};

static NativeSymbol clocks_wall_clock_symbols[] = {
    REG_WASI_P2_FUNCTION("now", wasi_wall_clock_now, "(i)"),
    REG_WASI_P2_FUNCTION("resolution", wasi_wall_clock_resolution, "(i)"),
};

static NativeSymbol filesystem_preopens_symbols[] = {
    REG_WASI_P2_FUNCTION("get-directories", wasi_filesystem_get_directories,
                         "(i)"),
};

static NativeSymbol filesystem_types_symbols[] = {
    REG_WASI_P2_FUNCTION("[method]descriptor.read-via-stream",
                         wasi_filesystem_read_via_stream, "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.write-via-stream",
                         wasi_filesystem_write_via_stream, "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.append-via-stream",
                         wasi_filesystem_append_via_stream, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.advise", wasi_filesystem_advise,
                         "(iIIii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.sync-data",
                         wasi_filesystem_sync_data, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.get-flags",
                         wasi_filesystem_get_flags, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.get-type",
                         wasi_filesystem_get_type, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.set-size",
                         wasi_filesystem_set_size, "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.set-times",
                         wasi_filesystem_set_times, "(iiIiiIii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.read", wasi_filesystem_read,
                         "(iIIi)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.write", wasi_filesystem_write,
                         "(i*~Ii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.read-directory",
                         wasi_filesystem_read_directory, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.sync", wasi_filesystem_sync,
                         "(ii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.create-directory-at",
                         wasi_filesystem_create_directory_at, "(ii~i)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.stat", wasi_filesystem_stat,
                         "(ii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.stat-at", wasi_filesystem_stat_at,
                         "(iii~i)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.set-times-at",
                         wasi_filesystem_set_times_at, "(iii~iIiiIii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.link-at", wasi_filesystem_link_at,
                         "(iii~ii~i)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.open-at", wasi_filesystem_open_at,
                         "(iii~iii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.readlink-at",
                         wasi_filesystem_readlink_at, "(ii~i)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.remove-directory-at",
                         wasi_filesystem_remove_directory_at, "(ii~i)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.rename-at",
                         wasi_filesystem_rename_at, "(ii~ii~i)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.symlink-at",
                         wasi_filesystem_symlink_at, "(ii~i~i)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.unlink-file-at",
                         wasi_filesystem_unlink_file_at, "(ii~i)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.is-same-object",
                         wasi_filesystem_is_same_object, "(ii)i"),
    REG_WASI_P2_FUNCTION("[method]descriptor.metadata-hash",
                         wasi_filesystem_metadata_hash, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]descriptor.metadata-hash-at",
                         wasi_filesystem_metadata_hash_at, "(iii~i)"),
    REG_WASI_P2_FUNCTION("[method]directory-entry-stream.read-directory-entry",
                         wasi_filesystem_read_directory_entry, "(Ii)"),
    REG_WASI_P2_FUNCTION("filesystem-error-code",
                         wasi_filesystem_filesystem_error_code, "(ii)"),
};

static NativeSymbol random_random_symbols[] = {
    REG_WASI_P2_FUNCTION("get-random-bytes", wasi_random_get_random_bytes,
                         "(Ii)"),
    REG_WASI_P2_FUNCTION("get-random-u64", wasi_random_get_random_u64, "()I"),
};

static NativeSymbol random_insecure_symbols[] = {
    REG_WASI_P2_FUNCTION("get-insecure-random-bytes",
                         wasi_random_get_insecure_random_bytes, "(Ii)"),
    REG_WASI_P2_FUNCTION("get-insecure-random-u64",
                         wasi_random_get_insecure_random_u64, "()I"),
};

static NativeSymbol random_insecure_seed_symbols[] = {
    REG_WASI_P2_FUNCTION("insecure-seed", wasi_random_insecure_seed, "(i)"),
};

static NativeSymbol io_error_symbols[] = {
    REG_WASI_P2_FUNCTION("[method]error.to-debug-string",
                         wasi_io_error_to_debug_string, "(ii)"),
};

static NativeSymbol io_poll_symbols[] = {
    REG_WASI_P2_FUNCTION("[method]pollable.ready", wasi_io_poll_pollable_ready,
                         "(i)i"),
    REG_WASI_P2_FUNCTION("[method]pollable.block", wasi_io_poll_pollable_block,
                         "(i)"),
    REG_WASI_P2_FUNCTION("poll", wasi_io_poll_poll, "(iii)"),
};

static NativeSymbol io_streams_symbols[] = {
    REG_WASI_P2_FUNCTION("[method]input-stream.read",
                         wasi_io_streams_input_stream_read, "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]input-stream.blocking-read",
                         wasi_io_streams_input_stream_blocking_read, "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]input-stream.skip",
                         wasi_io_streams_input_stream_skip, "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]input-stream.blocking-skip",
                         wasi_io_streams_input_stream_blocking_skip, "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]input-stream.subscribe",
                         wasi_io_streams_input_stream_subscribe, "(i)i"),
    REG_WASI_P2_FUNCTION("[method]output-stream.check-write",
                         wasi_io_streams_output_stream_check_write, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]output-stream.write",
                         wasi_io_streams_output_stream_write, "(ii~i)"),
    REG_WASI_P2_FUNCTION("[method]output-stream.blocking-write-and-flush",
                         wasi_io_streams_output_stream_blocking_write_and_flush,
                         "(ii~i)"),
    REG_WASI_P2_FUNCTION("[method]output-stream.flush",
                         wasi_io_streams_output_stream_flush, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]output-stream.blocking-flush",
                         wasi_io_streams_output_stream_blocking_flush, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]output-stream.subscribe",
                         wasi_io_streams_output_stream_subscribe, "(i)i"),
    REG_WASI_P2_FUNCTION("[method]output-stream.write-zeroes",
                         wasi_io_streams_output_stream_write_zeroes, "(iIi)"),
    REG_WASI_P2_FUNCTION(
        "[method]output-stream.blocking-write-zeroes-and-flush",
        wasi_io_streams_output_stream_blocking_write_zeroes_and_flush, "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]output-stream.splice",
                         wasi_io_streams_output_stream_splice, "(iiIi)"),
    REG_WASI_P2_FUNCTION("[method]output-stream.blocking-splice",
                         wasi_io_streams_output_stream_blocking_splice,
                         "(iiIi)"),
};

static NativeSymbol sockets_instance_network_symbols[] = {
    REG_WASI_P2_FUNCTION("instance-network",
                         wasi_sockets_instance_network_instance_network, "()i"),
};

static NativeSymbol sockets_ip_name_lookup_symbols[] = {
    REG_WASI_P2_FUNCTION("resolve-addresses",
                         wasi_sockets_ip_name_lookup_resolve_addresses,
                         "(ii~i)"),
    REG_WASI_P2_FUNCTION(
        "[method]resolve-address-stream.resolve-next-address",
        wasi_sockets_ip_name_lookup_resolve_address_stream_resolve_next_address,
        "(ii)"),
    REG_WASI_P2_FUNCTION(
        "[method]resolve-address-stream.subscribe",
        wasi_sockets_ip_name_lookup_resolve_address_stream_subscribe, "(i)"),
};

static NativeSymbol sockets_tcp_create_socket_symbols[] = {
    REG_WASI_P2_FUNCTION("create-tcp-socket",
                         wasi_sockets_tcp_create_socket_create_tcp_socket,
                         "(ii)"),
};

static NativeSymbol sockets_tcp_symbols[] = {
    REG_WASI_P2_FUNCTION("[method]tcp-socket.start-bind",
                         wasi_sockets_tcp_tcp_socket_start_bind,
                         "(iiiiiiiiiiiiiii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.finish-bind",
                         wasi_sockets_tcp_tcp_socket_finish_bind, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.start-connect",
                         wasi_sockets_tcp_tcp_socket_start_connect,
                         "(iiiiiiiiiiiiiii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.finish-connect",
                         wasi_sockets_tcp_tcp_socket_finish_connect, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.start-listen",
                         wasi_sockets_tcp_tcp_socket_start_listen, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.finish-listen",
                         wasi_sockets_tcp_tcp_socket_finish_listen, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.accept",
                         wasi_sockets_tcp_tcp_socket_accept, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.local-address",
                         wasi_sockets_tcp_tcp_socket_local_address, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.remote-address",
                         wasi_sockets_tcp_tcp_socket_remote_address, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.is-listening",
                         wasi_sockets_tcp_tcp_socket_is_listening, "(i)i"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.address-family",
                         wasi_sockets_tcp_tcp_socket_address_family, "(i)i"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.set-listen-backlog-size",
                         wasi_sockets_tcp_tcp_socket_set_listen_backlog_size,
                         "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.keep-alive-enabled",
                         wasi_sockets_tcp_tcp_socket_keep_alive_enabled,
                         "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.set-keep-alive-enabled",
                         wasi_sockets_tcp_tcp_socket_set_keep_alive_enabled,
                         "(iii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.keep-alive-idle-time",
                         wasi_sockets_tcp_tcp_socket_keep_alive_idle_time,
                         "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.set-keep-alive-idle-time",
                         wasi_sockets_tcp_tcp_socket_set_keep_alive_idle_time,
                         "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.keep-alive-interval",
                         wasi_sockets_tcp_tcp_socket_keep_alive_interval,
                         "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.set-keep-alive-interval",
                         wasi_sockets_tcp_tcp_socket_set_keep_alive_interval,
                         "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.keep-alive-count",
                         wasi_sockets_tcp_tcp_socket_keep_alive_count, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.set-keep-alive-count",
                         wasi_sockets_tcp_tcp_socket_set_keep_alive_count,
                         "(iii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.hop-limit",
                         wasi_sockets_tcp_tcp_socket_hop_limit, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.set-hop-limit",
                         wasi_sockets_tcp_tcp_socket_set_hop_limit, "(iii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.receive-buffer-size",
                         wasi_sockets_tcp_tcp_socket_receive_buffer_size,
                         "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.set-receive-buffer-size",
                         wasi_sockets_tcp_tcp_socket_set_receive_buffer_size,
                         "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.send-buffer-size",
                         wasi_sockets_tcp_tcp_socket_send_buffer_size, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.set-send-buffer-size",
                         wasi_sockets_tcp_tcp_socket_set_send_buffer_size,
                         "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.subscribe",
                         wasi_sockets_tcp_tcp_socket_subscribe, "(i)i"),
    REG_WASI_P2_FUNCTION("[method]tcp-socket.shutdown",
                         wasi_sockets_tcp_tcp_socket_shutdown, "(iii)"),
};

static NativeSymbol sockets_udp_create_socket_symbols[] = {
    REG_WASI_P2_FUNCTION("create-udp-socket",
                         wasi_sockets_udp_create_socket_create_udp_socket,
                         "(ii)"),
};

static NativeSymbol sockets_udp_symbols[] = {
    REG_WASI_P2_FUNCTION("[method]udp-socket.start-bind",
                         wasi_sockets_udp_udp_socket_start_bind,
                         "(iiiiiiiiiiiiiii)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.finish-bind",
                         wasi_sockets_udp_udp_socket_finish_bind, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.stream",
                         wasi_sockets_udp_udp_socket_stream,
                         "(iiiiiiiiiiiiiii)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.local-address",
                         wasi_sockets_udp_udp_socket_local_address, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.remote-address",
                         wasi_sockets_udp_udp_socket_remote_address, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.address-family",
                         wasi_sockets_udp_udp_socket_address_family, "(i)i"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.unicast-hop-limit",
                         wasi_sockets_udp_udp_socket_unicast_hop_limit, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.set-unicast-hop-limit",
                         wasi_sockets_udp_udp_socket_set_unicast_hop_limit,
                         "(iii)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.receive-buffer-size",
                         wasi_sockets_udp_udp_socket_receive_buffer_size,
                         "(ii)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.set-receive-buffer-size",
                         wasi_sockets_udp_udp_socket_set_receive_buffer_size,
                         "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.send-buffer-size",
                         wasi_sockets_udp_udp_socket_send_buffer_size, "(ii)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.set-send-buffer-size",
                         wasi_sockets_udp_udp_socket_set_send_buffer_size,
                         "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]udp-socket.subscribe",
                         wasi_sockets_udp_udp_socket_subscribe, "(i)i"),
    REG_WASI_P2_FUNCTION("[method]incoming-datagram-stream.receive",
                         wasi_sockets_udp_incoming_datagram_stream_receive,
                         "(iIi)"),
    REG_WASI_P2_FUNCTION("[method]incoming-datagram-stream.subscribe",
                         wasi_sockets_udp_incoming_datagram_stream_subscribe,
                         "(i)i"),
    REG_WASI_P2_FUNCTION("[method]outgoing-datagram-stream.check-send",
                         wasi_sockets_udp_outgoing_datagram_stream_check_send,
                         "(ii)"),
    REG_WASI_P2_FUNCTION("[method]outgoing-datagram-stream.send",
                         wasi_sockets_udp_outgoing_datagram_stream_send,
                         "(ii~i)"),
    REG_WASI_P2_FUNCTION("[method]outgoing-datagram-stream.subscribe",
                         wasi_sockets_udp_outgoing_datagram_stream_subscribe,
                         "(i)i"),
};

static wasi_p2_module_t wasi_p2_modules[] = {
    WASI_P2_MODULE(cli_environment, "cli/environment", "0.2.0"),
    WASI_P2_MODULE(cli_exit, "cli/exit", "0.2.0"),
    WASI_P2_MODULE(cli_stdin, "cli/stdin", "0.2.0"),
    WASI_P2_MODULE(cli_stdout, "cli/stdout", "0.2.0"),
    WASI_P2_MODULE(cli_stderr, "cli/stderr", "0.2.0"),
    WASI_P2_MODULE(cli_terminal_stdin, "cli/terminal-stdin", "0.2.0"),
    WASI_P2_MODULE(cli_terminal_stdout, "cli/terminal-stdout", "0.2.0"),
    WASI_P2_MODULE(cli_terminal_stderr, "cli/terminal-stderr", "0.2.0"),
    WASI_P2_MODULE(clocks_monotonic_clock, "clocks/monotonic-clock", "0.2.0"),
    WASI_P2_MODULE(clocks_wall_clock, "clocks/wall-clock", "0.2.0"),
    WASI_P2_MODULE(filesystem_preopens, "filesystem/preopens", "0.2.0"),
    WASI_P2_MODULE(filesystem_types, "filesystem/types", "0.2.0"),
    WASI_P2_MODULE(random_random, "random/random", "0.2.0"),
    WASI_P2_MODULE(random_insecure, "random/insecure", "0.2.0"),
    WASI_P2_MODULE(random_insecure_seed, "random/insecure-seed", "0.2.0"),
    WASI_P2_MODULE(io_error, "io/error", "0.2.0"),
    WASI_P2_MODULE(io_poll, "io/poll", "0.2.0"),
    WASI_P2_MODULE(io_streams, "io/streams", "0.2.0"),
    WASI_P2_MODULE(sockets_instance_network, "sockets/instance-network",
                   "0.2.0"),
    WASI_P2_MODULE(sockets_ip_name_lookup, "sockets/ip-name-lookup", "0.2.0"),
    WASI_P2_MODULE(sockets_tcp_create_socket, "sockets/tcp-create-socket",
                   "0.2.0"),
    WASI_P2_MODULE(sockets_tcp, "sockets/tcp", "0.2.0"),
    WASI_P2_MODULE(sockets_udp_create_socket, "sockets/udp-create-socket",
                   "0.2.0"),
    WASI_P2_MODULE(sockets_udp, "sockets/udp", "0.2.0"),
};

static bool
convert_version(int *value, const char version_string[])
{
    errno = 0;
    char *endptr = NULL;
    *value = (int)strtol(version_string, &endptr, 10);
    if (version_string == endptr || errno == ERANGE || *endptr != '\0')
        return false;

    return true;
}

bool
wasm_check_wasi_p2_version(const char *required_interface)
{
    const char *at = strchr(required_interface, '@');
    if (!at)
        return true; // no version requirement, always ok

    const char *required_ver = at + 1;
    wasi_p2_module_t *modules = NULL;
    uint32_t count = get_libc_wasi_p2_export_apis(&modules);
    for (uint32_t i = 0; i < count; i++) {
        if (is_module_equal(modules[i].module_name, required_interface)) {
            int req_maj = 0, req_min = 0, req_pat = 0, run_maj = 0, run_min = 0,
                run_pat = 0;
            char req_maj_str[20] = { 0 }, req_min_str[20] = { 0 },
                 req_pat_str[20] = { 0 }, run_maj_str[20] = { 0 },
                 run_min_str[20] = { 0 }, run_pat_str[20] = { 0 };

            if (sscanf(required_ver, "%19[^.].%19[^.].%19[^.]", req_maj_str,
                       req_min_str, req_pat_str)
                    != 3
                || sscanf(modules[i].version, "%19[^.].%19[^.].%19[^.]",
                          run_maj_str, run_min_str, run_pat_str)
                       != 3) {
                return false;
            }

            if (!convert_version(&req_maj, req_maj_str)
                || !convert_version(&req_min, req_min_str)
                || !convert_version(&req_pat, req_pat_str)
                || !convert_version(&run_maj, run_maj_str)
                || !convert_version(&run_min, run_min_str)
                || !convert_version(&run_pat, run_pat_str)) {
                return false;
            }

            // Hard fail: major or minor mismatch = incompatible API
            if (req_maj != run_maj || req_min != run_min || req_pat < run_pat) {
                LOG_ERROR("Incompatible WASI version for %s: "
                          "required %d.%d.%d, runtime implements %d.%d.%d",
                          required_interface, req_maj, req_min, req_pat,
                          run_maj, run_min, run_pat);
                return false;
            }
            return true;
        }
    }
    return true;
}

/**
 * @brief Get the exported APIs for the WASI P2 modules.
 * @details This function returns a pointer to the array of WASI P2 modules
 *          and the number of modules in the array. It also sorts the native
 *          symbols in each module by name on the first call.
 * @param p_libc_wasi_p2_apis A pointer to a pointer to a wasi_p2_module_t
 *                            struct. This will be updated to point to the
 *                            array of WASI P2 modules.
 * @return The number of WASI P2 modules.
 */
uint32_t
get_libc_wasi_p2_export_apis(wasi_p2_module_t **p_libc_wasi_p2_apis)
{
    static bool wasi_p2_native_symbols_sorted = false;
    if (!wasi_p2_native_symbols_sorted) {
        for (uint32_t i = 0;
             i < sizeof(wasi_p2_modules) / sizeof(wasi_p2_module_t); i++) {
            qsort((void *)wasi_p2_modules[i].symbols,
                  wasi_p2_modules[i].symbol_count, sizeof(NativeSymbol),
                  native_symbol_cmp);
        }
        wasi_p2_native_symbols_sorted = true;
    }

    *p_libc_wasi_p2_apis = wasi_p2_modules;
    return sizeof(wasi_p2_modules) / sizeof(wasi_p2_module_t);
}

/**
 * @brief Register all WASI P2 modules.
 * @return True if successful, false otherwise.
 */
bool
wasm_native_register_wasi_p2_modules()
{
    wasi_p2_module_t *modules = NULL;
    uint32_t wasi_p2_module_count = get_libc_wasi_p2_export_apis(&modules);

    for (uint32_t i = 0; i < wasi_p2_module_count; i++) {
        if (!wasm_native_register_natives(modules[i].module_name,
                                          (NativeSymbol *)modules[i].symbols,
                                          modules[i].symbol_count)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Unregister all WASI P2 modules.
 */
void
wasm_native_unregister_wasi_p2_modules()
{
    wasi_p2_module_t *modules = NULL;
    uint32_t wasi_p2_module_count = get_libc_wasi_p2_export_apis(&modules);

    for (uint32_t i = 0; i < wasi_p2_module_count; i++) {
        wasm_native_unregister_natives(modules[i].module_name,
                                       (NativeSymbol *)modules[i].symbols);
    }
}

/**
 * @brief Register a single WASI P2 module.
 * @param module_name The name of the module to register.
 * @return True if successful, false otherwise.
 */
bool
wasm_native_register_wasi_p2_module(const char *module_name)
{
    wasi_p2_module_t *modules = NULL;
    uint32_t wasi_p2_module_count = get_libc_wasi_p2_export_apis(&modules);

    for (uint32_t i = 0; i < wasi_p2_module_count; i++) {
        if (is_module_equal(modules[i].module_name, module_name)) {
            return wasm_native_register_natives(
                modules[i].module_name, (NativeSymbol *)modules[i].symbols,
                modules[i].symbol_count);
        }
    }

    return false;
}

/**
 * @brief Unregister a single WASI P2 module.
 * @param module_name The name of the module to unregister.
 */
void
wasm_native_unregister_wasi_p2_module(const char *module_name)
{
    wasi_p2_module_t *modules = NULL;
    uint32_t wasi_p2_module_count = get_libc_wasi_p2_export_apis(&modules);

    for (uint32_t i = 0; i < wasi_p2_module_count; i++) {
        if (is_module_equal(modules[i].module_name, module_name)) {
            wasm_native_unregister_natives(modules[i].module_name,
                                           (NativeSymbol *)modules[i].symbols);
            return;
        }
    }
}

/**
 * @brief Find a function in a WASI P2 module.
 * @param module_name The name of the module to search.
 * @param func_name The name of the function to find.
 * @return A pointer to the native symbol if found, otherwise NULL.
 */
static const NativeSymbol *
find_wasi_p2_module_func(const char *module_name, const char *func_name)
{
    wasi_p2_module_t *modules = NULL;
    uint32_t wasi_p2_module_count = get_libc_wasi_p2_export_apis(&modules);

    for (uint32_t i = 0; i < wasi_p2_module_count; i++) {
        if (is_module_equal(modules[i].module_name, module_name)) {
            for (uint32_t j = 0; j < modules[i].symbol_count; j++) {
                if (strcmp(modules[i].symbols[j].symbol, func_name) == 0) {
                    return &modules[i].symbols[j];
                }
            }
        }
    }
    return NULL;
}

/**
 * @brief Register a single function in a WASI P2 module.
 * @param module_name The name of the module.
 * @param func_name The name of the function to register.
 * @return True if successful, false otherwise.
 */
bool
wasm_native_register_wasi_p2_module_func(const char *module_name,
                                         const char *func_name)
{
    const NativeSymbol *symbol =
        find_wasi_p2_module_func(module_name, func_name);
    if (symbol) {
        return wasm_native_register_natives(module_name, (NativeSymbol *)symbol,
                                            1);
    }
    return false;
}

/**
 * @brief Unregister a single function in a WASI P2 module.
 * @param module_name The name of the module.
 * @param func_name The name of the function to unregister.
 */
void
wasm_native_unregister_wasi_p2_module_func(const char *module_name,
                                           const char *func_name)
{
    const NativeSymbol *symbol =
        find_wasi_p2_module_func(module_name, func_name);
    if (symbol) {
        wasm_native_unregister_natives(module_name, (NativeSymbol *)symbol);
    }
}

#ifdef __cplusplus
}
#endif
