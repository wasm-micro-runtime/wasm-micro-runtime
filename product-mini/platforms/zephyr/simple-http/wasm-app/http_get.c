/*
 * Copyright (c) 2017 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#ifdef __wasi__
#include <wasi_socket_ext.h>
#endif

/* HTTP server to connect to */
#define HTTP_HOST "192.0.2.10"
/* Port to connect to, as string */
#define HTTP_PORT "8000"
/* HTTP path to request */
#define HTTP_PATH "/"

#define SSTRLEN(s) (sizeof(s) - 1)
// #define CHECK(r) { if (r == -1) { printf("Error %d: " #r "\n", errno);
// exit(1); } }

#define REQUEST "GET " HTTP_PATH " HTTP/1.0\r\nHost: " HTTP_HOST "\r\n\r\n"

static char response[1024];

int
main(int argc, char **argv)
{
    int st, sock;
    struct sockaddr_in addr;
    int rc = 0;

    printf("[wasm-mod] Preparing HTTP GET request for http://" HTTP_HOST
           ":" HTTP_PORT HTTP_PATH "\n");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8000);
    addr.sin_addr.s_addr =
        htonl(0xC000020A); // hard coded IP address for 192.0.2.10

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    printf("[wasm-mod] sock = %d\n", sock);
    if (sock < 0) {
        printf("ERROR: socket failed with errno %d\n", errno);
        return 1;
    }

    rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    printf("[wasm-mod] connect rc = %d\n", rc);
    if (rc < 0) {
        printf("ERROR: connect to " HTTP_HOST ":" HTTP_PORT
               " failed with errno %d\n",
               errno);
        return 2;
    }

    rc = sendto(sock, (const void *)REQUEST, SSTRLEN(REQUEST), 0,
                (struct sockaddr *)&addr, sizeof(addr));
    printf("[wasm-mod] send rc = %d\n", rc);
    if (rc < 0) {
        printf("ERROR: send failed with errno %d\n", errno);
        return 3;
    }

    printf("[wasm-mod] Response:\n\n");

    while (1) {
        socklen_t socklen = sizeof(struct sockaddr_in);
        int len = recvfrom(sock, response, sizeof(response) - 1, 0,
                           (struct sockaddr *)&addr, &socklen);

        if (len < 0) {
            printf("ERROR: receive failed with errno %d\n", errno);
            return 4;
        }

        response[len] = 0;
        printf("%s", response);

        if (len == 0) {
            printf("[wasm-mod] len = 0 break\n");
            break;
        }
    }

    printf("\n");

    (void)close(sock);
    printf("[wasm-mod] Connection closed\n");
    return 0;
}
