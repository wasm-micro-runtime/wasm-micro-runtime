/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
        .text
        .align  2
#ifndef BH_PLATFORM_DARWIN
        .globl invokeNative
        .type  invokeNative, function
invokeNative:
#else
        .globl _invokeNative
_invokeNative:
#endif /* end of BH_PLATFORM_DARWIN */
        .cfi_startproc
/*
 * Arguments passed in:
 *
 * r0 function ptr
 * r1 argv
 * r2 argc
 */

        push    {r4, r5, r6, r7}
        push    {lr}
        sub     sp, sp, #4      /* make sp 8 byte aligned */
        .cfi_def_cfa_offset 24
        .cfi_offset lr, -20
        .cfi_offset r4, -16
        .cfi_offset r5, -12
        .cfi_offset r6, -8
        .cfi_offset r7, -4
        mov     ip, r0          /* ip = function ptr */
        mov     r4, r1          /* r4 = argv */
        mov     r5, r2          /* r5 = argc */

        cmp     r5, #1          /* at least one argument required: exec_env */
        blt     .Lreturn

        mov     r6, #0          /* increased stack size */

        ldr     r0, [r4]        /* r0 = argv[0] = exec_env */
        add     r4, r4, #4      /* r4 += 4 */
        cmp     r5, #1
        beq     .Lcall_func

        ldr     r1, [r4]        /* r1 = argv[1] */
        add     r4, r4, #4
        cmp     r5, #2
        beq     .Lcall_func

        ldr     r2, [r4]        /* r2 = argv[2] */
        add     r4, r4, #4
        cmp     r5, #3
        beq     .Lcall_func

        ldr     r3, [r4]        /* r3 = argv[3] */
        add     r4, r4, #4
        cmp     r5, #4
        beq     .Lcall_func

        sub    r5, r5, #4       /* argc -= 4, now we have r0 ~ r3 */

        /* Reserve stack for the remaining args, rounded up to 8 bytes so SP
           stays 8-byte aligned at the blx below (AAPCS requires 8-byte SP
           alignment at a public interface). SP is already 8-aligned here:
           the prologue pushed r4-r7 + lr (20 bytes) and did sub sp,#4, i.e.
           24 bytes (see .cfi_def_cfa_offset 24). The reservation must
           therefore be a multiple of 8 and nothing more. */
        lsl     r6, r5, #2      /* r6 = argc * 4 */
        mov     r7, #7
        add     r6, r6, r7      /* r6 = (r6 + 7) & ~7 */
        bic     r6, r6, r7
        mov     r7, sp
        sub     r7, r7, r6      /* reserved stack space for left arguments */
        mov     sp, r7

        mov     lr, r2          /* save r2 */

.Lloop_args:                      /* copy left arguments to stack */
        cmp     r5, #0
        beq     .Lcall_func1
        ldr     r2, [r4]
        add     r4, r4, #4
        str     r2, [r7]
        add     r7, r7, #4
        sub     r5, r5, #1
        b       .Lloop_args

.Lcall_func1:
        mov     r2, lr          /* restore r2 */

.Lcall_func:
        blx     ip
        add     sp, sp, r6       /* restore sp */

.Lreturn:
        add     sp, sp, #4      /* make sp 8 byte aligned */
        pop     {r3}
        pop     {r4, r5, r6, r7}
        mov     lr, r3
        bx      lr
        .cfi_endproc
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
