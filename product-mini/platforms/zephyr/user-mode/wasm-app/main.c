/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
    char *buf;

    printf("Hello world!\n");

    buf = malloc(16);
    if (!buf) {
        printf("ERROR: malloc buf failed\n");
        return -1;
    }

    printf("buf ptr: %p\n", buf);

    (void)snprintf(buf, 16, "%s", EXPECTED);
    printf("buf: %s", buf);

    free(buf);
    return 0;
}
