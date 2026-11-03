/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "wasi_http.h"
#include <string.h>

enum {
    RETURN_HANDLE = 1,
    RETURN_RUN,
    DROP_FIELDS,
    DROP_REQUEST,
    DROP_RESPONSE,
    DROP_OPTIONS,
    FIELDS_NEW,
    FIELDS_CLONE,
    FIELDS_FROM_LIST,
    FIELDS_GET,
    FIELDS_HAS,
    FIELDS_SET,
    FIELDS_DELETE,
    FIELDS_TAKE,
    FIELDS_APPEND,
    FIELDS_COPY,
    REQUEST_NEW,
    REQUEST_CONSUME,
    REQUEST_HEADERS,
    METHOD_GET,
    METHOD_SET,
    PATH_GET,
    PATH_SET,
    SCHEME_GET,
    SCHEME_SET,
    AUTHORITY_GET,
    AUTHORITY_SET,
    OPTIONS_GET,
    RESPONSE_NEW,
    STATUS_GET,
    STATUS_SET,
    RESPONSE_HEADERS,
    OPTIONS_NEW,
    OPTIONS_CLONE,
    CONNECT_GET,
    FIRST_GET,
    BETWEEN_GET,
    CONNECT_SET,
    FIRST_SET,
    BETWEEN_SET
};
enum { INVALID_SYNTAX, FORBIDDEN, IMMUTABLE, SIZE_EXCEEDED, HEADER_OK = -1 };
static const struct {
    const char *name;
    uint8_t argc, result;
} bindings[] = {
    [RETURN_HANDLE] = { "[return]handle", 8, 0 },
    [RETURN_RUN] = { "[return]run", 1, 0 },
    [DROP_FIELDS] = { "[drop]fields", 1, 0 },
    [DROP_REQUEST] = { "[drop]request", 1, 0 },
    [DROP_RESPONSE] = { "[drop]response", 1, 0 },
    [DROP_OPTIONS] = { "[drop]request-options", 1, 0 },
    [FIELDS_NEW] = { "[constructor]fields", 0, 1 },
    [FIELDS_CLONE] = { "[method]fields.clone", 1, 1 },
    [FIELDS_FROM_LIST] = { "[static]fields.from-list", 3, 0 },
    [FIELDS_GET] = { "[method]fields.get", 4, 0 },
    [FIELDS_HAS] = { "[method]fields.has", 3, 1 },
    [FIELDS_SET] = { "[method]fields.set", 6, 0 },
    [FIELDS_DELETE] = { "[method]fields.delete", 4, 0 },
    [FIELDS_TAKE] = { "[method]fields.get-and-delete", 4, 0 },
    [FIELDS_APPEND] = { "[method]fields.append", 6, 0 },
    [FIELDS_COPY] = { "[method]fields.copy-all", 2, 0 },
    [REQUEST_NEW] = { "[static]request.new", 7, 0 },
    [REQUEST_CONSUME] = { "[static]request.consume-body", 3, 0 },
    [REQUEST_HEADERS] = { "[method]request.get-headers", 1, 1 },
    [METHOD_GET] = { "[method]request.get-method", 2, 0 },
    [METHOD_SET] = { "[method]request.set-method", 4, 1 },
    [PATH_GET] = { "[method]request.get-path-with-query", 2, 0 },
    [PATH_SET] = { "[method]request.set-path-with-query", 4, 1 },
    [SCHEME_GET] = { "[method]request.get-scheme", 2, 0 },
    [SCHEME_SET] = { "[method]request.set-scheme", 5, 1 },
    [AUTHORITY_GET] = { "[method]request.get-authority", 2, 0 },
    [AUTHORITY_SET] = { "[method]request.set-authority", 4, 1 },
    [OPTIONS_GET] = { "[method]request.get-options", 2, 0 },
    [RESPONSE_NEW] = { "[static]response.new", 5, 0 },
    [STATUS_GET] = { "[method]response.get-status-code", 1, 1 },
    [STATUS_SET] = { "[method]response.set-status-code", 2, 1 },
    [RESPONSE_HEADERS] = { "[method]response.get-headers", 1, 1 },
    [OPTIONS_NEW] = { "[constructor]request-options", 0, 1 },
    [OPTIONS_CLONE] = { "[method]request-options.clone", 1, 1 },
    [CONNECT_GET] = { "[method]request-options.get-connect-timeout", 2, 0 },
    [FIRST_GET] = { "[method]request-options.get-first-byte-timeout", 2, 0 },
    [BETWEEN_GET] = { "[method]request-options.get-between-bytes-timeout", 2,
                      0 },
    [CONNECT_SET] = { "[method]request-options.set-connect-timeout", 4, 0 },
    [FIRST_SET] = { "[method]request-options.set-first-byte-timeout", 4, 0 },
    [BETWEEN_SET] = { "[method]request-options.set-between-bytes-timeout", 4,
                      0 },
};

