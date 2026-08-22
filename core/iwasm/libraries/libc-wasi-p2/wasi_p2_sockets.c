/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* accept4/pipe2/SOCK_NONBLOCK/SOCK_CLOEXEC/MSG_NOSIGNAL below are GNU
   extensions; request them before any system header (see wasi_p2_io.c). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "wasi_p2_sockets.h"
#include "wasi_p2_common.h"

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <bh_common.h>
#include <sys/types.h>
#include "bh_hashmap.h"
#include "wasm_export.h"

/**
 * @brief A singleton resource representing the host's network capability.
 * @details The `wasi:sockets/network` interface represents access to the
 * network. This struct acts as a handle to that capability. The `in_use` flag
 *          can ensure it's only held by one instance at a time.
 */
static struct wasi_network_resource {
    bool in_use;
} network_resource;

/* TCP/UDP sockets store a wasi_socket_context_t whose fd was allocated by
   wasi_sockets_create_tcp/udp_socket (socket()) or accept(); closing it
   releases the kernel socket. */
void
tcp_socket_dtor(void *data)
{
    close(((wasi_socket_context_t *)data)->fd);
}

void
udp_socket_dtor(void *data)
{
    close(((wasi_socket_context_t *)data)->fd);
}

/* TCP input/output streams from tcp.accept carry a dup()'d socket fd
   that duplicate is independently owned and must be closed on drop. */
void
tcp_owned_stream_dtor(void *data)
{
    close(((StreamResourceType *)data)->fd);
}

/* UDP datagram streams from udp.stream carry fcntl(F_DUPFD_CLOEXEC) fds
   each is owned and closed on drop. */
void
udp_datagram_stream_dtor(void *data)
{
    close((int)*(uint32_t *)data);
}

// --- Asynchronous DNS Resolution Infrastructure ---

/**
 * @brief A resource representing an in-progress DNS name resolution stream.
 * @details This struct manages the state of an asynchronous `getaddrinfo` call,
 *          which is executed in a separate thread to avoid blocking.
 */
typedef struct resolve_stream {
    // The head of the linked list of addresses returned by getaddrinfo.
    struct addrinfo *addr_head;
    // A pointer to the current address in the list for iteration.
    struct addrinfo *addr_current;
    // A pipe used to signal when the DNS resolution is complete.
    int pipe_fd;
    // The handle for the background thread performing the resolution.
    pthread_t thread;
    // A flag indicating if the background thread has finished.
    bool resolution_done;
    // The error code from getaddrinfo, if any.
    int resolution_error;
    // The errno value if resolution_error is EAI_SYSTEM
    int resolution_errno;
} resolve_stream_t;

/**
 * @brief A hash map to store and manage active `resolve_stream` resources.
 *        The key is a unique ID, and the value is a pointer to the
 * resolve_stream_t.
 */
static HashMap *resolve_streams_map = NULL;

/**
 * @brief A mutex to ensure thread-safe access to the `resolve_streams_map`.
 */
static pthread_mutex_t resolve_streams_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief A simple counter to generate unique IDs for resolve-stream resources.
 */
static long resolve_stream_id_counter = 0;

/**
 * @brief The hash function for the `resolve_streams_map`.
 * @param key The key to hash.
 * @return The hash value.
 */
static uint32
resolve_stream_hash(const void *key)
{
    return (uint32)(uintptr_t)key;
}

/**
 * @brief The key equality function for the `resolve_streams_map`.
 * @param key1 The first key.
 * @param key2 The second key.
 * @return `true` if the keys are equal, `false` otherwise.
 */
static bool
resolve_stream_equal(void *key1, void *key2)
{
    return (uintptr_t)key1 == (uintptr_t)key2;
}

/**
 * @brief The destructor function for `resolve_stream_t` objects stored in the
 * map.
 * @details This function is called automatically by the hash map when an entry
 *          is removed, ensuring that all associated resources are freed.
 * @param stream The stream to destroy.
 */
static void
destroy_resolve_stream(void *stream)
{
    resolve_stream_t *s = (resolve_stream_t *)stream;
    if (s) {
        if (s->addr_head) {
            // Free the linked list of addresses.
            freeaddrinfo(s->addr_head);
        }
        if (s->pipe_fd != -1) {
            // Close the signaling pipe.
            close(s->pipe_fd);
        }
        wasm_runtime_free(s);
    }
}

/**
 * @brief Lazily initializes the global hash map for resolve-address streams.
 */
static void
init_resolve_streams_map()
{
    if (!resolve_streams_map) {
        resolve_streams_map = bh_hash_map_create(
            32, false, (HashFunc)resolve_stream_hash,
            (KeyEqualFunc)resolve_stream_equal, NULL, destroy_resolve_stream);
    }
}

/**
 * @brief Deinitialize the global hash map for resolve-address streams.
 */
void
destroy_resolve_streams_map()
{
    resolve_streams_map = NULL;
}

void
wasi_sockets_drop_resolve_stream(uint32_t stream_id)
{
    if (!resolve_streams_map)
        return;
    void *old_key = NULL, *old_val = NULL;
    pthread_mutex_lock(&resolve_streams_lock);
    bool removed = bh_hash_map_remove(
        resolve_streams_map, (void *)(uintptr_t)stream_id, &old_key, &old_val);
    pthread_mutex_unlock(&resolve_streams_lock);
    if (removed && old_val)
        destroy_resolve_stream(old_val);
}

void
resolve_stream_dtor(void *data)
{
    wasi_sockets_drop_resolve_stream(*(uint32_t *)data);
}

/**
 * @brief A struct to pass arguments to the DNS resolution worker thread.
 */
struct resolve_thread_args {
    long stream_id;
    int pipe_write_fd;
    char *name;
};

/**
 * @brief The main function for the DNS resolution worker thread.
 * @details This function performs a blocking `getaddrinfo` call and then
 * signals completion by writing to a pipe. This makes the DNS lookup appear
 *          asynchronous to the main application.
 * @param arg A pointer to a `resolve_thread_args` struct.
 */
static void *
resolve_thread(void *arg)
{
    struct resolve_thread_args *args = arg;
    struct addrinfo *result = NULL;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;

    // Perform the potentially long-running, blocking DNS lookup.
    int ret = getaddrinfo(args->name, NULL, &hints, &result);
    int err_ = (ret == EAI_SYSTEM) ? errno : 0;

    // Lock the mutex to safely update the shared resolve_streams_map.
    pthread_mutex_lock(&resolve_streams_lock);

    if (resolve_streams_map) {
        resolve_stream_t *stream =
            bh_hash_map_find(resolve_streams_map, (void *)args->stream_id);

        if (stream) {
            // If the stream still exists, update it with the result or error.
            stream->resolution_error = ret;
            stream->resolution_errno = err_;
            if (ret == 0) {
                stream->addr_head = result;
                stream->addr_current = result;
                result = NULL; /* ownership transferred */
            }
            stream->resolution_done = true;

            // Signal completion by writing a single byte to the pipe. This will
            // make the read end of the pipe pollable.
            char c = 1;
            ssize_t s = write(args->pipe_write_fd, &c, 1);
            (void)s;

            // Close the write end of the pipe to signal that we're done.
            close(args->pipe_write_fd);
            args->pipe_write_fd = -1;
        }
        else if (result) {
            freeaddrinfo(result);
            result = NULL;
        }
    }

    pthread_mutex_unlock(&resolve_streams_lock);

    if (result) {
        freeaddrinfo(result);
    }

    // Clean up resources used by this thread.
    if (args->pipe_write_fd != -1) {
        close(args->pipe_write_fd);
    }
    wasm_runtime_free(args->name);
    wasm_runtime_free(args);
    return NULL;
}

/**
 * @brief Cleans up all global resources used by the wasi-sockets
 * implementation.
 * @details This is a non-WASI, host-specific function designed to be called
 *          by the runtime during shutdown. It destroys the hash map for DNS
 *          resolution streams to prevent resource leaks.
 */
void
wasi_p2_sockets_cleanup(void)
{
    // Destroy the hash map, which will also call the destructor for each
    // remaining resolve_stream_t.
    if (resolve_streams_map) {
        bh_hash_map_destroy(resolve_streams_map);
        resolve_streams_map = NULL;
    }
}

