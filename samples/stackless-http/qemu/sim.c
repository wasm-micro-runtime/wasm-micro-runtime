/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "http.h"
#include "handler.h"
#include "wasm_export.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int
semi(int operation, uintptr_t argument);
int
pico_libc_peak(void);
static http_server server;
static _Alignas(16) uint8_t pool[128 * 1024];
static volatile unsigned wakes;
#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            printf("FAIL %d: %s: %s\n", __LINE__, #condition, \
                   server.vm.error);                          \
            fflush(stdout);                                   \
            exit(1);                                          \
        }                                                     \
    } while (0)
#define SYST_CSR (*(volatile uint32_t *)0xe000e010)
#define SYST_RVR (*(volatile uint32_t *)0xe000e014)
#define SYST_CVR (*(volatile uint32_t *)0xe000e018)

void
systick(void)
{
    SYST_CSR = 0;
    ++wakes;
}
__attribute__((noipa, used)) void
native_begin(void)
{
    __asm__ volatile("nop" ::: "memory");
}
__attribute__((noipa, used)) void
native_end(void)
{
    __asm__ volatile("yield" ::: "memory");
}
__attribute__((noipa, used)) void
idle_begin(void)
{
    __asm__ volatile("nop; nop" ::: "memory");
}
__attribute__((noipa, used)) void
idle_end(void)
{
    __asm__ volatile("yield; nop" ::: "memory");
}

static void
wait_for_irq(void)
{
    SYST_RVR = 20000;
    SYST_CVR = 0;
    SYST_CSR = 7;
    idle_begin();
    for (;;) {
        __asm__ volatile("cpsid i" ::: "memory");
        if (wakes) {
            __asm__ volatile("cpsie i" ::: "memory");
            break;
        }
        __asm__ volatile("wfi; cpsie i" ::: "memory");
    }
    idle_end();
}
static void
send_request(unsigned slot, const char *request)
{
    CHECK(http_input(&server, slot, (uint8_t *)request,
                     (uint32_t)strlen(request)));
    CHECK(http_progress(&server, slot));
}
static void
drain(unsigned slot, bool large)
{
    const char *header = large ? "HTTP/1.1 200 OK\r\nContent-Length: "
                                 "65536\r\nConnection: close\r\n\r\n"
                               : "HTTP/1.1 200 OK\r\nContent-Length: "
                                 "15\r\nConnection: close\r\n\r\n";
    const char *small = "stackless wasm\n";
    size_t count = 0, header_length = strlen(header);
    http_connection *c = &server.connections[slot];
    for (unsigned turn = 0; turn < 6000 && !http_finished(c); ++turn) {
        CHECK(http_progress(&server, slot));
        size_t chunk = c->length - c->sent;
        if (chunk > 37)
            chunk = 37; /* Simulated small transport window. */
        for (size_t j = 0; j < chunk; ++j, ++count) {
            CHECK(count < header_length + (large ? 65536 : strlen(small)));
            uint8_t expected =
                (uint8_t)(count < header_length ? header[count]
                          : large ? 'a' + ((count - header_length) % 26)
                                  : small[count - header_length]);
            CHECK(c->output[c->sent + j] == expected);
        }
        c->sent += chunk;
    }
    CHECK(count == header_length + (large ? 65536 : strlen(small)));
    CHECK(http_reap(&server, slot));
}
static void
disconnect(unsigned slot)
{
    CHECK(http_disconnect(&server, slot));
    CHECK(http_progress(&server, slot));
    CHECK(http_reap(&server, slot));
}
static void
run_http(void)
{
    for (unsigned i = 0; i < HTTP_CONNECTIONS; ++i)
        CHECK(http_open(&server, i));
    send_request(0, "GET / HTTP/1.1\r\nHost:");
    send_request(1, "GET /large HTTP/1.1\r\n\r\n");
    CHECK(server.connections[1].task.returned
          && !server.connections[1].task.exited);
    uint32_t callbacks = server.callbacks;
    CHECK(http_progress(&server, 1));
    CHECK(server.callbacks
          == callbacks); /* No room in the native output buffer. */
    drain(1, true);
    send_request(0, " localhost\r\n\r\n");
    drain(0, false);
    disconnect(2);
    send_request(3, "GET /large HTTP/1.1\r\n\r\n");
    CHECK(server.connections[3].task.returned);
    disconnect(3);
    for (unsigned i = 0; i < 32; ++i) {
        CHECK(http_open(&server, 0));
        send_request(0, "GET / HTTP/1.1\r\n\r\n");
        drain(0, false);
        CHECK(server.vm.async.live == 0);
    }
    CHECK(server.completed == 34 && server.cancelled == 1
          && server.disconnected == 1);
}
int
main(void)
{
    MemAllocOption mem = { 0 };
    mem.pool.heap_buf = pool;
    mem.pool.heap_size = sizeof(pool);
    CHECK(p3_vm_open_with_engine(
        &server.vm, wasm_engine_new_with_args(Alloc_With_Pool, &mem),
        handler_wasm, sizeof(handler_wasm)));
    char command[64] = { 0 };
    uintptr_t cmd[] = { (uintptr_t)command, sizeof(command) };
    semi(0x15, (uintptr_t)cmd);
    bool count = strstr(command, "count") != NULL;
    if (!count)
        run_http();
    native_begin();
    bool ok = http_native_loop(&server, 1000);
    native_end();
    CHECK(ok);
    wait_for_irq();
    CHECK(wakes == 1 && server.vm.async.live == 0);
    mem_alloc_info_t usage = { 0 };
    CHECK(wasm_runtime_get_mem_alloc_info(&usage));
    CHECK(usage.highmark_size >= 65536 && usage.highmark_size <= sizeof(pool));
    printf(
        "M33 PASS completed=%lu cancelled=%lu disconnected=%lu live=%lu "
        "peak_handles=%lu "
        "pool_peak=%lu pool_reserved=%lu libc_peak=%d idle_wakes=%u "
        "native_imports=1000\n",
        (unsigned long)server.completed, (unsigned long)server.cancelled,
        (unsigned long)server.disconnected, (unsigned long)server.vm.async.live,
        (unsigned long)server.vm.async.peak, (unsigned long)usage.highmark_size,
        (unsigned long)sizeof(pool), pico_libc_peak(), wakes);
    p3_vm_close(&server.vm);
    fflush(stdout);
    return 0;
}
