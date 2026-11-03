/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int
main(int argc, char **argv)
{
    if (argc < 4 || argc > 8)
        return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END))
        return 2;
    long length = ftell(file);
    if (length < 0 || length > 1024 * 1024 || fseek(file, 0, SEEK_SET))
        return 2;
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length)
        return 2;
    fclose(file);
    p3_vm vm;
    if (!p3_vm_open(&vm, bytes, (size_t)length)) {
        fprintf(stderr, "instantiate: %s\n", vm.error);
        return 1;
    }
    free(bytes);
    int32_t value = 0;
    bool result = false;
    int32_t args[4];
    for (int i = 4; i < argc; ++i)
        args[i - 4] = (int32_t)strtol(argv[i], NULL, 0);
    bool ok =
        p3_vm_call(&vm, argv[2], args, (size_t)(argc - 4), &value, &result);
    bool expected_trap = !strncmp(argv[3], "trap:", 5);
    if (expected_trap)
        ok = !ok && strstr(vm.error, argv[3] + 5) != NULL;
    else
        ok = ok
             && ((!strcmp(argv[3], "void") && !result)
                 || (result && value == (int32_t)strtol(argv[3], NULL, 0)));
    printf("%s %s result=%d live=%u peak=%u adapter_bytes=%zu transferred=%llu "
           "error=%s\n",
           ok ? "PASS" : "FAIL", argv[2], value, vm.async.live, vm.async.peak,
           sizeof(vm.async), (unsigned long long)vm.async.bytes, vm.error);
    p3_vm_close(&vm);
    return ok ? 0 : 1;
}
