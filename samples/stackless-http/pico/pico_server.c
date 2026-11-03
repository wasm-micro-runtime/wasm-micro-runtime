/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "http.h"
#include "handler.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/sem.h"
#include "lwip/tcp.h"
#include "lwip/stats.h"
#include "wasm_export.h"
#include <stdio.h>
#include <string.h>
#define REQUEST_US UINT64_C(10000000)
int
pico_libc_peak(void);

/* lwIP callbacks own only network slots. No guest execution or guest memory
 * access occurs in IRQ context, under the lwIP lock, or on core 1. */
typedef struct network_slot {
    struct tcp_pcb *pcb;
    struct pbuf *input;
    uint16_t received;
    uint32_t unacked;
    uint64_t deadline;
    bool reserved, closed;
} network_slot;
static network_slot network[HTTP_CONNECTIONS];
static http_server server;
static semaphore_t wake;
static _Alignas(16) uint8_t runtime_pool[128 * 1024];

static void
signal_work(void)
{
    sem_release(&wake);
}
static void
link_changed(struct netif *n)
{
    (void)n;
    signal_work();
}
static void
network_error(void *env, err_t error)
{
    (void)error;
    network_slot *s = env;
    s->pcb = NULL;
    s->closed = true;
    signal_work();
}
static err_t
network_sent(void *env, struct tcp_pcb *pcb, u16_t bytes)
{
    (void)pcb;
    network_slot *s = env;
    if (bytes > s->unacked)
        panic("TCP ACK accounting");
    s->unacked -= bytes;
    signal_work();
    return ERR_OK;
}
static err_t
network_received(void *env, struct tcp_pcb *pcb, struct pbuf *data, err_t error)
{
    (void)pcb;
    network_slot *s = env;
    if (!data || error != ERR_OK) {
        if (data)
            pbuf_free(data);
        s->closed = true;
    }
    else if (s->input)
        return ERR_MEM; /* lwIP retains it and applies backpressure. */
    else {
        s->input = data;
        s->received = 0;
    }
    signal_work();
    return ERR_OK;
}
static err_t
network_accept(void *env, struct tcp_pcb *pcb, err_t error)
{
    (void)env;
    if (error != ERR_OK || !pcb)
        return ERR_VAL;
    for (unsigned i = 0; i < HTTP_CONNECTIONS; ++i) {
        network_slot *s = &network[i];
        if (s->reserved)
            continue;
        *s = (network_slot){ .pcb = pcb,
                             .reserved = true,
                             .deadline = time_us_64() + REQUEST_US };
        tcp_arg(pcb, s);
        tcp_err(pcb, network_error);
        tcp_recv(pcb, network_received);
        tcp_sent(pcb, network_sent);
        signal_work();
        return ERR_OK;
    }
    tcp_abort(pcb);
    return ERR_ABRT;
}
static void
clear_input(network_slot *s)
{
    if (s->input)
        pbuf_free(s->input);
    s->input = NULL;
}
static void
abort_connection(network_slot *s)
{
    if (s->pcb) {
        tcp_arg(s->pcb, NULL);
        tcp_err(s->pcb, NULL);
        tcp_recv(s->pcb, NULL);
        tcp_sent(s->pcb, NULL);
        tcp_abort(s->pcb);
    }
    s->pcb = NULL;
    clear_input(s);
}
static bool
pump(unsigned i)
{
    network_slot *s = &network[i];
    http_connection *c = &server.connections[i];
    uint8_t data[HTTP_CHUNK];
    cyw43_arch_lwip_begin();
    bool reserved = s->reserved;
    cyw43_arch_lwip_end();
    if (!reserved)
        return false;
    bool progress = false;
    if (!c->live) {
        if (!http_open(&server, i))
            panic("component start: %s", server.vm.error);
        progress = true;
    }
    uint32_t limit =
        c->input ? p3_native_capacity(&server.vm.async, c->input) : 0;
    if (limit > sizeof(data))
        limit = sizeof(data);
    cyw43_arch_lwip_begin();
    bool closed = s->closed || time_us_64() >= s->deadline;
    uint16_t count = 0;
    if (!closed && limit && s->input) {
        count = pbuf_copy_partial(s->input, data, (u16_t)limit, s->received);
        s->received += count;
        tcp_recved(s->pcb, count);
        if (s->received == s->input->tot_len)
            clear_input(s);
    }
    if (closed)
        abort_connection(s);
    cyw43_arch_lwip_end();
    if (closed && !c->disconnected) {
        if (!http_disconnect(&server, i))
            panic("component disconnect");
        progress = true;
    }
    if (count) {
        if (!http_input(&server, i, data, count))
            panic("component input");
        progress = true;
    }
    uint32_t callbacks = server.callbacks;
    if (!http_progress(&server, i))
        panic("component callback: %s", server.vm.error);
    progress |= callbacks != server.callbacks;
    cyw43_arch_lwip_begin();
    if (s->pcb && c->sent < c->length) {
        size_t count = c->length - c->sent;
        if (count > tcp_sndbuf(s->pcb))
            count = tcp_sndbuf(s->pcb);
        if (count
            && tcp_write(s->pcb, c->output + c->sent, (u16_t)count,
                         TCP_WRITE_FLAG_COPY)
                   == ERR_OK) {
            c->sent += count;
            s->unacked += count;
            tcp_output(s->pcb);
            progress = true;
        }
    }
    bool finished = http_finished(c) && (!s->pcb || !s->unacked);
    if (finished) {
        if (s->pcb) {
            /* Reject further request bytes after the bounded single request. */
            if (s->input)
                tcp_recved(s->pcb, s->input->tot_len - s->received);
            tcp_arg(s->pcb, NULL);
            tcp_err(s->pcb, NULL);
            tcp_recv(s->pcb, NULL);
            tcp_sent(s->pcb, NULL);
            if (tcp_close(s->pcb) != ERR_OK)
                tcp_abort(s->pcb);
        }
        clear_input(s);
        memset(s, 0, sizeof(*s));
    }
    cyw43_arch_lwip_end();
    if (finished && !http_reap(&server, i))
        panic("component reap");
    return progress || finished || p3_task_ready(&c->task);
}
static void
read_line(char *text, size_t capacity)
{
    size_t length = 0;
    bool overflow = false;
    for (;;) {
        int c = getchar();
        if (c == '\n' || c == '\r') {
            if (length || overflow)
                break;
            continue;
        }
        if (c < 0)
            continue;
        if (length + 1 < capacity)
            text[length++] = (char)c;
        else
            overflow = true;
    }
    if (overflow)
        panic("Wi-Fi credential too long");
    text[length] = 0;
}
int
main(void)
{
    stdio_init_all();
    sem_init(&wake, 0, 1);
    if (cyw43_arch_init())
        panic("Wi-Fi init");
    cyw43_arch_enable_sta_mode();
    char ssid[33], password[64];
    printf("Wi-Fi SSID (then password, input is not echoed):\n");
    read_line(ssid, sizeof(ssid));
    read_line(password, sizeof(password));
    cyw43_arch_lwip_begin();
    netif_set_status_callback(netif_default, link_changed);
    cyw43_arch_lwip_end();
    if (cyw43_arch_wifi_connect_async(ssid, password, CYW43_AUTH_WPA2_AES_PSK))
        panic("Wi-Fi join");
    absolute_time_t deadline = make_timeout_time_ms(30000);
    ip4_addr_t ip;
    for (;;) {
        cyw43_arch_lwip_begin();
        ip = *netif_ip4_addr(netif_default);
        cyw43_arch_lwip_end();
        if (!ip4_addr_isany_val(ip))
            break;
        if (!sem_acquire_block_until(&wake, deadline))
            panic("Wi-Fi DHCP timeout");
    }
    MemAllocOption memory = { 0 };
    memory.pool.heap_buf = runtime_pool;
    memory.pool.heap_size = sizeof(runtime_pool);
    if (!p3_vm_open_with_engine(
            &server.vm, wasm_engine_new_with_args(Alloc_With_Pool, &memory),
            handler_wasm, sizeof(handler_wasm)))
        panic("component load: %s", server.vm.error);
    uint64_t benchmark_start = time_us_64();
    if (!http_native_loop(&server, 100000))
        panic("native call benchmark");
    printf("NATIVE imports=100000 elapsed_us=%llu\n",
           time_us_64() - benchmark_start);
    cyw43_arch_lwip_begin();
    struct tcp_pcb *listener = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!listener || tcp_bind(listener, IP_ANY_TYPE, 8080) != ERR_OK)
        panic("TCP bind");
    listener = tcp_listen(listener);
    if (!listener)
        panic("TCP listen");
    tcp_accept(listener, network_accept);
    cyw43_arch_lwip_end();
    printf("LISTEN http://%s:8080\n", ip4addr_ntoa(&ip));
    uint64_t active = 0;
    uint32_t wakes = 0;
    uint64_t window = time_us_64();
    for (;;) {
        (void)sem_try_acquire(&wake);
        uint64_t start = time_us_64();
        bool progress = false;
        for (unsigned i = 0; i < HTTP_CONNECTIONS; ++i)
            progress |= pump(i);
        active += time_us_64() - start;
        if (time_us_64() - window >= 10000000) {
            mem_alloc_info_t usage = { 0 };
            (void)wasm_runtime_get_mem_alloc_info(&usage);
            cyw43_arch_lwip_begin();
            unsigned net_peak = lwip_stats.mem.max;
            cyw43_arch_lwip_end();
            printf("RESULT elapsed_us=%llu owner_active_us=%llu io_wakes=%u "
                   "wamr_pool_peak=%lu "
                   "lwip_heap_peak=%u libc_heap_peak=%d completed=%lu "
                   "cancelled=%lu live=%lu peak_handles=%lu\n",
                   time_us_64() - window, active, wakes,
                   (unsigned long)usage.highmark_size, net_peak,
                   pico_libc_peak(), (unsigned long)server.completed,
                   (unsigned long)server.cancelled,
                   (unsigned long)server.vm.async.live,
                   (unsigned long)server.vm.async.peak);
            window = time_us_64();
            active = wakes = 0;
        }
        uint64_t until = window + 10000000;
        cyw43_arch_lwip_begin();
        for (unsigned i = 0; i < HTTP_CONNECTIONS; ++i)
            if (network[i].pcb && network[i].deadline < until)
                until = network[i].deadline;
        cyw43_arch_lwip_end();
        if (!progress
            && sem_acquire_block_until(&wake, from_us_since_boot(until)))
            ++wakes;
    }
}
