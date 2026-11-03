/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

int
main(void);
void
systick(void);
int
semi(int operation, uintptr_t argument)
{
    register int r0 __asm__("r0") = operation;
    register uintptr_t r1 __asm__("r1") = argument;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
void __attribute__((noreturn))
_exit(int status)
{
    uint32_t params[2] = { 0x20026, (uint32_t)status };
    semi(0x20, (uintptr_t)params);
    for (;;)
        __asm__ volatile("wfi");
}
int
_write(int fd, char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; ++i)
        semi(3, (uintptr_t)&buf[i]);
    return len;
}
static void
fault(void)
{
    semi(4, (uintptr_t)"FAULT Cortex-M33\n");
    _exit(90);
}
void
reset(void)
{
    extern char __data_start__, __data_end__, __data_load__, __bss_start__,
        __bss_end__;
    memcpy(&__data_start__, &__data_load__,
           (uintptr_t)&__data_end__ - (uintptr_t)&__data_start__);
    memset(&__bss_start__, 0,
           (uintptr_t)&__bss_end__ - (uintptr_t)&__bss_start__);
    _exit(main());
}
extern uint32_t __StackTop;
__attribute__((section(".vectors"), used)) const uintptr_t vectors[16] = {
    (uintptr_t)&__StackTop, (uintptr_t)reset, (uintptr_t)fault,
    (uintptr_t)fault,       (uintptr_t)fault, (uintptr_t)fault,
    (uintptr_t)fault,       (uintptr_t)fault, (uintptr_t)fault,
    (uintptr_t)fault,       (uintptr_t)fault, (uintptr_t)fault,
    (uintptr_t)fault,       (uintptr_t)fault, (uintptr_t)fault,
    (uintptr_t)systick
};
