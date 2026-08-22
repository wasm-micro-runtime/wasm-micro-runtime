/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_FILESYSTEM_H
#define WASI_P2_FILESYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>

#include "wasi_p2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t wasi_descriptor_t;
typedef DIR *wasi_directory_entry_stream_t;

typedef enum wasi_descriptor_type_t {
    WASI_DESCRIPTOR_TYPE_UNKNOWN,
    WASI_DESCRIPTOR_TYPE_BLOCK_DEVICE,
    WASI_DESCRIPTOR_TYPE_CHARACTER_DEVICE,
    WASI_DESCRIPTOR_TYPE_DIRECTORY,
    WASI_DESCRIPTOR_TYPE_FIFO,
    WASI_DESCRIPTOR_TYPE_SYMBOLIC_LINK,
    WASI_DESCRIPTOR_TYPE_REGULAR_FILE,
    WASI_DESCRIPTOR_TYPE_SOCKET,
} wasi_descriptor_type_t;

typedef uint32_t wasi_descriptor_flags_t;

#define WASI_DESCRIPTOR_FLAGS_READ (1 << 0)
#define WASI_DESCRIPTOR_FLAGS_WRITE (1 << 1)
#define WASI_DESCRIPTOR_FLAGS_FILE_INTEGRITY_SYNC (1 << 2)
#define WASI_DESCRIPTOR_FLAGS_DATA_INTEGRITY_SYNC (1 << 3)
#define WASI_DESCRIPTOR_FLAGS_REQUESTED_WRITE_SYNC (1 << 4)
#define WASI_DESCRIPTOR_FLAGS_MUTATE_DIRECTORY (1 << 5)

typedef struct wasi_optional_datetime_t {
    bool has_value;
    wasi_datetime_t datetime;
} wasi_optional_datetime_t;

typedef struct wasi_descriptor_stat_t {
    wasi_descriptor_type_t type;
    wasi_link_count_t link_count;
    wasi_filesize_t size;
    wasi_optional_datetime_t data_access_timestamp;
    wasi_optional_datetime_t data_modification_timestamp;
    wasi_optional_datetime_t status_change_timestamp;
} wasi_descriptor_stat_t;

typedef uint32_t wasi_path_flags_t;

#define WASI_PATH_FLAGS_SYMLINK_FOLLOW (1 << 0)

typedef enum wasi_access_type_t {
    WASI_ACCESS_TYPE_READ,
    WASI_ACCESS_TYPE_WRITE,
    WASI_ACCESS_TYPE_EXECUTE,
} wasi_access_type_t;

typedef uint32_t wasi_access_flags_t;

#define WASI_ACCESS_FLAGS_READ (1 << 0)
#define WASI_ACCESS_FLAGS_WRITE (1 << 1)
#define WASI_ACCESS_FLAGS_EXECUTE (1 << 2)

typedef uint32_t wasi_open_flags_t;

#define WASI_OPEN_FLAGS_CREATE (1 << 0)
#define WASI_OPEN_FLAGS_DIRECTORY (1 << 1)
#define WASI_OPEN_FLAGS_EXCLUSIVE (1 << 2)
#define WASI_OPEN_FLAGS_TRUNCATE (1 << 3)

typedef enum wasi_new_timestamp_tag_t {
    WASI_NEW_TIMESTAMP_TAG_NO_CHANGE,
    WASI_NEW_TIMESTAMP_TAG_NOW,
    WASI_NEW_TIMESTAMP_TAG_TIMESTAMP,
} wasi_new_timestamp_tag_t;

typedef struct wasi_new_timestamp_t {
    wasi_new_timestamp_tag_t tag;
    wasi_datetime_t timestamp;
} wasi_new_timestamp_t;

typedef struct wasi_directory_entry_t {
    wasi_descriptor_type_t type;
    char *name;
} wasi_directory_entry_t;

typedef enum wasi_advice_t {
    WASI_ADVICE_NORMAL,
    WASI_ADVICE_SEQUENTIAL,
    WASI_ADVICE_RANDOM,
    WASI_ADVICE_WILL_NEED,
    WASI_ADVICE_DONT_NEED,
    WASI_ADVICE_NO_REUSE,
} wasi_advice_t;

typedef struct wasi_metadata_hash_value_t {
    uint64_t lower;
    uint64_t upper;
} wasi_metadata_hash_value_t;

typedef struct wasi_tuple_descriptor_string_t {
    wasi_descriptor_t descriptor;
    char *string;
} wasi_tuple_descriptor_string_t;

// wasi:filesystem/types
void
wasi_filesystem_read_via_stream(wasi_descriptor_t fd, wasi_filesize_t offset,
                                wasi_input_stream_t *ret, int *err);