/**
 * @brief Checks if a network handle is valid.
 * @details In this implementation, the network is treated as a singleton
 *          resource. This function verifies that the provided handle
 * corresponds to the singleton and that it is currently marked as "in use".
 * @param network The network handle to validate.
 * @return `true` if the network handle is valid, `false` otherwise.
 */
static bool
is_valid_network(wasi_network_t network)
{
    // The network is a singleton resource, represented by the handle 0.
    // TODO: check if guest is allowed to access the network (sandboxed
    // environment mechansim)
    return network == 0 && network_resource.in_use;
}

/**
 * @brief Converts a WASI IP socket address to a native POSIX sockaddr struct.
 * @details This is a crucial helper for translating the abstract address format
 *          defined in the `wasi:sockets/network` WIT into the concrete
 * `sockaddr_storage` struct required by POSIX system calls like `connect`,
 * `bind`, and `sendto`.
 * @param wasi_addr A pointer to the source WASI IP socket address.
 * @param[out] native_addr A pointer to the destination `sockaddr_storage`
 * struct.
 * @param[out] native_addr_len A pointer to store the size of the resulting
 * native struct.
 * @return `0` on success, or an error code on failure.
 */
static int
convert_socket_addr_to_native(const wasi_ip_socket_address_t *wasi_addr,
                              struct sockaddr_storage *native_addr,
                              socklen_t *native_addr_len)
{
    if (!wasi_addr || !native_addr || !native_addr_len) {
        return WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT;
    }

    if (wasi_addr->tag == WASI_IP_SOCKET_ADDRESS_TAG_IPV4) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)native_addr;
        *native_addr_len = sizeof(struct sockaddr_in);
        addr_in->sin_family = AF_INET;
        // Convert port from host byte order to network byte order.
        addr_in->sin_port = htons(wasi_addr->val.ipv4.port);
        memcpy(&addr_in->sin_addr, &wasi_addr->val.ipv4.address,
               sizeof(wasi_addr->val.ipv4.address));
    }
    else if (wasi_addr->tag == WASI_IP_SOCKET_ADDRESS_TAG_IPV6) {
        struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)native_addr;
        *native_addr_len = sizeof(struct sockaddr_in6);
        addr_in6->sin6_family = AF_INET6;
        // Convert port from host byte order to network byte order.
        addr_in6->sin6_port = htons(wasi_addr->val.ipv6.port);
        addr_in6->sin6_flowinfo = wasi_addr->val.ipv6.flow_info;
        addr_in6->sin6_scope_id = wasi_addr->val.ipv6.scope_id;
        memcpy(&addr_in6->sin6_addr, &wasi_addr->val.ipv6.address,
               sizeof(wasi_addr->val.ipv6.address));
    }
    else {
        return WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED;
    }
    return WASI_ERROR_CODE_SUCCESS;
}

/**
 * @brief Converts a native POSIX sockaddr struct to a WASI IP socket address.
 * @details This is the reverse of `convert_socket_addr_to_native`. It's used
 *          after system calls like `accept`, `getsockname`, or `recvfrom` to
 *          translate the native address format back into the abstract WIT
 * format.
 * @param[out] wasi_addr A pointer to the destination WASI IP socket address.
 * @param native_addr A pointer to the source `sockaddr_storage` struct.
 * @return `0` on success, or an error code on failure.
 */
static int
convert_socket_addr_from_native(wasi_ip_socket_address_t *wasi_addr,
                                const struct sockaddr_storage *native_addr)
{
    if (!wasi_addr || !native_addr) {
        return WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT;
    }

    if (native_addr->ss_family == AF_INET) {
        const struct sockaddr_in *addr_in =
            (const struct sockaddr_in *)native_addr;
        wasi_addr->tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
        // Convert port from network byte order to host byte order.
        wasi_addr->val.ipv4.port = ntohs(addr_in->sin_port);
        memcpy(&wasi_addr->val.ipv4.address, &addr_in->sin_addr,
               sizeof(wasi_addr->val.ipv4.address));
    }
    else if (native_addr->ss_family == AF_INET6) {
        const struct sockaddr_in6 *addr_in6 =
            (const struct sockaddr_in6 *)native_addr;
        wasi_addr->tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV6;
        // Convert port from network byte order to host byte order.
        wasi_addr->val.ipv6.port = ntohs(addr_in6->sin6_port);
        wasi_addr->val.ipv6.flow_info = addr_in6->sin6_flowinfo;
        wasi_addr->val.ipv6.scope_id = addr_in6->sin6_scope_id;
        memcpy(&wasi_addr->val.ipv6.address, &addr_in6->sin6_addr,
               sizeof(wasi_addr->val.ipv6.address));
    }
    else {
        return WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED;
    }
    return WASI_ERROR_CODE_SUCCESS;
}

// wasi:sockets/instance-network

/**
 * @brief Get the default network capability.
 * @details Implements the `instance-network` function from the
 *          `wasi:sockets/instance-network` interface. This function returns a
 *          handle to the singleton network resource, marking it as "in use".
 * @return A handle to the network resource (always 0 in this implementation).
 */
wasi_network_t
wasi_sockets_instance_network(void)
{
    // This implementation treats the network as a singleton resource.
    // The first call will mark it as in-use.
    if (!network_resource.in_use) {
        network_resource.in_use = true;
    }
    return 0;
}

// wasi:sockets/ip-name-lookup

/**
 * @brief Resolve a hostname to a stream of IP addresses.
 * @details Implements the `resolve-addresses` function from the
 *          `wasi:sockets/ip-name-lookup` interface. This operation is performed
 *          asynchronously in a background thread to avoid blocking.
 * @param network The network handle.
 * @param name The hostname or IP address string to resolve.
 * @param[out] ret A pointer to store the handle for the new
 * `resolve-address-stream`.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_resolve_addresses(wasi_network_t network, const char *name,
                               wasi_resolve_address_stream_t *ret, int *err)
{
    if (!name || !ret || !err) {
        if (err)
            *err = EINVAL;
        if (ret)
            *ret = -1;
        return;
    }

    *ret = -1;
    resolve_stream_t *stream = NULL;
    struct resolve_thread_args *args = NULL;
    long id = -1;
    bool lock_held = false;
    int pipefd[2] = { -1, -1 };

    if (!is_valid_network(network)) {
        *err = EINVAL;
        goto fail;
    }

    pthread_mutex_lock(&resolve_streams_lock);
    lock_held = true;

    init_resolve_streams_map();
    if (!resolve_streams_map) {
        *err = EINVAL;
        goto fail;
    }

    // Allocate resources for the asynchronous operation.
    stream = wasm_runtime_malloc(sizeof(resolve_stream_t));
    if (!stream) {
        *err = EINVAL;
        goto fail;
    }
    memset(stream, 0, sizeof(resolve_stream_t));
    stream->pipe_fd = -1;

    args = wasm_runtime_malloc(sizeof(*args));
    if (!args) {
        *err = EINVAL;
        goto fail;
    }
    args->name = NULL;
    args->pipe_write_fd = -1;

    // Create a pipe to signal completion from the worker thread.
    // The read-end of this pipe will become the pollable for the stream's
    // subscribe method.
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        *err = errno;
        goto fail;
    }
    stream->pipe_fd = pipefd[0];
    args->pipe_write_fd = pipefd[1];
    pipefd[0] = pipefd[1] = -1;

    args->name = wa_strdup(name);
    if (!args->name) {
        *err = EINVAL;
        goto fail;
    }

    // Generate a unique ID for this resolution stream and add it to the global
    // map.
    id = ++resolve_stream_id_counter;
    args->stream_id = id;
    stream->resolution_done = false;

    if (!bh_hash_map_insert(resolve_streams_map, (void *)id, stream)) {
        *err = EINVAL;
        goto fail;
    }

    // Create and detach a worker thread to perform the blocking getaddrinfo
    // call.
    pthread_t th;
    if (pthread_create(&th, NULL, resolve_thread, args) != 0) {
        bh_hash_map_remove(resolve_streams_map, (void *)id, NULL, NULL);
        stream = NULL; /* freed by remove */
        *err = EIO;
        goto fail;
    }
    args = NULL; /* ownership transferred to thread */

    if (pthread_detach(th) != 0) {
        fprintf(stderr, "wasi-sockets: warning: pthread_detach failed\n");
    }
    *ret = id;
    *err = 0;
    pthread_mutex_unlock(&resolve_streams_lock);
    return;

