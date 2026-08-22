/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_FILESYSTEM_WRAPPER_H
#define WASI_P2_FILESYSTEM_WRAPPER_H

#include "wasm_export.h"
#include "wasi_p2_filesystem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* wasi:filesystem/preopens */
void
wasi_filesystem_get_directories_wrapper(wasm_exec_env_t exec_env,
                                        uint32_t offset_addr);

/* wasi:filesystem/types */

/* descriptor */
void
wasi_filesystem_read_via_stream_wrapper(wasm_exec_env_t exec_env,
                                        wasi_descriptor_t fd,
                                        wasi_filesize_t offset,
                                        uint32_t offset_addr);
void
wasi_filesystem_write_via_stream_wrapper(wasm_exec_env_t exec_env,
                                         wasi_descriptor_t fd,
                                         wasi_filesize_t offset,
                                         uint32_t offset_addr);
void
wasi_filesystem_append_via_stream_wrapper(wasm_exec_env_t exec_env,
                                          wasi_descriptor_t fd,
                                          uint32_t offset_addr);
void
wasi_filesystem_advise_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                               wasi_filesize_t offset, wasi_filesize_t length,
                               uint32_t advice, uint32_t offset_addr);
void
wasi_filesystem_sync_data_wrapper(wasm_exec_env_t exec_env,
                                  wasi_descriptor_t fd, uint32_t offset_addr);
void
wasi_filesystem_get_flags_wrapper(wasm_exec_env_t exec_env,
                                  wasi_descriptor_t fd, uint32_t offset_addr);
void
wasi_filesystem_get_type_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                                 uint32_t offset_addr);
void
wasi_filesystem_set_size_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                                 wasi_filesize_t size, uint32_t offset_addr);
void
wasi_filesystem_set_times_wrapper(wasm_exec_env_t exec_env,
                                  wasi_descriptor_t fd,
                                  uint32_t data_access_timestamp_tag,
                                  int64_t data_access_timestamp_sec,
                                  uint32_t data_access_timestamp_nsec,
                                  uint32_t data_modification_timestamp_tag,
                                  int64_t data_modification_timestamp_sec,
                                  uint32_t data_modification_timestamp_nsec,
                                  uint32_t offset_addr);
void
wasi_filesystem_read_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                             wasi_filesize_t length, wasi_filesize_t offset,
                             uint32_t offset_addr);
void
wasi_filesystem_write_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                              uint32_t *buffer_ptr, uint32_t buffer_len,
                              wasi_filesize_t offset, uint32_t offset_addr);
void
wasi_filesystem_read_directory_wrapper(wasm_exec_env_t exec_env,
                                       wasi_descriptor_t fd,
                                       uint32_t offset_addr);
void
wasi_filesystem_sync_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                             uint32_t offset_addr);
void
wasi_filesystem_create_directory_at_wrapper(wasm_exec_env_t exec_env,
                                            wasi_descriptor_t fd,
                                            uint32_t path_ptr,
                                            uint32_t path_len,
                                            uint32_t offset_addr);
void
wasi_filesystem_stat_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                             uint32_t offset_addr);
void
wasi_filesystem_stat_at_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                                uint32_t path_flags, uint32_t path_ptr,
                                uint32_t path_len, uint32_t offset_addr);
void
wasi_filesystem_set_times_at_wrapper(
    wasm_exec_env_t exec_env, wasi_descriptor_t fd, uint32_t path_flags,
    uint32_t path_ptr, uint32_t path_len, uint32_t data_access_timestamp_tag,
    int64_t data_access_timestamp_sec, uint32_t data_access_timestamp_nsec,
    uint32_t data_modification_timestamp_tag,
    int64_t data_modification_timestamp_sec,
    uint32_t data_modification_timestamp_nsec, uint32_t offset_addr);
void
wasi_filesystem_link_at_wrapper(wasm_exec_env_t exec_env,
                                wasi_descriptor_t old_fd,
                                uint32_t old_path_flags, uint32_t old_path_ptr,
                                uint32_t old_path_len, wasi_descriptor_t new_fd,
                                uint32_t new_path_ptr, uint32_t new_path_len,
                                uint32_t offset_addr);
void
wasi_filesystem_open_at_wrapper(wasm_exec_env_t exec_env, wasi_descriptor_t fd,
                                uint32_t path_flags, uint32_t path_ptr,
                                uint32_t path_len, uint32_t open_flags,
                                uint32_t desc_flags, uint32_t offset_addr);
void
wasi_filesystem_readlink_at_wrapper(wasm_exec_env_t exec_env,
                                    wasi_descriptor_t fd, uint32_t path_ptr,
                                    uint32_t path_len, uint32_t offset_addr);
void
wasi_filesystem_remove_directory_at_wrapper(wasm_exec_env_t exec_env,
                                            wasi_descriptor_t fd,
                                            uint32_t path_ptr,
                                            uint32_t path_len,
                                            uint32_t offset_addr);
void
wasi_filesystem_rename_at_wrapper(wasm_exec_env_t exec_env,
                                  wasi_descriptor_t old_fd,
                                  uint32_t old_path_ptr, uint32_t old_path_len,
                                  wasi_descriptor_t new_fd,
                                  uint32_t new_path_ptr, uint32_t new_path_len,
                                  uint32_t offset_addr);
void
wasi_filesystem_symlink_at_wrapper(wasm_exec_env_t exec_env,
                                   wasi_descriptor_t fd, uint32_t old_path_ptr,
                                   uint32_t old_path_len, uint32_t new_path_ptr,
                                   uint32_t new_path_len, uint32_t offset_addr);
void
wasi_filesystem_unlink_file_at_wrapper(wasm_exec_env_t exec_env,
                                       wasi_descriptor_t fd, uint32_t path_ptr,
                                       uint32_t path_len, uint32_t offset_addr);
uint32_t
wasi_filesystem_is_same_object_wrapper(wasm_exec_env_t exec_env,
                                       wasi_descriptor_t fd1,
                                       wasi_descriptor_t fd2);
void
wasi_filesystem_metadata_hash_wrapper(wasm_exec_env_t exec_env,
                                      wasi_descriptor_t fd,
                                      uint32_t offset_addr);
void
wasi_filesystem_metadata_hash_at_wrapper(wasm_exec_env_t exec_env,
                                         wasi_descriptor_t fd,
                                         uint32_t path_flags, uint32_t path_ptr,
                                         uint32_t path_len,
                                         uint32_t offset_addr);

/* directory-entry-stream */
void
wasi_filesystem_read_directory_entry_wrapper(wasm_exec_env_t exec_env,
                                             int64_t stream,
                                             uint32_t offset_addr);

void
wasi_filesystem_filesystem_error_code_wrapper(wasm_exec_env_t exec_env,
                                              uint32_t err,
                                              uint32_t offset_addr);

#ifdef __cplusplus
}
#endif

#endif /* WASI_P2_FILESYSTEM_WRAPPER_H */
