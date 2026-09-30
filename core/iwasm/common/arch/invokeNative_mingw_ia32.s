/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

    .text
    .align 2
    .globl _invokeNative
_invokeNative:
    push    %ebp
    movl    %esp, %ebp
    movl    16(%ebp), %ecx
    leal    2(%ecx), %edx
    andl    $3, %edx
    jz      stack_aligned
    leal    -16(%esp, %edx, 4), %esp
stack_aligned:
    test    %ecx, %ecx
    jz      skip_push_args
    movl    12(%ebp), %edx
    leal    -4(%edx,%ecx,4), %edx
    subl    %esp, %edx
copy_stack_args:
    push    0(%esp,%edx)
    loop    copy_stack_args
skip_push_args:
    movl    8(%ebp), %edx
    call    *%edx
    leave
    ret