fail:
    // Robust cleanup logic in case of any failure during setup.
    if (lock_held)
        pthread_mutex_unlock(&resolve_streams_lock);

    if (pipefd[0] != -1)
        close(pipefd[0]);
    if (pipefd[1] != -1)
        close(pipefd[1]);

    if (args) {
        if (args->pipe_write_fd != -1)
            close(args->pipe_write_fd);
        if (args->name)
            wasm_runtime_free(args->name);
        wasm_runtime_free(args);
    }
    if (stream) {
        if (stream->pipe_fd != -1)
            close(stream->pipe_fd);
        wasm_runtime_free(stream);
    }

    *ret = -1;
}
/**
 * @brief Resolve the next IP address from a resolution stream.
 * @details Implements the `resolve-next-address` method on the
 *          `resolve-address-stream` resource from
 * `wasi:sockets/ip-name-lookup`. This function is non-blocking and iterates
 * through the list of addresses found by the background DNS lookup.
 * @param stream_id The handle of the resolve-address-stream.
 * @param[out] ret A pointer to store the resulting IP address if one is
 * available.
 * @param[out] is_some Set to `true` if an address was returned, or `false` if
 * the stream is exhausted or not yet ready.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_resolve_next_address(wasi_resolve_address_stream_t stream_id,
                                  wasi_ip_address_t *ret, bool *is_some,
                                  int *err)
{
    if (!ret || !is_some || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    pthread_mutex_lock(&resolve_streams_lock);
    resolve_stream_t *stream =
        bh_hash_map_find(resolve_streams_map, (void *)(intptr_t)stream_id);
    if (!stream) {
        pthread_mutex_unlock(&resolve_streams_lock);
        *err = EINVAL;
        *is_some = false;
        return;
    }

    // If the background thread hasn't finished, the result is not ready.
    // TODO: is that ok?
    if (!stream->resolution_done) {
        pthread_mutex_unlock(&resolve_streams_lock);
        *err = 0;
        *is_some = false;
        return;
    }

    // If the resolution failed or returned no addresses, report the error.
    if (stream->resolution_error != 0 || !stream->addr_head) {
        int gai_err = stream->resolution_error;
        if (gai_err == EAI_SYSTEM) {
            *err = stream->resolution_errno;
        }
        else if (gai_err == EAI_AGAIN) {
            *err = ETIMEDOUT;
        }
        else if (gai_err == EAI_FAIL) {
            *err = ETIMEDOUT;
        }
        else { // EAI_NONAME, EAI_NODATA, or resolution_error is 0 but no addr
            *err = EHOSTUNREACH;
        }
        *is_some = false;
        pthread_mutex_unlock(&resolve_streams_lock);
        return;
    }

    // If we've reached the end of the address list, signal completion.
    if (stream->addr_current == NULL) {
        *is_some = false;
        *err = 0;
        pthread_mutex_unlock(&resolve_streams_lock);
        return;
    }

    // Iterate through the addrinfo linked list to find the next valid address.
    while (stream->addr_current) {
        if (stream->addr_current->ai_family == AF_INET) {
            const struct sockaddr_in *addr_in =
                (struct sockaddr_in *)stream->addr_current->ai_addr;
            ret->kind = (uint8_t)WASI_IP_ADDRESS_TAG_IPV4;
            memcpy(&ret->addr.ip4, &addr_in->sin_addr, sizeof(ret->addr.ip4));
            *is_some = true;
            *err = 0;
            stream->addr_current = stream->addr_current->ai_next;
            pthread_mutex_unlock(&resolve_streams_lock);
            return;
        }
        else if (stream->addr_current->ai_family == AF_INET6) {
            const struct sockaddr_in6 *addr_in6 =
                (struct sockaddr_in6 *)stream->addr_current->ai_addr;
            ret->kind = (uint8_t)WASI_IP_ADDRESS_TAG_IPV6;
            memcpy(&ret->addr.ip6, &addr_in6->sin6_addr, sizeof(ret->addr.ip6));
            *is_some = true;
            *err = 0;
            stream->addr_current = stream->addr_current->ai_next;
            pthread_mutex_unlock(&resolve_streams_lock);
            return;
        }
        // Unsupported family, try the next address in the list.
        stream->addr_current = stream->addr_current->ai_next;
    }

    // No more supported addresses were found.
    *is_some = false;
    *err = 0;
    pthread_mutex_unlock(&resolve_streams_lock);
}

/**
 * @brief Create a pollable that resolves when the DNS lookup is complete.
 * @details Implements the `subscribe` method on the `resolve-address-stream`
 *          resource. This allows the stream to be used with `wasi:io/poll`.
 *          The pollable gets an independent dup of the stream's pipe read-end
 *          so it can be closed independently of the stream's lifetime.
 * @param stream_id The handle of the resolve-address-stream.
 * @return A pollable context wrapping an owned pipe read-end fd, signalled
 *         when DNS resolution finishes. On error, `fd` is -1.
 */
wasi_pollable_context_t
wasi_sockets_resolve_address_stream_subscribe(
    wasi_resolve_address_stream_t stream_id)
{
    wasi_pollable_context_t pollable = { .fd = -1,
                                         .own_fd = false,
                                         .type = WASI_POLLABLE_IN };

    pthread_mutex_lock(&resolve_streams_lock);
    const resolve_stream_t *stream =
        bh_hash_map_find(resolve_streams_map, (void *)(intptr_t)stream_id);
    if (!stream) {
        pthread_mutex_unlock(&resolve_streams_lock);
        return pollable;
    }
    // dup so the pollable owns its own fd and can outlive the stream.
    int pipe_fd = dup(stream->pipe_fd);
    pthread_mutex_unlock(&resolve_streams_lock);

    if (pipe_fd < 0) {
        return pollable;
    }
    SET_INPUT_POLLABLE(&pollable, pipe_fd, true);
    return pollable;
}

/**
 * @brief Static helper to create a native POSIX socket from WASI parameters.
 * @details This function translates a WASI address family and socket type into
 *          the arguments needed for the `socket` system call.
 * @note Crucially, it creates all sockets with the `SOCK_NONBLOCK` flag, which
 *       is fundamental to the non-blocking, readiness-based design of
 * wasi-sockets.
 * @param family The WASI IP address family (IPv4 or IPv6).
 * @param type The POSIX socket type (e.g., `SOCK_STREAM` for TCP, `SOCK_DGRAM`
 * for UDP).
 * @param[out] ret_sock A pointer to store the newly created socket file
 * descriptor.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
static void
create_socket(wasi_ip_address_family_t family, int type, int *ret_sock,
              int *err)
{
    if (!ret_sock || !err) {
        if (err)
            *err = EINVAL;
        return;
    }

    *ret_sock = -1;

    int af = 0;
    // Translate the WASI address family enum to a POSIX AF_ constant.
    switch (family) {
        case WASI_IP_ADDRESS_FAMILY_IPV4:
            af = AF_INET;
            break;
        case WASI_IP_ADDRESS_FAMILY_IPV6:
            af = AF_INET6;
            break;
        default:
            *err = WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED;
            return;
    }
    // Create a non-blocking, close-on-exec socket of the specified type.
    int sock = socket(af, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sock < 0) {
        *err = errno;
    }
    else {
        *err = 0;
        *ret_sock = sock;
    }
}

// wasi:sockets/tcp-create-socket

/**
 * @brief Create a new TCP socket.
 * @details Implements the `create-tcp-socket` function from the
 *          `wasi:sockets/tcp-create-socket` interface.
 * @param family The IP address family (IPv4 or IPv6) for the socket.
 * @param[out] ret A pointer to store the newly created TCP socket handle.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_create_tcp_socket(wasi_ip_address_family_t family,
                               wasi_tcp_socket_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    // Delegate to the generic socket creation helper, specifying SOCK_STREAM
    // for TCP.
    create_socket(family, SOCK_STREAM, ret, err);
}

// wasi:sockets/tcp

/**
 * @brief Bind a TCP socket to a local address.
 * @details Implements the `start-bind` method on the `tcp-socket` resource from
 *          the `wasi:sockets/tcp` interface. In this implementation, the bind
 *          operation is performed synchronously.
 * @param socket The TCP socket handle.
 * @param network The network handle.
 * @param local_address The local IP address and port to bind to.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_start_bind(wasi_tcp_socket_t socket, wasi_network_t network,
                            const wasi_ip_socket_address_t *local_address)
{
    if (!local_address) {
        return EINVAL;
    }
    if (!is_valid_network(network)) {
        return EINVAL;
    }
    struct sockaddr_storage native_addr;
    socklen_t native_addr_len;

    // Convert the abstract WASI address into a native POSIX sockaddr struct.
    int err = convert_socket_addr_to_native(local_address, &native_addr,
                                            &native_addr_len);
    if (err != 0) {
        return err;
    }

    // Set SO_REUSEADDR to avoid EADDRINUSE errors on restart
    int reuse = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))
        < 0) {
        return errno;
    }

    // Perform the POSIX bind system call.
    if (bind(socket, (struct sockaddr *)&native_addr, native_addr_len) < 0) {
        return errno;
    }

    return WASI_ERROR_CODE_SUCCESS;
}

/**
 * @brief Complete a bind operation.
 * @details Implements the `finish-bind` method on the `tcp-socket` resource.
 * @note Since `start-bind` is implemented synchronously in this host, this
 *       function is a no-op and always returns success.
 * @param socket The TCP socket handle.
 * @return Always returns `0`.
 */