static bool
fail(wasi_http *http, const char *reason)
{
    http->vm->async.error = reason;
    return false;
}
static bool
wide_arg(unsigned op, size_t i)
{
    return (op >= CONNECT_SET && i == 2) || (op == RETURN_HANDLE && i == 3);
}
uint8_t
wasi_http_binding(const wasm_name_t *name, const wasm_functype_t *type)
{
    const wasm_valtype_vec_t *params = wasm_functype_params(type);
    const wasm_valtype_vec_t *results = wasm_functype_results(type);
    for (unsigned i = 1; i < sizeof(bindings) / sizeof(*bindings); ++i) {
        size_t len = strlen(bindings[i].name);
        if ((name->size != len && (name->size != len + 1 || name->data[len]))
            || memcmp(name->data, bindings[i].name, len))
            continue;
        if (params->size != bindings[i].argc
            || results->size != bindings[i].result
            || (results->size
                && wasm_valtype_kind(results->data[0]) != WASM_I32))
            return 0;
        for (size_t p = 0; p < params->size; ++p)
            if (wasm_valtype_kind(params->data[p])
                != (wide_arg(i, p) ? WASM_I64 : WASM_I32))
                return 0;
        return (uint8_t)i;
    }
    return 0;
}
void
wasi_http_init(wasi_http *http, p3_vm *vm)
{
    memset(http, 0, sizeof(*http));
    http->vm = vm;
    http->realloc_export = p3_vm_export(vm, "cabi_realloc");
    vm->http = http;
}
_Static_assert(HTTP_RESOURCE_SLOTS <= 255 && HTTP_FIELD_BYTES <= UINT16_MAX,
               "HTTP resource index widths");
