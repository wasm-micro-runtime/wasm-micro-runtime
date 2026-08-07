/*
 * Copyright (C) 2024 Grenoble INP - ESISAR.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* The runtime pre-opens /lfs, which is where the littlefs volume is mounted */
#define CWD "/lfs"
#define FOLDER_PATH CWD "/folder"
#define FILE_PATH FOLDER_PATH "/test.txt"

static const char DATA[] = "Hello, World!";

/* Exit codes, reported to the host through the WASI exit code */
#define EXIT_OK 0
#define EXIT_MKDIR 1
#define EXIT_WRITE 2
#define EXIT_READ 3
#define EXIT_MISMATCH 4
#define EXIT_STAT 5
#define EXIT_REMOVE 6

/* Write DATA to FILE_PATH, truncating whatever was there before. */
static int
write_file(void)
{
    FILE *file = fopen(FILE_PATH, "w");
    if (!file) {
        printf("ERROR: fopen(w) failed with errno %d\n", errno);
        return EXIT_WRITE;
    }

    size_t written = fwrite(DATA, 1, strlen(DATA), file);
    printf("wrote %d bytes\n", (int)written);

    /* fclose flushes, so the data is in the file system when it returns */
    if (fclose(file) != 0) {
        printf("ERROR: fclose failed with errno %d\n", errno);
        return EXIT_WRITE;
    }

    if (written != strlen(DATA)) {
        printf("ERROR: wrote %d bytes, expected %d\n", (int)written,
               (int)strlen(DATA));
        return EXIT_WRITE;
    }

    return EXIT_OK;
}

/* Re-open FILE_PATH from scratch and check that DATA comes back. */
static int
read_file(void)
{
    char buffer[32] = { 0 };

    FILE *file = fopen(FILE_PATH, "r");
    if (!file) {
        printf("ERROR: fopen(r) failed with errno %d\n", errno);
        return EXIT_READ;
    }

    size_t read = fread(buffer, 1, sizeof(buffer) - 1, file);
    int failed = ferror(file);
    fclose(file);

    if (failed) {
        printf("ERROR: fread failed with errno %d\n", errno);
        return EXIT_READ;
    }

    printf("read %d bytes: %s\n", (int)read, buffer);

    if (read != strlen(DATA) || strcmp(buffer, DATA) != 0) {
        printf("ERROR: content mismatch, read \"%s\" (%d bytes), "
               "expected \"%s\" (%d bytes)\n",
               buffer, (int)read, DATA, (int)strlen(DATA));
        return EXIT_MISMATCH;
    }

    return EXIT_OK;
}

int
main(int argc, char **argv)
{
    struct stat info;
    int rc;

    printf("Hello WebAssembly Module !\n");

    /* The directory survives a warm reboot, so an existing one is not an error
     */
    if (mkdir(FOLDER_PATH, 0777) != 0 && errno != EEXIST) {
        printf("ERROR: mkdir failed with errno %d\n", errno);
        return EXIT_MKDIR;
    }
    printf("directory " FOLDER_PATH " ready\n");

    if ((rc = write_file()) != EXIT_OK)
        return rc;

    if ((rc = read_file()) != EXIT_OK)
        return rc;

    if (stat(FILE_PATH, &info) != 0) {
        printf("ERROR: stat failed with errno %d\n", errno);
        return EXIT_STAT;
    }
    printf("file size on disk: %d bytes\n", (int)info.st_size);

    if (info.st_size != (off_t)strlen(DATA)) {
        printf("ERROR: file size is %d, expected %d\n", (int)info.st_size,
               (int)strlen(DATA));
        return EXIT_STAT;
    }

    if (remove(FILE_PATH) != 0) {
        printf("ERROR: remove failed with errno %d\n", errno);
        return EXIT_REMOVE;
    }

    if (stat(FILE_PATH, &info) == 0 || errno != ENOENT) {
        printf("ERROR: file still there after remove\n");
        return EXIT_REMOVE;
    }
    printf("file removed\n");

    return EXIT_OK;
}