int
wasi_sockets_tcp_finish_bind(wasi_tcp_socket_t socket)
{
    // Nothing to do here, as the bind operation is completed synchronously
    // in the `start-bind` function.
    return WASI_ERROR_CODE_SUCCESS;
}

/**
 * @brief Start a non-blocking connection to a remote TCP socket.
 * @details Implements the `start-connect` method on the `tcp-socket` resource
 *          from the `wasi:sockets/tcp` interface.
 * @note Because the socket is non-blocking, this function will return
 *       immediately. A successful return indicates that the connection process
 *       has begun. The connection must be completed by calling
 * `finish-connect`.
 * @param socket The TCP socket handle.
 * @param network The network handle.
 * @param remote_address The remote IP address and port to connect to.
 * @return A WASI error code. `SUCCESS` is returned even if the connection is
 *         still in progress (indicated by `EINPROGRESS` from the OS).
 */
int
wasi_sockets_tcp_start_connect(wasi_tcp_socket_t socket, wasi_network_t network,
                               const wasi_ip_socket_address_t *remote_address)
{
    if (!remote_address) {
        return EINVAL;
    }
    if (!is_valid_network(network)) {
        return EINVAL;
    }
    struct sockaddr_storage native_addr;
    socklen_t native_addr_len;

    // Convert the abstract WASI address into a native POSIX sockaddr struct.
    int err = convert_socket_addr_to_native(remote_address, &native_addr,
                                            &native_addr_len);
    if (err != 0) {
        return err;
    }

    // Initiate the connection.
    int ret = connect(socket, (struct sockaddr *)&native_addr, native_addr_len);
    if (ret < 0) {
        // For a non-blocking socket, `EINPROGRESS` is not an error. It means
        // the connection is being established in the background.
        return errno;
    }

    // If connect returns 0, the connection was established immediately.
    return WASI_ERROR_CODE_SUCCESS;
}

/**
 * @brief Complete a non-blocking connection attempt.
 * @details Implements the `finish-connect` method on the `tcp-socket` resource.
 *          This function checks the status of a connection initiated by
 *          `start-connect`.
 * @param socket The TCP socket handle.
 * @param[out] input_stream A pointer to store the input stream handle on
 * success.
 * @param[out] output_stream A pointer to store the output stream handle on
 * success.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_finish_connect(wasi_tcp_socket_t socket,
                                wasi_input_stream_t *input_stream,
                                wasi_output_stream_t *output_stream, int *err)
{
    if (!input_stream || !output_stream || !err) {
        if (err)
            *err = EINVAL;
        return;
    }

    *input_stream = -1;
    *output_stream = -1;
    *err = 0;

    struct pollfd pfd;
    pfd.fd = socket;
    pfd.events = POLLOUT; // A successful connection makes a socket writable.
    // A timeout of 0 makes poll non-blocking.
    int ret = poll(&pfd, 1, 0);
    if (ret < 0) {
        *err = errno;
        return;
    }

    // Check if poll reported an error on the socket.
    if (pfd.revents & (POLLERR | POLLHUP)) {
        int error = 0;
        socklen_t len = sizeof(error);
        // Retrieve the specific error from the socket options.
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            *err = errno;
            return;
        }
        *err = error;
        return;
    }

    // If the socket is writable, the connection has completed.
    if (pfd.revents & POLLOUT) {
        int error = 0;
        socklen_t len = sizeof(error);
        // We must still check SO_ERROR to confirm the connection was
        // successful.
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            *err = errno;
            return;
        }

        if (error == 0) {
            *err = 0;
        }
        else {
            *err = error;
        }
    }
    else {
        // If the socket is not yet writable, the connection is still in
        // progress.
        *err = EWOULDBLOCK;
    }

    // On success, a TCP socket can be used for both reading and writing.
    if (*err == 0) {
        *input_stream = socket;
        *output_stream = socket;
    }
}

/**
 * @brief Start listening for incoming connections on a TCP socket.
 * @details Implements the `start-listen` method on the `tcp-socket` resource
 *          from the `wasi:sockets/tcp` interface. In this implementation, the
 *          operation is performed synchronously using the POSIX `listen` call.
 * @param socket The TCP socket handle to start listening on.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_start_listen(wasi_tcp_socket_t socket, uint64_t backlog)
{
    // Put the socket into a listening state.
    if (listen(socket, backlog) < 0) {
        return errno;
    }

    return WASI_ERROR_CODE_SUCCESS;
}

/**
 * @brief Complete a listen operation.
 * @details Implements the `finish-listen` method on the `tcp-socket` resource.
 * @note Since `start-listen` is implemented synchronously in this host, this
 *       function is a no-op and always returns success.
 * @param socket The TCP socket handle.
 * @return Always returns `0`.
 */
int
wasi_sockets_tcp_finish_listen(wasi_tcp_socket_t socket)
{
    // Nothing to do here, as the listen operation is completed synchronously
    // in the `start-listen` function.
    return WASI_ERROR_CODE_SUCCESS;
}

/**
 * @brief Accept a new incoming connection.
 * @details Implements the `accept` method on the `tcp-socket` resource. This
 *          function is non-blocking.
 * @param socket The listening TCP socket handle.
 * @param[out] ret A pointer to store the new connected TCP socket handle.
 * @param[out] input_stream A pointer to store the input stream for the new
 * connection.
 * @param[out] output_stream A pointer to store the output stream for the new
 * connection.
 * @param[out] err A pointer to store the resulting WASI error code. This will
 * be set to `WOULD_BLOCK` if no connection is currently pending.
 */
