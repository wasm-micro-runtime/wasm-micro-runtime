/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef STACKLESS_WASI_HTTP_H
#define STACKLESS_WASI_HTTP_H
#include "vm.h"

#ifndef HTTP_RESOURCE_SLOTS
#define HTTP_RESOURCE_SLOTS 32
#endif
#ifndef HTTP_FIELD_BYTES
#define HTTP_FIELD_BYTES 1536
#endif
#define HTTP_FIELDS_MAX 12
#define HTTP_URI_MAX 512
#ifndef HTTP_PENDING_DROPS
#define HTTP_PENDING_DROPS 16
#endif
enum http_kind { HTTP_FIELDS = 1, HTTP_REQUEST, HTTP_RESPONSE, HTTP_OPTIONS };

typedef struct http_field {
    uint16_t offset, name_size, value_size;
} http_field;
typedef struct http_fields {
    uint8_t bytes[HTTP_FIELD_BYTES];
    http_field field[HTTP_FIELDS_MAX];
    uint16_t size, count;
} http_fields;
typedef struct http_message {
    char path[HTTP_URI_MAX], authority[HTTP_URI_MAX], method_other[64],
        scheme_other[64];
    uint32_t fields, options, body, trailers, sent;
    uint16_t status;
    uint8_t method, scheme;
    bool inbound;
} http_message;
typedef struct http_options {
    uint64_t timeout[3];
    uint8_t mask;
} http_options;
typedef struct http_resource {
    union {
        http_fields fields;
        http_message message;
        http_options options;
    } as;
    uint32_t generation;
    uint8_t kind;
    bool native, immutable;
} http_resource;
typedef struct wasi_http {
    p3_vm *vm;
    const p3_export *realloc_export;
    http_resource resources[HTTP_RESOURCE_SLOTS];
    uint32_t pending[HTTP_PENDING_DROPS], pending_count, live, peak;
    bool allocating;
} wasi_http;

void
wasi_http_init(wasi_http *http, p3_vm *vm);
uint8_t
wasi_http_binding(const wasm_name_t *name, const wasm_functype_t *type);
bool
wasi_http_call(p3_vm *vm, uint8_t op, const wasm_val_vec_t *args,
               wasm_val_vec_t *results);
bool
wasi_http_sweep(wasi_http *http);
uint32_t
wasi_http_new(wasi_http *http, uint8_t kind, bool native);
http_resource *
wasi_http_get(wasi_http *http, uint32_t handle, uint8_t kind, bool native);
bool
wasi_http_drop(wasi_http *http, uint32_t handle, uint8_t kind, bool native);
bool
wasi_http_pair(wasi_http *http, bool future, uint8_t element, uint32_t *reader,
               uint32_t *writer);
int
wasi_http_append(http_fields *fields, const uint8_t *name, uint32_t names,
                 const uint8_t *value, uint32_t values);
int
wasi_http_method(const uint8_t *str, uint32_t len);
bool
wasi_http_path(const uint8_t *str, uint32_t len);
bool
wasi_http_authority(const uint8_t *str, uint32_t len);
#endif
