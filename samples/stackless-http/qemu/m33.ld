/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/* AN505 secure aliases; cap the application region to the Pico 2 W budget. */
MEMORY {
    XIP (rx) : ORIGIN = 0x10000000, LENGTH = 1024K
    RAM (rwx) : ORIGIN = 0x38000000, LENGTH = 520K
}
ENTRY(reset)
PROVIDE(__p3_stack_size = 4K);
SECTIONS {
    .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) . = ALIGN(4); } > XIP
    .ARM.exidx : { *(.ARM.exidx*) } > XIP
    .data : { __data_start__ = .; *(.data*) . = ALIGN(4); __data_end__ = .; } > RAM AT > XIP
    __data_load__ = LOADADDR(.data);
    .bss (NOLOAD) : { __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end__ = .; } > RAM
    .net_reserve (NOLOAD) : { . += 64K; } > RAM
    .heap (NOLOAD) : { __end__ = .; . += 16K; } > RAM
    .stack (NOLOAD) : { __StackBottom = .; . += __p3_stack_size; __StackTop = .; } > RAM
    ASSERT(__StackTop <= ORIGIN(RAM) + LENGTH(RAM), "Pico RAM budget exceeded")
}