void
wasi_sockets_tcp_accept(wasi_tcp_socket_t socket, wasi_tcp_socket_t *ret,
                        wasi_input_stream_t *input_stream,
                        wasi_output_stream_t *output_stream, int *err)
{
    if (!ret || !input_stream || !output_stream || !err) {
        if (err)
            *err = EINVAL;
        return;
    }

    *ret = -1;
    *input_stream = -1;
    *output_stream = -1;

    if (!wasi_sockets_tcp_is_listening(socket)) {
        *err = ENOTCONN;
        return;
    }

    int new_socket;
// Use accept4 on Linux to atomically set the close-on-exec and
// non-blocking flags. This is more efficient than calling fcntl separately.
#if defined(__linux__)
    new_socket = accept4(socket, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
#else
    new_socket = accept(socket, NULL, NULL);
#endif

    if (new_socket < 0) {
        // If no connection is pending, the OS returns EAGAIN or EWOULDBLOCK.
        // This is not a fatal error; we map it to the WASI `would-block` code.
        *err = errno;
        return;
    }

// Ensure the new socket is non-blocking on non-Linux platforms.
#ifndef __linux__
    int flags;
    if ((flags = fcntl(new_socket, F_GETFL, 0)) < 0
        || fcntl(new_socket, F_SETFL, flags | O_NONBLOCK) < 0
        || fcntl(new_socket, F_SETFD, FD_CLOEXEC) < 0) {
        *err = errno;
        close(new_socket);
        return;
    }
#endif

    // On success, the new socket represents a bidirectional stream.
    *err = 0;
    *ret = new_socket;
    *input_stream = dup(new_socket);
    *output_stream = dup(new_socket);
}

/**
 * @brief Get the local address of a TCP socket.
 * @details Implements the `local-address` method on the `tcp-socket` resource
 *          from the `wasi:sockets/tcp` interface. This is similar to
 * `getsockname` in POSIX.
 * @param socket The TCP socket handle.
 * @param[out] ret A pointer to store the resulting local IP socket address.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_local_address(wasi_tcp_socket_t socket,
                               wasi_ip_socket_address_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    struct sockaddr_storage native_addr;
    socklen_t native_addr_len = sizeof(native_addr);

    // Use the POSIX getsockname call to retrieve the local address.
    if (getsockname(socket, (struct sockaddr *)&native_addr, &native_addr_len)
        < 0) {
        *err = errno;
        return;
    }

    // Convert the native POSIX address back to the abstract WASI format.
    *err = convert_socket_addr_from_native(ret, &native_addr);
}

/**
 * @brief Get the remote address of a TCP socket.
 * @details Implements the `remote-address` method on the `tcp-socket` resource
 *          from the `wasi:sockets/tcp` interface. This is similar to
 * `getpeername` in POSIX.
 * @param socket The TCP socket handle.
 * @param[out] ret A pointer to store the resulting remote IP socket address.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_remote_address(wasi_tcp_socket_t socket,
                                wasi_ip_socket_address_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    struct sockaddr_storage native_addr;
    socklen_t native_addr_len = sizeof(native_addr);

    // Use the POSIX getpeername call to retrieve the remote (peer) address.
    if (getpeername(socket, (struct sockaddr *)&native_addr, &native_addr_len)
        < 0) {
        *err = errno;
        return;
    }

    // Convert the native POSIX address back to the abstract WASI format.
    *err = convert_socket_addr_from_native(ret, &native_addr);
}

/**
 * @brief Check if a TCP socket is in the listening state.
 * @details Implements the `is-listening` method on the `tcp-socket` resource
 *          from the `wasi:sockets/tcp` interface. It uses the `SO_ACCEPTCONN`
 *          socket option, which is the standard POSIX way to determine this.
 * @param socket The TCP socket handle.
 * @return `true` if the socket is listening, `false` otherwise.
 */
bool
wasi_sockets_tcp_is_listening(wasi_tcp_socket_t socket)
{
    int val;
    socklen_t len = sizeof(val);

    // According to the spec, this function should not fail. However, if
    // getsockopt returns an error, we return false, which is a safe default.
    if (getsockopt(socket, SOL_SOCKET, SO_ACCEPTCONN, &val, &len) < 0) {
        return false;
    }

    return val != 0;
}

/**
 * @brief Set the listen backlog size for a TCP socket.
 * @details Implements the `set-listen-backlog-size` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @note In POSIX, the backlog size is set by the `listen()` call itself and
 *       cannot be changed afterward. Therefore, this implementation checks
 *       the socket's state and returns `unsupported` as required by the spec.
 * @param socket The TCP socket handle.
 * @param value The desired backlog size.
 * @return A WASI error code. Returns `invalid-state` if the socket is not
 *         listening, or `unsupported` because the operation cannot be
 *         performed on a listening socket in POSIX.
 */

/**
 * @brief Static helper to get a POSIX socket option.
 * @details This is a generic wrapper around the `getsockopt` system call,
 *          providing standardized error handling.
 * @param sock The socket file descriptor.
 * @param level The protocol level for the option (e.g., SOL_SOCKET).
 * @param optname The name of the option to retrieve.
 * @param[out] optval A pointer to the buffer where the option value will be
 * stored.
 * @param[in,out] optlen A pointer to the size of the optval buffer.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
static void
get_socket_option(int sock, int level, int optname, void *optval,
                  socklen_t *optlen, int *err)
{
    if (!optval || !optlen || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    if (getsockopt(sock, level, optname, optval, optlen) < 0) {
        *err = errno;
        return;
    }
    *err = 0;
}

/**
 * @brief Static helper to set a POSIX socket option.
 * @details This is a generic wrapper around the `setsockopt` system call,
 *          providing standardized error handling.
 * @param sock The socket file descriptor.
 * @param level The protocol level for the option (e.g., SOL_SOCKET).
 * @param optname The name of the option to set.
 * @param optval A pointer to the buffer containing the new option value.
 * @param optlen The size of the optval buffer.
 * @return A WASI error code, `SUCCESS` on success.
 */
static int
set_socket_option(int sock, int level, int optname, const void *optval,
                  socklen_t optlen)
{
    if (!optval) {
        return WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT;
    }
    if (setsockopt(sock, level, optname, optval, optlen) < 0) {
        return errno;
    }
    return WASI_ERROR_CODE_SUCCESS;
}

/**
 * @brief Check if the TCP keep-alive option is enabled.
 * @details Implements the `keep-alive-enabled` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param[out] ret A pointer to a boolean to store the result.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_keep_alive_enabled(wasi_tcp_socket_t socket, bool *ret,
                                    int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the SO_KEEPALIVE socket option.
    get_socket_option(socket, SOL_SOCKET, SO_KEEPALIVE, &val, &len, err);
    if (*err != 0) {
        return;
    }
    *ret = val;
}

/**
 * @brief Enable or disable the TCP keep-alive option.
 * @details Implements the `set-keep-alive-enabled` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param value `true` to enable keep-alive, `false` to disable it.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_set_keep_alive_enabled(wasi_tcp_socket_t socket, bool value)
{
    int val = value;
    // Use the helper to set the SO_KEEPALIVE socket option.
    return set_socket_option(socket, SOL_SOCKET, SO_KEEPALIVE, &val,
                             sizeof(val));
}

/**
 * @brief Get the TCP keep-alive idle time.
 * @details Implements the `keep-alive-idle-time` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface. This is the duration
 *          of inactivity before the first keep-alive probe is sent.
 * @param socket The TCP socket handle.
 * @param[out] ret A pointer to a `wasi_duration_t` to store the result in
 * nanoseconds.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_keep_alive_idle_time(wasi_tcp_socket_t socket,
                                      wasi_duration_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the TCP_KEEPIDLE socket option.
    get_socket_option(socket, IPPROTO_TCP, TCP_KEEPIDLE, &val, &len, err);
    if (*err != 0) {
        return;
    }
    // The POSIX option is in seconds; convert it to nanoseconds for WASI.
    *ret = (wasi_duration_t)val * 1000000000;
}

/**
 * @brief Set the TCP keep-alive idle time.
 * @details Implements the `set-keep-alive-idle-time` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param value The duration of inactivity in nanoseconds.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_set_keep_alive_idle_time(wasi_tcp_socket_t socket,
                                          wasi_duration_t value)
{
    // The POSIX option requires seconds; convert from nanoseconds.
    int val = value / 1000000000;
    // Use the helper to set the TCP_KEEPIDLE socket option.
    return set_socket_option(socket, IPPROTO_TCP, TCP_KEEPIDLE, &val,
                             sizeof(val));
}

/**
 * @brief Get the TCP keep-alive probe interval.
 * @details Implements the `keep-alive-interval` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface. This is the duration
 *          between subsequent keep-alive probes.
 * @param socket The TCP socket handle.
 * @param[out] ret A pointer to a `wasi_duration_t` to store the result in
 * nanoseconds.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_keep_alive_interval(wasi_tcp_socket_t socket,
                                     wasi_duration_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the TCP_KEEPINTVL socket option.
    get_socket_option(socket, IPPROTO_TCP, TCP_KEEPINTVL, &val, &len, err);
    if (*err != 0) {
        return;
    }
    // The POSIX option is in seconds; convert it to nanoseconds for WASI.
    *ret = (wasi_duration_t)val * 1000000000;
}

/**
 * @brief Set the TCP keep-alive probe interval.
 * @details Implements the `set-keep-alive-interval` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param value The duration between probes in nanoseconds.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_set_keep_alive_interval(wasi_tcp_socket_t socket,
                                         wasi_duration_t value)
{
    // The POSIX option requires seconds; convert from nanoseconds.
    int val = value / 1000000000;
    // Use the helper to set the TCP_KEEPINTVL socket option.
    return set_socket_option(socket, IPPROTO_TCP, TCP_KEEPINTVL, &val,
                             sizeof(val));
}

/**
 * @brief Get the TCP keep-alive probe count.
 * @details Implements the `keep-alive-count` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface. This is the number
 *          of probes to send before dropping a connection.
 * @param socket The TCP socket handle.
 * @param[out] ret A pointer to a u32 to store the result.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_keep_alive_count(wasi_tcp_socket_t socket, uint32_t *ret,
                                  int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the TCP_KEEPCNT socket option.
    get_socket_option(socket, IPPROTO_TCP, TCP_KEEPCNT, &val, &len, err);
    if (*err != 0) {
        return;
    }
    *ret = val;
}

/**
 * @brief Set the TCP keep-alive probe count.
 * @details Implements the `set-keep-alive-count` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param value The number of probes to send.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_set_keep_alive_count(wasi_tcp_socket_t socket, uint32_t value)
{
    int val = value;
    // Use the helper to set the TCP_KEEPCNT socket option.
    return set_socket_option(socket, IPPROTO_TCP, TCP_KEEPCNT, &val,
                             sizeof(val));
}

/**
 * @brief Get the IP hop limit (TTL for IPv4) for a TCP socket.
 * @details Implements the `hop-limit` method on the `tcp-socket` resource
 *          from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param[out] ret A pointer to a u8 to store the hop limit value.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_hop_limit(wasi_tcp_socket_t socket, uint8_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the IP_TTL socket option.
    get_socket_option(socket, IPPROTO_IP, IP_TTL, &val, &len, err);
    if (*err != 0) {
        return;
    }
    *ret = val;
}

/**
 * @brief Set the IP hop limit (TTL for IPv4) for a TCP socket.
 * @details Implements the `set-hop-limit` method on the `tcp-socket` resource
 *          from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param value The new hop limit value.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_set_hop_limit(wasi_tcp_socket_t socket, uint8_t value)
{
    int val = value;
    // Use the helper to set the IP_TTL socket option.
    return set_socket_option(socket, IPPROTO_IP, IP_TTL, &val, sizeof(val));
}

/**
 * @brief Get the size of the TCP receive buffer.
 * @details Implements the `receive-buffer-size` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param[out] ret A pointer to a u64 to store the buffer size in bytes.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_receive_buffer_size(wasi_tcp_socket_t socket, uint64_t *ret,
                                     int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the SO_RCVBUF socket option.
    get_socket_option(socket, SOL_SOCKET, SO_RCVBUF, &val, &len, err);
    if (*err != 0) {
        return;
    }
    *ret = val;
}

/**
 * @brief Set the size of the TCP receive buffer.
 * @details Implements the `set-receive-buffer-size` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param value The desired buffer size in bytes.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_set_receive_buffer_size(wasi_tcp_socket_t socket,
                                         uint64_t value)
{
    int val = value;
    // Use the helper to set the SO_RCVBUF socket option.
    return set_socket_option(socket, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
}

/**
 * @brief Get the size of the TCP send buffer.
 * @details Implements the `send-buffer-size` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param[out] ret A pointer to a u64 to store the buffer size in bytes.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_tcp_send_buffer_size(wasi_tcp_socket_t socket, uint64_t *ret,
                                  int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the SO_SNDBUF socket option.
    get_socket_option(socket, SOL_SOCKET, SO_SNDBUF, &val, &len, err);
    if (*err != 0) {
        return;
    }
    *ret = val;
}

/**
 * @brief Set the size of the TCP send buffer.
 * @details Implements the `set-send-buffer-size` method on the `tcp-socket`
 *          resource from the `wasi:sockets/tcp` interface.
 * @param socket The TCP socket handle.
 * @param value The desired buffer size in bytes.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_set_send_buffer_size(wasi_tcp_socket_t socket, uint64_t value)
{
    int val = value;
    // Use the helper to set the SO_SNDBUF socket option.
    return set_socket_option(socket, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));
}

/**
 * @brief Shut down the read, write, or both sides of a TCP connection.
 * @details Implements the `shutdown` method on the `tcp-socket` resource
 *          from the `wasi:sockets/tcp` interface. This is similar to the
 *          `shutdown` system call in POSIX.
 * @param socket The TCP socket handle.
 * @param shutdown_type Which side of the connection to shut down.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_tcp_shutdown(wasi_tcp_socket_t socket,
                          wasi_shutdown_type_t shutdown_type)
{
    int how;
    // Translate the WASI shutdown type to the corresponding POSIX flag.
    switch (shutdown_type) {
        case WASI_SHUTDOWN_TYPE_RECEIVE:
            how = SHUT_RD;
            break;
        case WASI_SHUTDOWN_TYPE_SEND:
            how = SHUT_WR;
            break;
        case WASI_SHUTDOWN_TYPE_BOTH:
            how = SHUT_RDWR;
            break;
        default:
            return WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT;
    }

    if (shutdown(socket, how) < 0) {
        return errno;
    }

    return WASI_ERROR_CODE_SUCCESS;
}

// wasi:sockets/udp-create-socket

/**
 * @brief Create a new UDP socket.
 * @details Implements the `create-udp-socket` function from the
 *          `wasi:sockets/udp-create-socket` interface.
 * @param family The IP address family (IPv4 or IPv6) for the socket.
 * @param[out] ret A pointer to store the newly created UDP socket handle.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_create_udp_socket(wasi_ip_address_family_t family,
                               wasi_udp_socket_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    // Delegate to the generic socket creation helper, specifying SOCK_DGRAM for
    // UDP.
    create_socket(family, SOCK_DGRAM, ret, err);
}

// wasi:sockets/udp

/**
 * @brief Bind a UDP socket to a local address.
 * @details Implements the `start-bind` method on the `udp-socket` resource from
 *          the `wasi:sockets/udp` interface. In this implementation, the bind
 *          operation is performed synchronously.
 * @param socket The UDP socket handle.
 * @param network The network handle.
 * @param local_address The local IP address and port to bind to.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_udp_start_bind(wasi_udp_socket_t socket, wasi_network_t network,
                            const wasi_ip_socket_address_t *local_address)
{
    if (!local_address) {
        return EINVAL;
    }
    if (!is_valid_network(network)) {
        return EINVAL;
    }
    struct sockaddr_storage native_addr;
    socklen_t native_addr_len;

    // Convert the abstract WASI address into a native POSIX sockaddr struct.
    int err = convert_socket_addr_to_native(local_address, &native_addr,
                                            &native_addr_len);
    if (err != 0) {
        return err;
    }

    // Set SO_REUSEADDR to avoid EADDRINUSE errors on restart
    int reuse = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))
        < 0) {
        return errno;
    }

    // Perform the POSIX bind system call.
    if (bind(socket, (struct sockaddr *)&native_addr, native_addr_len) < 0) {
        return errno;
    }

    return WASI_ERROR_CODE_SUCCESS;
}

/**
 * @brief Complete a bind operation for a UDP socket.
 * @details Implements the `finish-bind` method on the `udp-socket` resource.
 * @note Since `start-bind` is implemented synchronously in this host, this
 *       function is a no-op and always returns success.
 * @param socket The UDP socket handle.
 * @return Always returns `0`.
 */
int
wasi_sockets_udp_finish_bind(wasi_udp_socket_t socket)
{
    // Nothing to do here, as the bind operation is completed synchronously
    // in the `start-bind` function.
    return WASI_ERROR_CODE_SUCCESS;
}

/**
 * @brief Create datagram streams for a UDP socket.
 * @details Implements the `stream` method on the `udp-socket` resource from the
 *          `wasi:sockets/udp` interface. If a remote address is provided, the
 *          socket is "connected" to that peer. It returns new, independent
 *          handles for the incoming and outgoing streams.
 * @param socket The UDP socket handle.
 * @param remote_address An optional remote address to connect the socket to.
 * @param[out] input_stream A pointer to store the new incoming datagram stream
 * handle.
 * @param[out] output_stream A pointer to store the new outgoing datagram stream
 * handle.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_udp_stream(wasi_udp_socket_t socket,
                        const wasi_ip_socket_address_t *remote_address,
                        wasi_incoming_datagram_stream_t *input_stream,
                        wasi_outgoing_datagram_stream_t *output_stream,
                        int *err)
{
    if (!input_stream || !output_stream || !err) {
        if (err)
            *err = EINVAL;
        return;
    }

    *input_stream = -1;
    *output_stream = -1;

    // If a remote address is provided, "connect" the UDP socket. This sets the
    // default destination for sends and filters incoming packets.
    if (remote_address) {
        struct sockaddr_storage native_addr;
        socklen_t native_addr_len;
        int e = convert_socket_addr_to_native(remote_address, &native_addr,
                                              &native_addr_len);
        if (e != 0) {
            *err = e;
            return;
        }
        if (connect(socket, (struct sockaddr *)&native_addr, native_addr_len)
            < 0) {
            *err = errno;
            return;
        }
    }
    // Duplicate the socket descriptor to create independent handles for the
    // input and output streams, allowing their lifecycles to be managed
    // separately. Use F_DUPFD_CLOEXEC to atomically create a new file
    // descriptor with the close-on-exec flag set. The O_NONBLOCK flag is
    // inherited.
    *input_stream = fcntl(socket, F_DUPFD_CLOEXEC, 0);
    if (*input_stream < 0) {
        *err = errno;
        return;
    }

    *output_stream = fcntl(socket, F_DUPFD_CLOEXEC, 0);
    if (*output_stream < 0) {
        close(*input_stream);
        *err = errno;
        return;
    }
    *err = 0;
}

/**
 * @brief Get the local address of a UDP socket.
 * @details Implements the `local-address` method on the `udp-socket` resource
 *          from the `wasi:sockets/udp` interface. This is similar to
 * `getsockname` in POSIX.
 * @param socket The UDP socket handle.
 * @param[out] ret A pointer to store the resulting local IP socket address.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_udp_local_address(wasi_udp_socket_t socket,
                               wasi_ip_socket_address_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    struct sockaddr_storage native_addr;
    socklen_t native_addr_len = sizeof(native_addr);
    // Use the POSIX getsockname call to retrieve the local address.
    if (getsockname(socket, (struct sockaddr *)&native_addr, &native_addr_len)
        < 0) {
        *err = errno;
        return;
    }

    // Convert the native POSIX address back to the abstract WASI format.
    *err = convert_socket_addr_from_native(ret, &native_addr);
}

/**
 * @brief Get the remote address of a UDP socket.
 * @details Implements the `remote-address` method on the `udp-socket` resource
 *          from the `wasi:sockets/udp` interface. This is similar to
 * `getpeername` in POSIX and will only succeed if the socket is "connected".
 * @param socket The UDP socket handle.
 * @param[out] ret A pointer to store the resulting remote IP socket address.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_udp_remote_address(wasi_udp_socket_t socket,
                                wasi_ip_socket_address_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    struct sockaddr_storage native_addr;
    socklen_t native_addr_len = sizeof(native_addr);
    // Use the POSIX getpeername call to retrieve the remote (peer) address.
    if (getpeername(socket, (struct sockaddr *)&native_addr, &native_addr_len)
        < 0) {
        *err = errno;
        return;
    }

    // Convert the native POSIX address back to the abstract WASI format.
    *err = convert_socket_addr_from_native(ret, &native_addr);
}

/**
 * @brief Get the unicast hop limit for a UDP socket.
 * @details Implements the `unicast-hop-limit` method on the `udp-socket`
 *          resource from the `wasi:sockets/udp` interface.
 * @param socket The UDP socket handle.
 * @param[out] ret A pointer to a u8 to store the hop limit value.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_udp_unicast_hop_limit(wasi_udp_socket_t socket, uint8_t *ret,
                                   int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the IP_TTL socket option.
    get_socket_option(socket, IPPROTO_IP, IP_TTL, &val, &len, err);
    if (*err != 0) {
        return;
    }
    *ret = val;
}

/**
 * @brief Set the unicast hop limit for a UDP socket.
 * @details Implements the `set-unicast-hop-limit` method on the `udp-socket`
 *          resource from the `wasi:sockets/udp` interface.
 * @param socket The UDP socket handle.
 * @param value The new hop limit value.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_udp_set_unicast_hop_limit(wasi_udp_socket_t socket, uint8_t value)
{
    int val = value;
    // Use the helper to set the IP_TTL socket option.
    return set_socket_option(socket, IPPROTO_IP, IP_TTL, &val, sizeof(val));
}

/**
 * @brief Get the size of the UDP receive buffer.
 * @details Implements the `receive-buffer-size` method on the `udp-socket`
 *          resource from the `wasi:sockets/udp` interface.
 * @param socket The UDP socket handle.
 * @param[out] ret A pointer to a u64 to store the buffer size in bytes.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_udp_receive_buffer_size(wasi_udp_socket_t socket, uint64_t *ret,
                                     int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the SO_RCVBUF socket option.
    get_socket_option(socket, SOL_SOCKET, SO_RCVBUF, &val, &len, err);
    if (*err != 0) {
        return;
    }
    *ret = val;
}

/**
 * @brief Set the size of the UDP receive buffer.
 * @details Implements the `set-receive-buffer-size` method on the `udp-socket`
 *          resource from the `wasi:sockets/udp` interface.
 * @param socket The UDP socket handle.
 * @param value The desired buffer size in bytes.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_udp_set_receive_buffer_size(wasi_udp_socket_t socket,
                                         uint64_t value)
{
    int val = value;
    // Use the helper to set the SO_RCVBUF socket option.
    return set_socket_option(socket, SOL_SOCKET, SO_RCVBUF, &val, sizeof(val));
}

/**
 * @brief Get the size of the UDP send buffer.
 * @details Implements the `send-buffer-size` method on the `udp-socket`
 *          resource from the `wasi:sockets/udp` interface.
 * @param socket The UDP socket handle.
 * @param[out] ret A pointer to a u64 to store the buffer size in bytes.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_udp_send_buffer_size(wasi_udp_socket_t socket, uint64_t *ret,
                                  int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    int val;
    socklen_t len = sizeof(val);
    // Use the helper to query the SO_SNDBUF socket option.
    get_socket_option(socket, SOL_SOCKET, SO_SNDBUF, &val, &len, err);
    if (*err != 0) {
        return;
    }
    *ret = val;
}

/**
 * @brief Set the size of the UDP send buffer.
 * @details Implements the `set-send-buffer-size` method on the `udp-socket`
 *          resource from the `wasi:sockets/udp` interface.
 * @param socket The UDP socket handle.
 * @param value The desired buffer size in bytes.
 * @return A WASI error code, `SUCCESS` on success.
 */
int
wasi_sockets_udp_set_send_buffer_size(wasi_udp_socket_t socket, uint64_t value)
{
    int val = value;
    // Use the helper to set the SO_SNDBUF socket option.
    return set_socket_option(socket, SOL_SOCKET, SO_SNDBUF, &val, sizeof(val));
}

/**
 * @brief Receive incoming datagrams from a stream.
 * @details Implements the `receive` method on the `incoming-datagram-stream`
 *          resource from the `wasi:sockets/udp` interface. This function is
 *          non-blocking and attempts to receive up to `max_results` datagrams.
 * @note The caller is responsible for freeing the returned array of datagrams
 *       and the `data` buffer within each datagram.
 * @param stream The incoming datagram stream handle.
 * @param max_results The maximum number of datagrams to receive.
 * @param[out] ret A pointer to store the array of received datagrams.
 * @param[out] ret_len A pointer to store the number of datagrams in the array.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_udp_receive(wasi_incoming_datagram_stream_t stream,
                         uint64_t max_results, wasi_incoming_datagram_t **ret,
                         uint64_t *ret_len, int *err)
{
    if (!ret || !ret_len || !err) {
        if (err)
            *err = EINVAL;
        return;
    }

    *ret = NULL;
    *ret_len = 0;
    *err = 0;

    wasi_incoming_datagram_t *datagrams = NULL;
    struct iovec *iovs = NULL;
    struct mmsghdr *msgs = NULL;
    struct sockaddr_storage *native_addrs = NULL;
    uint64_t i;
    int n = 0;

    *ret = NULL;
    *ret_len = 0;
    *err = 0;

    if (max_results == 0) {
        return;
    }

    if (max_results > UIO_MAXIOV) {
        max_results = UIO_MAXIOV;
    }

    // Allocate all necessary memory upfront.
    datagrams =
        wasm_runtime_malloc(sizeof(wasi_incoming_datagram_t) * max_results);
    if (!datagrams) {
        *err = ENOMEM;
        goto cleanup;
    }
    memset(datagrams, 0, sizeof(wasi_incoming_datagram_t) * max_results);

    for (i = 0; i < max_results; i++) {
        datagrams[i].data = wasm_runtime_malloc(2048);
        if (!datagrams[i].data) {
            *err = ENOMEM;
            goto cleanup;
        }
    }

    iovs = wasm_runtime_malloc(sizeof(struct iovec) * max_results);
    if (!iovs) {
        *err = ENOMEM;
        goto cleanup;
    }

    msgs = wasm_runtime_malloc(sizeof(struct mmsghdr) * max_results);
    if (!msgs) {
        *err = ENOMEM;
        goto cleanup;
    }

    native_addrs =
        wasm_runtime_malloc(sizeof(struct sockaddr_storage) * max_results);
    if (!native_addrs) {
        *err = ENOMEM;
        goto cleanup;
    }

    for (i = 0; i < max_results; i++) {
        iovs[i].iov_base = datagrams[i].data;
        iovs[i].iov_len = 2048;
        msgs[i].msg_hdr.msg_name = &native_addrs[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_storage);
        msgs[i].msg_hdr.msg_iov = &iovs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_control = NULL;
        msgs[i].msg_hdr.msg_controllen = 0;
        msgs[i].msg_hdr.msg_flags = 0;
    }

// Use recvmmsg for efficiency on Linux, otherwise fall back to recvmsg loop.
#ifdef __linux__
    n = recvmmsg(stream, msgs, max_results, 0, NULL);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            n = 0;
        }
        else {
            *err = errno;
            goto cleanup;
        }
    }
#else
    for (i = 0; i < max_results; i++) {
        ssize_t s = recvmsg(stream, &msgs[i].msg_hdr, 0);
        if (s < 0) {
            // If no more messages are available, stop receiving.
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            *err = errno;
            goto cleanup;
        }
        msgs[i].msg_len = s;
        n++;
    }
#endif

    for (i = 0; i < (uint64_t)n; i++) {
        datagrams[i].data_len = msgs[i].msg_len;
        if (msgs[i].msg_len == 0) {
            wasm_runtime_free(datagrams[i].data);
            datagrams[i].data = NULL;
        }
        else {
            uint8_t *new_data =
                wasm_runtime_realloc(datagrams[i].data, msgs[i].msg_len);
            if (!new_data) {
                *err = ENOMEM;
                goto cleanup;
            }
            datagrams[i].data = new_data;
        }
        convert_socket_addr_from_native(
            &datagrams[i].remote_address,
            (struct sockaddr_storage *)msgs[i].msg_hdr.msg_name);
    }

    for (i = n; i < max_results; i++) {
        wasm_runtime_free(datagrams[i].data);
        datagrams[i].data = NULL;
    }

    if (n == 0) {
        wasm_runtime_free(datagrams);
        datagrams = NULL;
    }

    *ret = datagrams;
    *ret_len = n;
    *err = 0;

    wasm_runtime_free(native_addrs);
    wasm_runtime_free(msgs);
    wasm_runtime_free(iovs);
    return;

cleanup:
    if (datagrams) {
        for (i = 0; i < max_results; i++) {
            if (datagrams[i].data) {
                wasm_runtime_free(datagrams[i].data);
            }
        }
        wasm_runtime_free(datagrams);
    }
    if (native_addrs) {
        wasm_runtime_free(native_addrs);
    }
    if (msgs) {
        wasm_runtime_free(msgs);
    }
    if (iovs) {
        wasm_runtime_free(iovs);
    }
    *ret = NULL;
    *ret_len = 0;
}

/**
 * @brief Check readiness for sending datagrams on an outgoing stream.
 * @details Implements the `check-send` method on the `outgoing-datagram-stream`
 *          resource from the `wasi:sockets/udp` interface. This function is
 *          non-blocking and returns a permit indicating the number of datagrams
 *          that can be sent without blocking.
 * @param stream The outgoing datagram stream handle.
 * @param[out] ret A pointer to a u64 to store the send permit size (number of
 *                 datagrams). Returns 0 if the stream is not ready for sending.
 */
void
wasi_sockets_udp_check_send(wasi_outgoing_datagram_stream_t stream,
                            uint64_t *ret, int *err)
{
    if (!ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    struct pollfd pfd;
    pfd.fd = stream;
    pfd.events = POLLOUT; // Check for writability.

    // Use poll with a timeout of 0 to perform a non-blocking check.
    int n = poll(&pfd, 1, 0);

    if (n < 0) {
        *err = errno;
        *ret = 0;
        return;
    }

    // If poll returns > 0 and the POLLOUT event is set, the socket is ready.
    if (n > 0 && (pfd.revents & POLLOUT)) {
        // Return a large permit size. UIO_MAXIOV is a reasonable choice,
        // indicating that a batch of datagrams can be sent.
        *ret = UIO_MAXIOV;
        *err = 0;
    }
    else {
        // Otherwise, the stream is not ready for sending.
        *ret = 0;
        *err = 0;
    }
}

/**
 * @brief Send datagrams on an outgoing stream.
 * @details Implements the `send` method on the `outgoing-datagram-stream`
 *          resource from the `wasi:sockets/udp` interface. This function is
 *          non-blocking and attempts to send a list of datagrams.
 * @param stream The outgoing datagram stream handle.
 * @param datagrams An array of datagrams to send.
 * @param datagrams_len The number of datagrams in the array.
 * @param[out] ret A pointer to store the number of datagrams successfully sent.
 * @param[out] err A pointer to store the resulting WASI error code.
 */
void
wasi_sockets_udp_send(wasi_outgoing_datagram_stream_t stream,
                      const wasi_outgoing_datagram_t *datagrams,
                      uint64_t datagrams_len, uint64_t *ret, int *err)
{
    if (!datagrams || !ret || !err) {
        if (err)
            *err = EINVAL;
        return;
    }
    struct mmsghdr *msgs = NULL;
    struct iovec *iovs = NULL;
    struct sockaddr_storage *native_addrs = NULL;
    uint64_t n = 0;

    *ret = 0;
    *err = -1; // -1 success, >= 0 network error code

    if (datagrams_len == 0) {
        return;
    }

    // Allocate memory for the POSIX message structures.
    msgs = wasm_runtime_malloc(sizeof(struct mmsghdr) * datagrams_len);
    if (!msgs) {
        *err = ENOMEM;
        goto cleanup;
    }
    iovs = wasm_runtime_malloc(sizeof(struct iovec) * datagrams_len);
    if (!iovs) {
        *err = ENOMEM;
        goto cleanup;
    }

    native_addrs =
        wasm_runtime_malloc(sizeof(struct sockaddr_storage) * datagrams_len);
    if (!native_addrs) {
        *err = ENOMEM;
        goto cleanup;
    }

    // Prepare each message header for the sendmsg calls.
    for (uint64_t i = 0; i < datagrams_len; i++) {
        iovs[i].iov_base = (void *)datagrams[i].data;
        iovs[i].iov_len = datagrams[i].data_len;
        msgs[i].msg_hdr.msg_iov = &iovs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_control = 0;
        msgs[i].msg_hdr.msg_controllen = 0;
        msgs[i].msg_hdr.msg_flags = 0;

        // If a remote address is provided for the datagram, convert it.
        // Otherwise, send to the socket's connected peer (if any).
        if (datagrams[i].remote_address) {
            socklen_t native_addr_len = 0;
            int res = convert_socket_addr_to_native(datagrams[i].remote_address,
                                                    &native_addrs[i],
                                                    &native_addr_len);
            if (res != 0) {
                *err = res;
                goto cleanup;
            }
            msgs[i].msg_hdr.msg_name = &native_addrs[i];
            msgs[i].msg_hdr.msg_namelen = native_addr_len;
        }
        else {
            msgs[i].msg_hdr.msg_name = NULL;
            msgs[i].msg_hdr.msg_namelen = 0;
        }
    }

// Use sendmmsg for efficiency on Linux, otherwise fall back to sendmsg loop.
#ifdef __linux__
    int sent_count = sendmmsg(stream, msgs, datagrams_len, MSG_NOSIGNAL);
    if (sent_count < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            *err = errno_to_wasi_network(errno);
        }
    }
    else {
        n = sent_count;
    }
#else
    // Send datagrams one by one in a non-blocking loop.
    for (n = 0; n < datagrams_len; n++) {
        ssize_t s = sendmsg(stream, &msgs[n].msg_hdr, MSG_NOSIGNAL);
        if (s < 0) {
            // If the send buffer is full, stop sending and return the count.
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                *err = errno_to_wasi_network(errno);
            }
            break;
        }
    }
#endif
    *ret = n;

cleanup:
    wasm_runtime_free(msgs);
    wasm_runtime_free(iovs);
    wasm_runtime_free(native_addrs);
}