void
wasi_filesystem_write_via_stream(wasi_descriptor_t fd, wasi_filesize_t offset,
                                 wasi_output_stream_t *ret, int *err);

void
wasi_filesystem_append_via_stream(wasi_descriptor_t fd,
                                  wasi_output_stream_t *ret, int *err);

int
wasi_filesystem_advise(wasi_descriptor_t fd, wasi_filesize_t offset,
                       wasi_filesize_t length, wasi_advice_t advice);

int
wasi_filesystem_sync_data(wasi_descriptor_t fd);

void
wasi_filesystem_get_flags(wasi_descriptor_t fd, wasi_descriptor_flags_t *ret,
                          int *err);

void
wasi_filesystem_get_type(wasi_descriptor_t fd, wasi_descriptor_type_t *ret,
                         int *err);

int
wasi_filesystem_set_size(wasi_descriptor_t fd, wasi_filesize_t size);

int
wasi_filesystem_set_times(wasi_descriptor_t fd,
                          wasi_new_timestamp_t data_access_timestamp,
                          wasi_new_timestamp_t data_modification_timestamp);

void
wasi_filesystem_read(wasi_descriptor_t fd, wasi_filesize_t length,
                     wasi_filesize_t offset, wasi_list_u8_t *ret,
                     bool *end_of_stream, int *err);

void
wasi_filesystem_write(wasi_descriptor_t fd, const uint8_t *buf,
                      uint64_t buf_len, wasi_filesize_t offset,
                      wasi_filesize_t *ret, int *err);

void
wasi_filesystem_read_directory(wasi_descriptor_t fd,
                               wasi_directory_entry_stream_t *ret, int *err);

int
wasi_filesystem_sync(wasi_descriptor_t fd);

int
wasi_filesystem_create_directory_at(wasi_descriptor_t fd, const char *path);

void
wasi_filesystem_stat(wasi_descriptor_t fd, wasi_descriptor_stat_t *ret,
                     int *err);

void
wasi_filesystem_stat_at(wasi_descriptor_t fd, wasi_path_flags_t path_flags,
                        const char *path, wasi_descriptor_stat_t *ret,
                        int *err);

int
wasi_filesystem_set_times_at(wasi_descriptor_t fd, wasi_path_flags_t path_flags,
                             const char *path,
                             wasi_new_timestamp_t data_access_timestamp,
                             wasi_new_timestamp_t data_modification_timestamp);

int
wasi_filesystem_link_at(wasi_descriptor_t old_fd,
                        wasi_path_flags_t old_path_flags, const char *old_path,
                        wasi_descriptor_t new_fd, const char *new_path);

void
wasi_filesystem_open_at(wasi_descriptor_t fd, wasi_path_flags_t path_flags,
                        const char *path, wasi_open_flags_t open_flags,
                        wasi_descriptor_flags_t flags, mode_t mode,
                        wasi_descriptor_t *ret, int *err);

void
wasi_filesystem_readlink_at(wasi_descriptor_t fd, const char *path, char **ret,
                            int *err);

int
wasi_filesystem_remove_directory_at(wasi_descriptor_t fd, const char *path);

int
wasi_filesystem_rename_at(wasi_descriptor_t old_fd, const char *old_path,
                          wasi_descriptor_t new_fd, const char *new_path);

int
wasi_filesystem_symlink_at(wasi_descriptor_t fd, const char *old_path,
                           const char *new_path);

int
wasi_filesystem_unlink_file_at(wasi_descriptor_t fd, const char *path);

bool
wasi_filesystem_is_same_object(wasi_descriptor_t fd1, wasi_descriptor_t fd2);

void
wasi_filesystem_metadata_hash(wasi_descriptor_t fd,
                              wasi_metadata_hash_value_t *ret, int *err);

void
wasi_filesystem_metadata_hash_at(wasi_descriptor_t fd,
                                 wasi_path_flags_t path_flags, const char *path,
                                 wasi_metadata_hash_value_t *ret, int *err);

void
wasi_filesystem_read_directory_entry(wasi_directory_entry_stream_t stream,
                                     wasi_directory_entry_t *ret, bool *is_some,
                                     int *error_code);

int
wasi_filesystem_close(wasi_descriptor_t fd);

int
wasi_filesystem_error_code(int err, bool *is_some);

void
directory_entry_stream_dtor(void *data);
void
file_stream_dtor(void *data);
void
filesystem_descriptor_dtor(void *data);

#ifdef __cplusplus
}
#endif

#endif /* end of _WASI_FILESYSTEM_H */
