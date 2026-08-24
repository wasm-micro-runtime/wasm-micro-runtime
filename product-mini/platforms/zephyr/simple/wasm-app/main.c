/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdio.h>
#include <stdlib.h>

/* Exit codes, reported to the host as the return value of main */
#define EXIT_OK 0
#define EXIT_MALLOC 1
#define EXIT_MISMATCH 2

static const char EXPECTED[] = "1234\n";

int
main(int argc, char **argv)
{
    char *buf;
    int i;

    printf("Hello world!\n");

    buf = malloc(16);
    if (!buf) {
        printf("ERROR: malloc buf failed\n");
        return EXIT_MALLOC;
    }

    printf("buf ptr: %p\n", buf);

    (void)snprintf(buf, 16, "%s", EXPECTED);
    printf("buf: %s", buf);

    for (i = 0; EXPECTED[i] != '\0'; i++) {
        if (buf[i] != EXPECTED[i]) {
            printf("ERROR: buf differs from the expected content\n");
            free(buf);
            return EXIT_MISMATCH;
        }
    }

    free(buf);
    return EXIT_OK;
}
