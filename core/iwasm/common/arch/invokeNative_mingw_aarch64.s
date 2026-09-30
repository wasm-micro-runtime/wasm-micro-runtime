/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

        .text
        .align  2
        .globl invokeNative
invokeNative:

/*
 * Arguments passed in:
 *
 * x0 function ptr
 * x1 argv
 * x2 nstacks
 */

        sub     sp, sp, #0x30
        stp     x19, x20, [sp, #0x20]
        stp     x21, x22, [sp, #0x10]
        stp     x23, x24, [sp, #0x0]

        mov     x19, x0
        mov     x20, x1
        mov     x21, x2
        mov     x22, sp

        /* Fill floating-point registers. */
        ldp     d0, d1, [x20], #16
        ldp     d2, d3, [x20], #16
        ldp     d4, d5, [x20], #16
        ldp     d6, d7, [x20], #16

        /* Fill integer registers. */
        ldp     x0, x1, [x20], #16
        ldp     x2, x3, [x20], #16
        ldp     x4, x5, [x20], #16
        ldp     x6, x7, [x20], #16

        cmp     x21, #0
        beq     call_func

        /* Reserve aligned stack space and copy stack arguments. */
        mov     x23, sp
        bic     sp, x23, #15
        lsl     x23, x21, #3
        add     x23, x23, #15
        bic     x23, x23, #15
        sub     sp, sp, x23
        mov     x23, sp

copy_stack_args:
        cmp     x21, #0
        beq     call_func
        ldr     x24, [x20], #8
        str     x24, [x23], #8
        sub     x21, x21, #1
        b       copy_stack_args

call_func:
        mov     x20, x30
        blr     x19
        mov     sp, x22

        mov     x30, x20
        ldp     x19, x20, [sp, #0x20]
        ldp     x21, x22, [sp, #0x10]
        ldp     x23, x24, [sp, #0x0]
        add     sp, sp, #0x30
        ret
