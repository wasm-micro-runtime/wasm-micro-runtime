/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "wasi_http.h"
#include <stdio.h>
#include <stdlib.h>

static p3_vm vm;
static wasi_http http;

int
main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END))
        return 2;
    long size = ftell(file);
    if (size <= 0 || fseek(file, 0, SEEK_SET))
        return 2;
    uint8_t *bytes = malloc((size_t)size);
    if (!bytes || fread(bytes, 1, (size_t)size, file) != (size_t)size)
        return 2;
    fclose(file);
    bool ok = p3_vm_open(&vm, bytes, (size_t)size);
    free(bytes);
    if (ok) {
        wasi_http_init(&http, &vm);
        p3_task task;
        p3_task_init(&task, &vm, "command-event");
        ok = p3_task_start(&task, "command", NULL, 0);
        for (unsigned i = 0; ok && i < 10000 && !task.exited; ++i) {
            ok = wasi_http_sweep(&http) && p3_task_ready(&task)
                 && p3_task_step(&task);
        }
        ok = ok && wasi_http_sweep(&http) && task.exited && task.returned
             && !task.http_result && !http.live && !vm.async.live;
    }
    if (ok)
        printf("PASS %s resources=%u handles=%u\n", argv[1], http.peak,
               vm.async.peak);
    else
        fprintf(stderr, "FAIL %s: %s %s resources=%u handles=%u\n", argv[1],
                vm.error, vm.async.error ? vm.async.error : "", http.live,
                vm.async.live);
    p3_vm_close(&vm);
    return ok ? 0 : 1;
}