uint32_t
wasi_http_new(wasi_http *http, uint8_t kind, bool native)
{
    for (unsigned i = 0; i < HTTP_RESOURCE_SLOTS; ++i) {
        http_resource *r = &http->resources[i];
        if (r->kind || r->generation == 0xffffff)
            continue;
        memset(&r->as, 0, sizeof(r->as));
        r->kind = kind;
        r->native = native;
        r->immutable = false;
        if (++http->live > http->peak)
            http->peak = http->live;
        return (++r->generation << 8) | (i + 1);
    }
    fail(http, "HTTP resource table exhausted");
    return 0;
}
http_resource *
wasi_http_get(wasi_http *http, uint32_t handle, uint8_t kind, bool native)
{
    unsigned i = (handle & 255U) - 1U;
    if (i < HTTP_RESOURCE_SLOTS) {
        http_resource *r = &http->resources[i];
        if (r->kind == kind && r->native == native
            && r->generation == handle >> 8)
            return r;
    }
    fail(http, "invalid HTTP resource handle or ownership");
    return NULL;
}
bool
wasi_http_pair(wasi_http *http, bool future, uint8_t element, uint32_t *reader,
               uint32_t *writer)
{
    uint64_t packed;
    if (!p3_call(&http->vm->async, P3_NEW, future, element, true, 0, 0, 0,
                 &packed))
        return false;
    *reader = (uint32_t)packed;
    *writer = (uint32_t)(packed >> 32);
    return true;
}
static bool
defer_future(wasi_http *http, uint32_t end)
{
    if (!end)
        return true;
    if (http->pending_count == HTTP_PENDING_DROPS)
        return fail(http, "HTTP future disposal capacity exhausted");
    http->pending[http->pending_count++] = end;
    return true;
}
bool
wasi_http_sweep(wasi_http *http)
{
    size_t i = 0;
    while (i < http->pending_count) {
        uint8_t value[P3_HTTP_RESULT_SIZE] = { 0 };
        bool complete;
        if (!p3_http_finish(&http->vm->async, http->pending[i], value,
                            &complete))
            return false;
        if (complete)
            http->pending[i] = http->pending[--http->pending_count];
        else
            ++i;
    }
    return true;
}
bool
wasi_http_drop(wasi_http *http, uint32_t handle, uint8_t kind, bool native)
{
    http_resource *r = wasi_http_get(http, handle, kind, native);
    if (!r)
        return false;
    if (kind == HTTP_REQUEST || kind == HTTP_RESPONSE) {
        http_message *m = &r->as.message;
        if ((m->fields && !wasi_http_drop(http, m->fields, HTTP_FIELDS, true))
            || (m->options
                && !wasi_http_drop(http, m->options, HTTP_OPTIONS, true))
            || (m->body && !p3_native_drop(&http->vm->async, m->body))
            || (m->trailers && !p3_native_drop(&http->vm->async, m->trailers))
            || !defer_future(http, m->sent))
            return false;
    }
    r->kind = 0;
    --http->live;
    return true;
}
static uint32_t
clone(wasi_http *http, const http_resource *r, bool immutable)
{
    uint32_t handle = wasi_http_new(http, r->kind, false);
    if (handle) {
        http_resource *copy = wasi_http_get(http, handle, r->kind, false);
        copy->as = r->as;
        copy->immutable = immutable;
    }
    return handle;
}
static uint8_t *
memory(wasi_http *http, uint32_t ptr, uint32_t len, unsigned align)
{
    p3_context *cx = &http->vm->async;
    size_t size;
    uint8_t *base = cx->memory(cx->memory_env, &size);
    if (!base || (ptr & (align - 1)) || ptr > size || len > size - ptr) {
        fail(http, "invalid HTTP canonical memory range");
        return NULL;
    }
    return base + ptr;
}
static uint32_t
read32(const uint8_t *src)
{
    uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i)
        n |= (uint32_t)src[i] << (i * 8);
    return n;
}
static void
write32(uint8_t *dst, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i)
        dst[i] = (uint8_t)(n >> (i * 8));
}
static bool
alloc(wasi_http *http, const uint8_t *bytes, uint32_t len, unsigned align,
      uint32_t *offset)
{
    *offset = align;
    if (!len)
        return true;
    int32_t args[] = { 0, 0, (int32_t)align, (int32_t)len }, result;
    bool has_result;
    http->allocating = true;
    bool ok = p3_vm_invoke(http->vm, http->realloc_export, args, 4, &result,
                           &has_result);
    http->allocating = false;
    if (!ok || !has_result || !result)
        return fail(http, "guest cabi_realloc failed");
    *offset = (uint32_t)result;
    uint8_t *dst = memory(http, *offset, len, align);
    if (!dst)
        return false;
    if (bytes)
        memcpy(dst, bytes, len);
    return true;
}
static unsigned
lower(unsigned ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}
static bool
alpha(unsigned ch)
{
    return lower(ch) >= 'a' && lower(ch) <= 'z';
}
static bool
digit(unsigned ch)
{
    return ch >= '0' && ch <= '9';
}
static bool
punct(unsigned ch, const char *set)
{
    return ch && ch < 128 && strchr(set, (int)ch) != NULL;
}
static bool
token(const uint8_t *s, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i)
        if (!alpha(s[i]) && !digit(s[i]) && !punct(s[i], "!#$%&'*+-.^_`|~"))
            return false;
    return len != 0;
}
static bool
same_name(const uint8_t *a, size_t na, const uint8_t *b, size_t nb)
{
    if (na != nb)
        return false;
    for (size_t i = 0; i < na; ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}
static int
validate_name(const uint8_t *name, uint32_t size)
{
    static const char *const forbidden[] = { "connection",
                                             "keep-alive",
                                             "proxy-authenticate",
                                             "proxy-authorization",
                                             "proxy-connection",
                                             "transfer-encoding",
                                             "upgrade",
                                             "http2-settings",
                                             "host" };
    if (!token(name, size))
        return INVALID_SYNTAX;
    for (unsigned i = 0; i < sizeof(forbidden) / sizeof(*forbidden); ++i)
        if (same_name(name, size, (const uint8_t *)forbidden[i],
                      strlen(forbidden[i])))
            return FORBIDDEN;
    return HEADER_OK;
}
int
wasi_http_append(http_fields *f, const uint8_t *name, uint32_t names,
                 const uint8_t *value, uint32_t values)
{
    int error = validate_name(name, names);
    if (error != HEADER_OK)
        return error;
    if (f->count == HTTP_FIELDS_MAX
        || names > (uint32_t)HTTP_FIELD_BYTES - f->size
        || values > (uint32_t)HTTP_FIELD_BYTES - f->size - names)
        return SIZE_EXCEEDED;
    for (uint32_t i = 0; i < values; ++i)
        if ((value[i] < 32 && value[i] != '\t') || value[i] == 127)
            return INVALID_SYNTAX;
    f->field[f->count++] =
        (http_field){ f->size, (uint16_t)names, (uint16_t)values };
    for (uint32_t i = 0; i < names; ++i)
        f->bytes[f->size + i] = (uint8_t)lower(name[i]);
    memcpy(f->bytes + f->size + names, value, values);
    f->size += (uint16_t)(names + values);
    return HEADER_OK;
}
static void
delete_fields(http_fields *f, unsigned mask)
{
    unsigned count = 0, length = 0;
    for (unsigned i = 0; i < f->count; ++i) {
        http_field h = f->field[i];
        if (mask & (1U << i))
            continue;
        unsigned size = h.name_size + h.value_size;
        memmove(f->bytes + length, f->bytes + h.offset, size);
        h.offset = (uint16_t)length;
        f->field[count++] = h;
        length += size;
    }
    f->count = (uint16_t)count;
    f->size = (uint16_t)length;
}
static bool
output_fields(wasi_http *http, const http_fields *f, const uint8_t *selected,
              unsigned count, bool all, uint32_t out)
{
    uint32_t list, width = all ? 16 : 8;
    if (!alloc(http, NULL, count * width, 4, &list))
        return false;
    for (unsigned i = 0; i < count; ++i) {
        http_field h = f->field[selected[i]];
        const uint8_t *name = f->bytes + h.offset;
        uint32_t key = 0, value;
        if ((all && !alloc(http, name, h.name_size, 1, &key))
            || !alloc(http, name + h.name_size, h.value_size, 1, &value))
            return false;
        uint8_t *dst = memory(http, list + i * width, width, 4);
        if (!dst)
            return false;
        if (all) {
            write32(dst, key);
            write32(dst + 4, h.name_size);
            dst += 8;
        }
        write32(dst, value);
        write32(dst + 4, h.value_size);
    }
    uint8_t *ret = memory(http, out, 8, 4);
    if (!ret)
        return false;
    write32(ret, list);
    write32(ret + 4, count);
    return true;
}
static bool
header_result(wasi_http *http, uint32_t ptr, int error)
{
    uint8_t *out = memory(http, ptr, 16, 4);
    if (!out)
        return false;
    memset(out, 0, 16);
    if (error != HEADER_OK) {
        out[0] = 1;
        out[4] = (uint8_t)error;
    }
    return true;
}
static bool
field_import(wasi_http *http, unsigned op, const uint32_t *a, uint32_t *ret)
{
    if (op == FIELDS_NEW)
        return (*ret = wasi_http_new(http, HTTP_FIELDS, false)) != 0;
    if (op == FIELDS_FROM_LIST) {
        http_fields fields = { 0 };
        int error = HEADER_OK;
        if (a[1] > HTTP_FIELDS_MAX)
            error = SIZE_EXCEEDED;
        for (uint32_t i = 0; i < a[1] && error == HEADER_OK; ++i) {
            uint8_t *item = memory(http, a[0], a[1] * 16, 4);
            if (!item)
                return false;
            item += i * 16;
            uint32_t ns = read32(item + 4), vs = read32(item + 12);
            const uint8_t *name = memory(http, read32(item), ns, 1);
            const uint8_t *value = memory(http, read32(item + 8), vs, 1);
            if (!name || !value)
                return false;
            error = wasi_http_append(&fields, name, ns, value, vs);
        }
        if (!header_result(http, a[2], error))
            return false;
        if (error == HEADER_OK) {
            uint32_t h = wasi_http_new(http, HTTP_FIELDS, false);
            if (!h)
                return false;
            wasi_http_get(http, h, HTTP_FIELDS, false)->as.fields = fields;
            write32(memory(http, a[2], 16, 4) + 4, h);
        }
        return true;
    }
    http_resource *r = wasi_http_get(http, a[0], HTTP_FIELDS, false);
    if (!r)
        return false;
    if (op == FIELDS_CLONE)
        return (*ret = clone(http, r, false)) != 0;
    http_fields *fields = &r->as.fields;
    const uint8_t *name =
        op == FIELDS_COPY ? NULL : memory(http, a[1], a[2], 1);
    if (!name && op != FIELDS_COPY)
        return false;
    uint8_t selected[HTTP_FIELDS_MAX];
    unsigned count = 0, mask = 0;
    for (unsigned i = 0; i < fields->count; ++i) {
        http_field f = fields->field[i];
        if (!name
            || same_name(fields->bytes + f.offset, f.name_size, name, a[2])) {
            selected[count++] = (uint8_t)i;
            mask |= 1U << i;
        }
    }
    if (op == FIELDS_HAS) {
        *ret = count != 0;
        return true;
    }
    if (op == FIELDS_GET || op == FIELDS_COPY)
        return output_fields(http, fields, selected, count, op == FIELDS_COPY,
                             a[op == FIELDS_COPY ? 1 : 3]);
    uint32_t out = a[bindings[op].argc - 1];
    int error = r->immutable ? IMMUTABLE : validate_name(name, a[2]);
    if (error != HEADER_OK)
        return header_result(http, out, error);
    if (op == FIELDS_APPEND) {
        const uint8_t *value = memory(http, a[3], a[4], 1);
        return value
               && header_result(
                   http, out,
                   wasi_http_append(fields, name, a[2], value, a[4]));
    }
    if (op == FIELDS_SET) {
        http_fields copy = *fields;
        delete_fields(&copy, mask);
        if (a[4] > HTTP_FIELDS_MAX)
            error = SIZE_EXCEEDED;
        for (uint32_t i = 0; i < a[4] && error == HEADER_OK; ++i) {
            uint8_t *list = memory(http, a[3], a[4] * 8, 4);
            if (!list)
                return false;
            uint32_t size = read32(list + i * 8 + 4);
            const uint8_t *value = memory(http, read32(list + i * 8), size, 1);
            if (!value)
                return false;
            error = wasi_http_append(&copy, name, a[2], value, size);
        }
        if (error == HEADER_OK)
            *fields = copy;
        return header_result(http, out, error);
    }
    if (!header_result(http, out, HEADER_OK))
        return false;
    if (op == FIELDS_TAKE
        && !output_fields(http, fields, selected, count, false, out + 4))
        return false;
    delete_fields(fields, mask);
    return true;
}

static const char *const methods[] = { "GET",     "HEAD",   "POST",
                                       "PUT",     "DELETE", "CONNECT",
                                       "OPTIONS", "TRACE",  "PATCH" };
int
wasi_http_method(const uint8_t *str, uint32_t len)
{
    if (len >= sizeof(((http_message *)0)->method_other) || !token(str, len))
        return -1;
    for (unsigned i = 0; i < sizeof(methods) / sizeof(*methods); ++i)
        if (strlen(methods[i]) == len && !memcmp(methods[i], str, len))
            return (int)i;
    return 9;
}
static bool
utf8(const uint8_t *str, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i) {
        unsigned lead = str[i];
        if (lead < 0x80)
            continue;
        if (lead < 0xc2 || lead > 0xf4)
            return false;
        unsigned tail = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : 1;
        if (tail >= len - i)
            return false;
        unsigned next = str[i + 1];
        if ((lead == 0xe0 && next < 0xa0) || (lead == 0xed && next >= 0xa0)
            || (lead == 0xf0 && next < 0x90) || (lead == 0xf4 && next >= 0x90))
            return false;
        while (tail--)
            if ((str[++i] & 0xc0) != 0x80)
                return false;
    }
    return true;
}
bool
wasi_http_path(const uint8_t *str, uint32_t len)
{
    if (len >= HTTP_URI_MAX || !utf8(str, len)
        || (len && str[0] != '/' && str[0] != '?'
            && (len != 1 || str[0] != '*')))
        return false;
    for (uint32_t i = 0; i < len; ++i)
        if (!(str[i] >= 128 || alpha(str[i]) || digit(str[i])
              || punct(str[i], "-._~%!$&'()*+,;=:@/?")))
            return false;
    return true;
}
bool
wasi_http_authority(const uint8_t *str, uint32_t len)
{
    if (!len || len >= HTTP_URI_MAX)
        return false;
    uint32_t host = 0;
    for (uint32_t i = 0; i < len; ++i)
        if (str[i] == '@') {
            if (host || !i || str[0] == ':')
                return false;
            host = i + 1;
        }
        else if (!alpha(str[i]) && !digit(str[i])
                 && !punct(str[i], "-._~%!$&'()*+,;=:"))
            return false;
    if (host == len || str[host] == ':')
        return false;
    bool port = false;
    for (uint32_t i = host; i < len; ++i) {
        if (port && !digit(str[i]))
            return false;
        if (str[i] == ':') {
            if (i + 1 == len)
                return false;
            port = true;
        }
    }
    return true;
}
static bool
string_result(wasi_http *http, uint32_t ptr, uint32_t tag, const char *str,
              unsigned header)
{
    uint32_t len = (uint32_t)strlen(str), offset;
    if (!alloc(http, (const uint8_t *)str, len, 1, &offset))
        return false;
    uint8_t *out = memory(http, ptr, header + 8, 4);
    if (!out)
        return false;
    write32(out, tag);
    if (header == 8)
        write32(out + 4, tag ? tag - 1 : 0);
    write32(out + header, offset);
    write32(out + header + 4, len);
    if (header == 8)
        write32(out, tag != 0);
    return true;
}
static void
copy_text(char *dst, const uint8_t *src, size_t len)
{
    if (len)
        memcpy(dst, src, len);
    dst[len] = 0;
}
static bool
options_import(wasi_http *http, unsigned op, const uint32_t *a,
               uint64_t timeout, uint32_t *ret)
{
    if (op == OPTIONS_NEW)
        return (*ret = wasi_http_new(http, HTTP_OPTIONS, false)) != 0;
    http_resource *r = wasi_http_get(http, a[0], HTTP_OPTIONS, false);
    if (!r)
        return false;
    if (op == OPTIONS_CLONE)
        return (*ret = clone(http, r, false)) != 0;
    bool set = op >= CONNECT_SET;
    unsigned index = op - (set ? CONNECT_SET : CONNECT_GET);
    uint8_t mask = (uint8_t)(1U << index);
    uint8_t *out = memory(http, a[set ? 3 : 1], 16, set ? 4 : 8);
    if (!out)
        return false;
    if (set) {
        out[0] = r->immutable;
        out[4] = 1; /* request-options-error.immutable */
        if (!r->immutable) {
            r->as.options.mask =
                (uint8_t)((r->as.options.mask & ~mask) | (a[1] ? mask : 0));
            r->as.options.timeout[index] = timeout;
        }
    }
    else {
        out[0] = (r->as.options.mask & mask) != 0;
        for (unsigned i = 0; i < 8; ++i)
            out[8 + i] = (uint8_t)(r->as.options.timeout[index] >> (8 * i));
    }
    return true;
}
static bool
message_new(wasi_http *http, bool request, const uint32_t *a)
{
    p3_context *cx = &http->vm->async;
    http_resource *headers = wasi_http_get(http, a[0], HTTP_FIELDS, false);
    http_resource *options = NULL;
    if (request && a[4])
        options = wasi_http_get(http, a[5], HTTP_OPTIONS, false);
    uint8_t *out = memory(http, a[request ? 6 : 4], 8, 4);
    if (!headers || (request && a[4] && !options) || !out || a[1] > 1
        || (request && a[4] > 1))
        return false;
    uint32_t h =
        wasi_http_new(http, request ? HTTP_REQUEST : HTTP_RESPONSE, false);
    if (!h)
        return false;
    http_message *m =
        &wasi_http_get(http, h, request ? HTTP_REQUEST : HTTP_RESPONSE, false)
             ->as.message;
    uint32_t sent;
    if (!p3_http_lift(cx, a[3], true, P3_HTTP_TRAILERS)
        || (a[1] && !p3_http_lift(cx, a[2], false, 1))
        || !wasi_http_pair(http, true, P3_HTTP_STATUS, &sent, &m->sent)
        || !p3_host_end(cx, m->sent))
        return false;
    headers->native = true;
    if (options)
        options->native = true;
    m->fields = a[0];
    m->options = options ? a[5] : 0;
    m->body = a[1] ? a[2] : 0;
    m->trailers = a[3];
    m->status = 200;
    write32(out, h);
    write32(out + 4, sent);
    return true;
}
static bool
message_import(wasi_http *http, unsigned op, const uint32_t *a, uint32_t *ret)
{
    if (op == REQUEST_NEW || op == RESPONSE_NEW)
        return message_new(http, op == REQUEST_NEW, a);
    bool response = op >= STATUS_GET;
    uint8_t kind = response ? HTTP_RESPONSE : HTTP_REQUEST;
    http_resource *r = wasi_http_get(http, a[0], kind, false);
    if (!r)
        return false;
    http_message *m = &r->as.message;
    switch (op) {
        case REQUEST_CONSUME:
        {
            if (!m->inbound)
                return fail(http, "consume-body of an unsent request is "
                                  "outside this transport");
            uint8_t *out = memory(http, a[2], 8, 4);
            if (!out
                || !p3_http_lift(&http->vm->async, a[1], true, P3_HTTP_STATUS)
                || !defer_future(http, a[1])
                || !p3_http_lower(&http->vm->async, m->body)
                || !p3_http_lower(&http->vm->async, m->trailers))
                return false;
            write32(out, m->body);
            write32(out + 4, m->trailers);
            m->body = m->trailers = 0;
            return wasi_http_drop(http, a[0], kind, false);
        }
        case REQUEST_HEADERS:
        case RESPONSE_HEADERS:
        {
            http_resource *headers =
                wasi_http_get(http, m->fields, HTTP_FIELDS, true);
            return headers && (*ret = clone(http, headers, true)) != 0;
        }
        case OPTIONS_GET:
        {
            uint8_t *out = memory(http, a[1], 8, 4);
            if (!out)
                return false;
            write32(out, m->options != 0);
            if (m->options) {
                http_resource *options =
                    wasi_http_get(http, m->options, HTTP_OPTIONS, true);
                uint32_t copy = options ? clone(http, options, true) : 0;
                if (!copy)
                    return false;
                write32(out + 4, copy);
            }
            return true;
        }
        case STATUS_GET:
            *ret = m->status;
            return true;
        case STATUS_SET:
            *ret = (uint16_t)a[1] < 100 || (uint16_t)a[1] > 599;
            if (!*ret)
                m->status = (uint16_t)a[1];
            return true;
        case METHOD_GET:
            return string_result(http, a[1], m->method, m->method_other, 4);
        case SCHEME_GET:
            return string_result(http, a[1], m->scheme, m->scheme_other, 8);
        case PATH_GET:
            return string_result(http, a[1], m->path[0] != 0, m->path, 4);
        case AUTHORITY_GET:
            return string_result(http, a[1], m->authority[0] != 0, m->authority,
                                 4);
        case METHOD_SET:
        {
            if (a[1] > 9)
                return fail(http, "invalid HTTP method discriminant");
            const uint8_t *text =
                a[1] == 9 ? memory(http, a[2], a[3], 1) : NULL;
            if (a[1] == 9 && !text)
                return false;
            int method = text ? wasi_http_method(text, a[3]) : (int)a[1];
            *ret = method < 0;
            if (!*ret) {
                m->method = (uint8_t)method;
                copy_text(m->method_other, text, method == 9 ? a[3] : 0);
            }
            return true;
        }
        case SCHEME_SET:
        {
            if (a[1] > 1 || (a[1] && a[2] > 2))
                return fail(http, "invalid HTTP scheme discriminant");
            unsigned scheme = a[1] ? a[2] + 1 : 0;
            const uint8_t *text =
                scheme == 3 ? memory(http, a[3], a[4], 1) : NULL;
            if (scheme == 3 && !text)
                return false;
            if (text) {
                *ret =
                    !a[4] || a[4] >= sizeof(m->scheme_other) || !alpha(text[0]);
                for (unsigned i = 1; !*ret && i < a[4]; ++i)
                    *ret = !alpha(text[i]) && !digit(text[i])
                           && !punct(text[i], "+-.");
                if (*ret)
                    return true;
                if (a[4] == 4 && !memcmp(text, "http", 4))
                    scheme = 1;
                if (a[4] == 5 && !memcmp(text, "https", 5))
                    scheme = 2;
            }
            m->scheme = (uint8_t)scheme;
            copy_text(m->scheme_other, text, scheme == 3 ? a[4] : 0);
            return true;
        }
        case PATH_SET:
        case AUTHORITY_SET:
        {
            if (a[1] > 1)
                return fail(http, "invalid HTTP string option");
            bool path = op == PATH_SET;
            char *dst = path ? m->path : m->authority;
            if (!a[1]) {
                dst[0] = 0;
                return true;
            }
            const uint8_t *text = memory(http, a[2], a[3], 1);
            if (!text)
                return false;
            *ret = !(path ? wasi_http_path(text, a[3])
                          : wasi_http_authority(text, a[3]));
            if (!*ret)
                copy_text(dst, a[3] || !path ? text : (const uint8_t *)"/",
                          a[3] || !path ? a[3] : 1);
            return true;
        }
        default:
            return false;
    }
}
bool
wasi_http_call(p3_vm *vm, uint8_t op, const wasm_val_vec_t *args,
               wasm_val_vec_t *results)
{
    wasi_http *http = vm->http;
    if (!http)
        return false;
    if (http->allocating)
        return fail(http, "HTTP import reentered through guest cabi_realloc");
    vm->async.error = NULL;
    uint32_t a[8] = { 0 }, ret = 0;
    uint64_t timeout = 0;
    for (size_t i = 0; i < args->size; ++i) {
        if (wide_arg(op, i))
            timeout = (uint64_t)args->data[i].of.i64;
        else
            a[i] = (uint32_t)args->data[i].of.i32;
    }
    bool ok;
    if (op <= RETURN_RUN) {
        p3_task *task = vm->current;
        if (!task || task->returned || task->cancelled || a[0] > 1)
            return fail(http, "invalid WASI task return");
        task->http_result = a[0];
        if (op == RETURN_HANDLE) {
            if (a[0])
                return fail(http, "HTTP handler returned an error");
            http_resource *r = wasi_http_get(http, a[1], HTTP_RESPONSE, false);
            if (!r)
                return false;
            r->native = true;
            task->http_result = a[1];
        }
        task->returned = true;
        ok = true;
    }
    else if (op <= DROP_OPTIONS)
        ok = wasi_http_drop(http, a[0],
                            (uint8_t)(op - DROP_FIELDS + HTTP_FIELDS), false);
    else if (op <= FIELDS_COPY)
        ok = field_import(http, op, a, &ret);
    else if (op <= RESPONSE_HEADERS)
        ok = message_import(http, op, a, &ret);
    else
        ok = options_import(http, op, a, timeout, &ret);
    if (ok && results->size) {
        results->data[0].kind = WASM_I32;
        results->data[0].of.i32 = (int32_t)ret;
    }
    return ok;
}
