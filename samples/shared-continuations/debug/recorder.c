/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "recorder.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <process.h>
#endif
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
_Static_assert(sizeof(wasm_ext_debug_snapshot_t) == 528, "snapshot ABI");
_Static_assert(sizeof(debug_segment) == 676, "segment format");
_Static_assert(sizeof(debug_record) == 12264, "record format");
static debug_record fallback;
debug_record *debug_published = &fallback;
#ifndef _WIN32
static int mapping_fd = -1;
#endif
static void
publish(const debug_task *task)
{
    uint32_t bank = 1 - debug_load(&debug_published->active);
    debug_published->banks[bank] = *task;
    debug_store(&debug_published->active, bank);
}
bool
debug_open(const char *path)
{
    const uint32_t endian = 1;
    if (*(const unsigned char *)&endian != 1)
        return false; /* v1 file transport is explicitly little endian. */
#ifndef _WIN32
    if (path) {
        mapping_fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (mapping_fd < 0)
            return false;
        if (ftruncate(mapping_fd, sizeof(debug_record))) {
            close(mapping_fd);
            mapping_fd = -1;
            return false;
        }
        void *mapping = mmap(NULL, sizeof(debug_record), PROT_READ | PROT_WRITE,
                             MAP_SHARED, mapping_fd, 0);
        if (mapping == MAP_FAILED) {
            close(mapping_fd);
            mapping_fd = -1;
            return false;
        }
        debug_published = mapping;
    }
#else
    if (path)
        return false; /* debugger memory works; Windows file transport is
                         external */
#endif
    memset(debug_published, 0, sizeof(*debug_published));
    debug_published->magic = DEBUG_MAGIC;
    debug_published->version = 1;
    debug_published->size = sizeof(*debug_published);
    debug_store(&debug_published->active, 0);
#ifdef _WIN32
    debug_published->pid = (uint32_t)_getpid();
#else
    debug_published->pid = (uint32_t)getpid();
#endif
    const char *identity = getenv("WASM_DEBUG_MANIFEST_SHA256");
    if (identity) {
        if (strlen(identity) != 64)
            return false;
        for (size_t i = 0; i < 32; i++) {
            unsigned byte;
            if (sscanf(identity + 2 * i, "%2x", &byte) != 1)
                return false;
            debug_published->manifest_sha256[i] = (unsigned char)byte;
        }
    }
    return true;
}
void
debug_close(void)
{
#ifndef _WIN32
    if (mapping_fd >= 0) {
        munmap(debug_published, sizeof(*debug_published));
        close(mapping_fd);
        mapping_fd = -1;
        debug_published = &fallback;
    }
#endif
}
bool
debug_task_start(debug_task *task, uint32_t *generation,
                 const debug_task *parent)
{
    if (!task || !generation || *generation == UINT32_MAX || parent == task)
        return false;
    memset(task, 0, sizeof(*task));
    task->current.task = ++*generation;
    if (parent) {
        task->current.parent = parent->current.task;
        task->ancestry[0] = parent->current;
        task->ancestors = 1;
        for (uint32_t i = 0;
             i < parent->ancestors && task->ancestors < DEBUG_ANCESTORS; i++)
            task->ancestry[task->ancestors++] = parent->ancestry[i];
        task->truncated =
            parent->truncated || parent->ancestors == DEBUG_ANCESTORS;
    }
    return true;
}
bool
debug_task_error(debug_task *task, uint32_t generation, uint32_t cause,
                 const char *message)
{
    if (task->current.task != generation
        || task->current.event != WASM_EXT_DEBUG_PENDING)
        return false;
    task->current.cause = cause;
    int length = snprintf(task->current.error, sizeof(task->current.error),
                          "%s", message);
    task->current.error_flags =
        length < 0 || (size_t)length >= sizeof(task->current.error);
    return true;
}
bool
debug_task_cause(debug_task *task, const debug_task *cause)
{
    if (task == cause || task->current.event != WASM_EXT_DEBUG_PENDING
        || cause->current.event != WASM_EXT_DEBUG_ASYNC_ERROR)
        return false;
    task->current.cause = cause->current.task;
    task->failures[0] = cause->current;
    task->causes = 1;
    for (uint32_t i = 0; i < cause->causes && task->causes < DEBUG_ANCESTORS;
         i++)
        task->failures[task->causes++] = cause->failures[i];
    task->causes_truncated =
        cause->causes_truncated || cause->causes == DEBUG_ANCESTORS;
    return true;
}
void
debug_observe(void *env, uint32_t event,
              const wasm_ext_debug_snapshot_t *snapshot)
{
    debug_task *task = env;
    if (snapshot->version != 1 || snapshot->size != sizeof(*snapshot)
        || snapshot->count > WASM_EXT_DEBUG_FRAMES)
        return;
    task->current.guest = *snapshot;
    task->current.event = event;
    publish(task);
}
