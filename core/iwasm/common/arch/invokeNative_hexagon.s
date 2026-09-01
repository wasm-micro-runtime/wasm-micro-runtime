/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

        .text
        .align 2
        .globl invokeNative
        .type invokeNative, @function

/*
 * void invokeNative(void (*native_code)(), uint32 argv[], uint32 argc)
 *
 * r0 = native_code, r1 = argv, r2 = argc
 * Loads up to 6 words into r0-r5; remaining args are pushed to stack.
 */

invokeNative:
        {
            allocframe(#16)
        }
        {
            memd(r29+#0) = r17:16
        }
        {
            memd(r29+#8) = r19:18
        }

        // Save arguments to callee-saved registers.
        // Also clear r0 so that if argc == 0 we don't call the native
        // function with r0 still holding the function pointer.  Reads
        // happen before writes within a Hexagon packet, so r16 receives
        // the original r0.
        {
            r16 = r0                    // r16 = function_ptr
            r17 = r1                    // r17 = argv
            r0 = #0
        }
        {
            r18 = r2                    // r18 = argc
            p0 = cmp.gt(r2, #0)
        }
        {
            if (!p0) jump .Lcall        // argc == 0: skip arg loading
        }

        // Load register arguments from argv (up to 6 words in r0-r5)
        {
            r0 = memw(r17+#0)          // r0 = argv[0] (exec_env)
            p0 = cmp.gt(r18, #1)
        }
        { if (!p0) jump .Lcall }

        {
            r1 = memw(r17+#4)          // r1 = argv[1]
            p0 = cmp.gt(r18, #2)
        }
        { if (!p0) jump .Lcall }

        {
            r2 = memw(r17+#8)          // r2 = argv[2]
            p0 = cmp.gt(r18, #3)
        }
        { if (!p0) jump .Lcall }

        {
            r3 = memw(r17+#12)         // r3 = argv[3]
            p0 = cmp.gt(r18, #4)
        }
        { if (!p0) jump .Lcall }

        {
            r4 = memw(r17+#16)         // r4 = argv[4]
            p0 = cmp.gt(r18, #5)
        }
        { if (!p0) jump .Lcall }

        {
            r5 = memw(r17+#20)         // r5 = argv[5]
            p0 = cmp.gt(r18, #6)
        }
        { if (!p0) jump .Lcall }

        // Stack arguments: argc > 6.
        // Copy argv[6..argc-1] to the stack, maintaining 8-byte alignment.
        {
            r19 = add(r18, #-6)         // r19 = number of stack args
        }
        {
            r7 = asl(r19, #2)           // r7 = stack_args * 4 (bytes)
        }
        {
            r7 = add(r7, #7)            // round up to 8-byte alignment
        }
        {
            r7 = and(r7, #-8)
        }
        {
            r29 = sub(r29, r7)          // allocate aligned stack space
        }
        {
            r6 = add(r17, #24)          // r6 = &argv[6] (source)
            r8 = r29                    // r8 = stack destination
        }

.Lcopy_loop:
        {
            r9 = memw(r6++#4)           // load next arg from argv
        }
        {
            memw(r8++#4) = r9            // store to stack
            r19 = add(r19, #-1)         // decrement counter
        }
        {
            p0 = cmp.gt(r19, #0)
            if (p0.new) jump:t .Lcopy_loop
        }

.Lcall:
        {
            callr r16                   // call native function
        }

.Lreturn:
        {
            r17:16 = memd(r30+#-16)
        }
        {
            r19:18 = memd(r30+#-8)
        }
        {
            deallocframe
        }
        {
            jumpr r31
        }

        .size invokeNative, .-invokeNative

#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
