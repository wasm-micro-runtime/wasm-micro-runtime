/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "payloads.h"
#include "upload.h"

typedef struct workload {
    const uint8_t *bytes;
    uint32_t size, uploaded, received, chunk;
    unsigned state, digits, suffix;
} workload;

static void
receive_byte(workload *w, uint8_t ch)
{
    switch (w->state) {
        case 0:
        {
            /* Parse the first line and then scan through CRLF CRLF. */
            static const char status[] = "HTTP/1.1 200\r\n";
            CHECK(ch == (uint8_t)status[w->suffix]);
            if (++w->suffix == sizeof(status) - 1) {
                w->state = 1;
                w->suffix = 2;
            }
            break;
        }
        case 1:
        {
            static const char end[] = "\r\n\r\n";
            if (ch == (uint8_t)end[w->suffix]) {
                if (++w->suffix == 4) {
                    w->state = 2;
                    w->suffix = 0;
                }
            }
            else
                w->suffix = ch == '\r';
            break;
        }
        case 2:
            if (ch == '\r') {
                CHECK(w->digits && w->digits <= 4);
                w->state = 3;
            }
            else {
                CHECK(
                    w->digits < 4
                    && ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')));
                w->chunk =
                    w->chunk * 16 + (ch <= '9' ? ch - '0' : ch - 'a' + 10);
                ++w->digits;
            }
            break;
        case 3:
            CHECK(ch == '\n');
            w->state = w->chunk ? 4 : 6;
            w->suffix = 0;
            break;
        case 4:
            CHECK(w->received < w->size && ch == w->bytes[w->received++]);
            if (!--w->chunk) {
                w->state = 5;
                w->suffix = 0;
            }
            break;
        case 6:
        {
            const char *end =
                w->size > 4096 ? "x-upload-check: complete\r\n\r\n" : "\r\n";
            CHECK(ch == (uint8_t)end[w->suffix]);
            if (!end[++w->suffix])
                w->state = 7;
            break;
        }
        case 5:
            CHECK(ch == (w->suffix ? '\n' : '\r'));
            if (w->suffix++) {
                w->state = 2;
                w->digits = 0;
            }
            break;
        default:
            CHECK(false);
    }
}

static void
run_http(void)
{
    static const char *const requests[] = {
        "GET /data.json HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "GET /image.png HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
        "16384\r\n\r\n",
        "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
        "16384\r\n\r\n"
    };
    workload jobs[HTTP_CONNECTIONS] = {
        { json_body, sizeof(json_body), sizeof(json_body), 0, 0, 0, 0, 0 },
        { png_body, sizeof(png_body), sizeof(png_body), 0, 0, 0, 0, 0 },
        { upload_body, sizeof(upload_body), 0, 0, 0, 0, 0, 0 },
        { upload_body, sizeof(upload_body), 0, 0, 0, 0, 0, 0 }
    };
    for (unsigned i = 0; i < HTTP_CONNECTIONS; ++i) {
        CHECK(http_open(&server, i));
        CHECK(http_input(&server, i, (uint8_t *)requests[i],
                         (uint32_t)strlen(requests[i])));
    }
    CHECK(!http_open(&server, 0) && !http_open(&server, HTTP_CONNECTIONS));
    unsigned complete = 0;
    for (unsigned turn = 0; turn < 4096 && complete < HTTP_CONNECTIONS;
         ++turn) {
        for (unsigned i = 0; i < HTTP_CONNECTIONS; ++i) {
            http_connection *c = &server.connections[i];
            if (!c->live)
                continue;
            workload *w = &jobs[i];
            CHECK(http_progress(&server, i));
            uint32_t cap = http_input_capacity(&server, i);
            if (cap > 379)
                cap = 379;
            if (cap > w->size - w->uploaded)
                cap = w->size - w->uploaded;
            if (cap) {
                CHECK(http_input(&server, i, (uint8_t *)w->bytes + w->uploaded,
                                 cap));
                w->uploaded += cap;
            }
            /* Force backpressure on one upload while the others progress. */
            if (i != 3 || turn >= 32) {
                size_t window = c->length - c->sent;
                if (window > 73)
                    window = 73;
                for (size_t j = 0; j < window; ++j)
                    receive_byte(w, c->output[c->sent++]);
            }
            if (http_finished(c)) {
                CHECK(w->state == 7 && w->received == w->size);
                CHECK(http_reap(&server, i));
                ++complete;
            }
        }
    }
    CHECK(complete == HTTP_CONNECTIONS);
    CHECK(http_open(&server, 0));
    CHECK(http_input(&server, 0, (uint8_t *)requests[2],
                     (uint32_t)strlen(requests[2])));
    CHECK(http_progress(&server, 0));
    CHECK(http_disconnect(&server, 0));
    CHECK(http_progress(&server, 0));
    CHECK(http_reap(&server, 0));
    CHECK(server.completed == 4 && server.disconnected == 1);
}
