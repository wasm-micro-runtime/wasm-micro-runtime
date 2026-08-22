/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>
#include <map>

extern "C" {
#include "wasi_p2_filesystem.h"
#include "wasi_p2_common.h"
}

class WasiP2FilesystemTest : public testing::Test {
protected:
    char test_dir[64] = { 0 };
    char test_file[128] = { 0 };
    char test_link[128] = { 0 };

    void SetUp() override {
        wasm_runtime_init();
        strcpy(test_dir, "/tmp/wasi_p2_fs_test.XXXXXX");
        ASSERT_NE(mkdtemp(test_dir), nullptr);
        snprintf(test_file, sizeof(test_file), "%s/test_file.txt", test_dir);
        snprintf(test_link, sizeof(test_link), "%s/test_link.txt", test_dir);
    }

    void TearDown() override {
        if (test_dir[0] != '\0') {
            char command[256];
            snprintf(command, sizeof(command), "rm -rf %s", test_dir);
            system(command);
        }
        wasm_runtime_destroy();
    }
};

// Test creating and removing a directory.
TEST_F(WasiP2FilesystemTest, Filesystem_CreateAndRemoveDirectory) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    const char *new_dir_name = "new_dir";
    int err =
        wasi_filesystem_create_directory_at(dir_fd, new_dir_name);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    char new_dir_path[256];
    snprintf(new_dir_path, sizeof(new_dir_path), "%s/%s", test_dir,
             new_dir_name);

    struct stat st;
    ASSERT_EQ(stat(new_dir_path, &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));

    err = wasi_filesystem_remove_directory_at(dir_fd, new_dir_name);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(stat(new_dir_path, &st), 0);

    close(dir_fd);
}

// Test: wasi:filesystem/types.read-directory-entry with failing fstatat
TEST_F(WasiP2FilesystemTest, ReadDirectoryEntry_FstatatFailure) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    // Create a file that we can't stat later
    char unstatable_file_path[256];
    snprintf(unstatable_file_path, sizeof(unstatable_file_path),
             "%s/unstatable", test_dir);
    int fd = open(unstatable_file_path, O_CREAT | O_WRONLY, 0644);
    ASSERT_NE(fd, -1);
    close(fd);

    // This test relies on the underlying filesystem returning DT_UNKNOWN for
    // d_type, to force a fallback to fstatat. To simulate this, we can't be
    // sure, but we can make fstatat fail and check that the error is
    // propagated.
    chmod(unstatable_file_path, 0000);

    wasi_directory_entry_stream_t stream;
    int err;
    wasi_filesystem_read_directory(dir_fd, &stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(stream, nullptr);

    wasi_directory_entry_t entry;
    bool is_some;
    bool error_found = false;

    // We loop through the directory. If the filesystem returns DT_UNKNOWN for
    // our unstatable file, the implementation will fall back to fstatat, which
    // will fail with EACCES. The test verifies that this error is propagated.
    while (true) {
        wasi_filesystem_read_directory_entry(stream, &entry, &is_some, &err);
        if (err != WASI_ERROR_CODE_SUCCESS) {
            error_found = true;
            ASSERT_EQ(err, EACCES);
            ASSERT_FALSE(is_some);
            break;
        }
        if (!is_some) {
            break;
        }
        if (strcmp(entry.name, "unstatable") == 0) {
            // If we successfully read the entry, it means d_type was not
            // DT_UNKNOWN, and the fstatat fallback was not triggered.
        }
        wasm_runtime_free(entry.name);
    }

    if (!error_found) {
        printf("Warning: test 'ReadDirectoryEntry_FstatatFailure' may not "
               "have triggered the intended fstatat fallback path on this "
               "filesystem.\n");
    }

    close(dir_fd);
}

// Test opening a file with different synchronization flags.
TEST_F(WasiP2FilesystemTest, Filesystem_OpenAtWithSyncFlags) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    wasi_descriptor_t file_fd;
    int err;

    // Test with O_SYNC
    wasi_filesystem_open_at(
        dir_fd, 0, "sync_file.txt", WASI_OPEN_FLAGS_CREATE,
        WASI_DESCRIPTOR_FLAGS_WRITE | WASI_DESCRIPTOR_FLAGS_FILE_INTEGRITY_SYNC,
        0644, &file_fd, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    int flags = fcntl(file_fd, F_GETFL);
    ASSERT_NE(flags, -1);
    ASSERT_TRUE(flags & O_SYNC);
    close(file_fd);

    // Test with O_DSYNC
    wasi_filesystem_open_at(
        dir_fd, 0, "dsync_file.txt", WASI_OPEN_FLAGS_CREATE,
        WASI_DESCRIPTOR_FLAGS_WRITE | WASI_DESCRIPTOR_FLAGS_DATA_INTEGRITY_SYNC,
        0644, &file_fd, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    flags = fcntl(file_fd, F_GETFL);
    ASSERT_NE(flags, -1);
    ASSERT_TRUE(flags & O_DSYNC);
    close(file_fd);

#ifdef O_RSYNC
    // Test with O_RSYNC
    wasi_filesystem_open_at(dir_fd, 0, "rsync_file.txt",
                            WASI_OPEN_FLAGS_CREATE,
                            WASI_DESCRIPTOR_FLAGS_WRITE
                                | WASI_DESCRIPTOR_FLAGS_REQUESTED_WRITE_SYNC,
                            0644, &file_fd, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    flags = fcntl(file_fd, F_GETFL);
    ASSERT_NE(flags, -1);
    ASSERT_TRUE(flags & O_RSYNC);
    close(file_fd);
#endif

    close(dir_fd);
}

// Test reading a directory containing various file types.
TEST_F(WasiP2FilesystemTest, Filesystem_ReadDirectoryWithMixedTypes) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    // Create a variety of file types
    char regular_file_path[256];
    snprintf(regular_file_path, sizeof(regular_file_path), "%s/regular_file",
             test_dir);
    int fd = open(regular_file_path, O_CREAT | O_WRONLY, 0644);
    ASSERT_NE(fd, -1);
    close(fd);

    char sub_dir_path[256];
    snprintf(sub_dir_path, sizeof(sub_dir_path), "%s/sub_dir", test_dir);
    ASSERT_EQ(mkdir(sub_dir_path, 0755), 0);

    char symlink_path[256];
    snprintf(symlink_path, sizeof(symlink_path), "%s/symlink_file", test_dir);
    ASSERT_EQ(symlink("target_file", symlink_path), 0);

    char fifo_path[256];
    snprintf(fifo_path, sizeof(fifo_path), "%s/fifo_file", test_dir);
    ASSERT_EQ(mkfifo(fifo_path, 0644), 0);

    wasi_directory_entry_stream_t stream;
    int err;
    wasi_filesystem_read_directory(dir_fd, &stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(stream, nullptr);

    std::map<std::string, wasi_descriptor_type_t> found_entries;
    wasi_directory_entry_t entry;
    bool is_some;

    while (true) {
        wasi_filesystem_read_directory_entry(stream, &entry, &is_some, &err);
        ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
        if (!is_some) {
            break;
        }
        found_entries[entry.name] = entry.type;
        wasm_runtime_free(entry.name);
    }

    ASSERT_EQ(found_entries.size(), 4);
    ASSERT_EQ(found_entries["regular_file"],
              WASI_DESCRIPTOR_TYPE_REGULAR_FILE);
    ASSERT_EQ(found_entries["sub_dir"], WASI_DESCRIPTOR_TYPE_DIRECTORY);
    ASSERT_EQ(found_entries["symlink_file"],
              WASI_DESCRIPTOR_TYPE_SYMBOLIC_LINK);
    ASSERT_EQ(found_entries["fifo_file"], WASI_DESCRIPTOR_TYPE_FIFO);

    close(dir_fd);
}

// Test reading a directory containing a dangling symlink.
TEST_F(WasiP2FilesystemTest, Filesystem_ReadDirectoryWithDanglingSymlink) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    char dangling_symlink_path[256];
    snprintf(dangling_symlink_path, sizeof(dangling_symlink_path),
             "%s/dangling_link", test_dir);
    ASSERT_EQ(symlink("non_existent_target", dangling_symlink_path), 0);

    wasi_directory_entry_stream_t stream;
    int err;
    wasi_filesystem_read_directory(dir_fd, &stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(stream, nullptr);

    wasi_directory_entry_t entry;
    bool is_some;
    bool found_dangling_link = false;

    while (true) {
        wasi_filesystem_read_directory_entry(stream, &entry, &is_some, &err);
        ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
        if (!is_some) {
            break;
        }
        if (strcmp(entry.name, "dangling_link") == 0) {
            ASSERT_EQ(entry.type, WASI_DESCRIPTOR_TYPE_SYMBOLIC_LINK);
            found_dangling_link = true;
        }
        wasm_runtime_free(entry.name);
    }

    ASSERT_TRUE(found_dangling_link);

    close(dir_fd);
}

// Test creating a directory that already exists.
TEST_F(WasiP2FilesystemTest, Filesystem_CreateDirectoryAtExistingPath) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    const char *new_dir_name = "new_dir";
    int err =
        wasi_filesystem_create_directory_at(dir_fd, new_dir_name);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Try to create the same directory again
    err = wasi_filesystem_create_directory_at(dir_fd, new_dir_name);
    ASSERT_EQ(err, EEXIST);

    err = wasi_filesystem_remove_directory_at(dir_fd, new_dir_name);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    close(dir_fd);
}

// Test removing a non-empty directory.
TEST_F(WasiP2FilesystemTest, Filesystem_RemoveNonEmptyDirectory) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    const char *new_dir_name = "new_dir";
    int err =
        wasi_filesystem_create_directory_at(dir_fd, new_dir_name);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    char new_dir_path[256];
    snprintf(new_dir_path, sizeof(new_dir_path), "%s/%s", test_dir,
             new_dir_name);
    int new_dir_fd = open(new_dir_path, O_RDONLY);
    ASSERT_NE(new_dir_fd, -1);

    wasi_descriptor_t file_fd;
    wasi_filesystem_open_at(new_dir_fd, 0, "test_file.txt",
                            WASI_OPEN_FLAGS_CREATE,
                            WASI_DESCRIPTOR_FLAGS_WRITE, 0777, &file_fd, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    close(file_fd);

    err = wasi_filesystem_remove_directory_at(dir_fd, new_dir_name);
    ASSERT_EQ(err, ENOTEMPTY);

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/test_file.txt", new_dir_path);
    unlink(file_path);

    err = wasi_filesystem_remove_directory_at(dir_fd, new_dir_name);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    close(new_dir_fd);
    close(dir_fd);
}

// Test reading and writing zero bytes.
TEST_F(WasiP2FilesystemTest, Filesystem_ReadWriteZeroBytes) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    wasi_filesize_t written;
    int err;

    wasi_filesystem_write(fd, (const uint8_t *)"", 0, 0, &written, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(written, 0);

    wasi_list_u8_t read_buf;
    bool end_of_stream;
    wasi_filesystem_read(fd, 0, 0, &read_buf, &end_of_stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(read_buf.buf_len, 0);
    ASSERT_FALSE(end_of_stream);

    close(fd);
}

// Test opening a file with no read permission.
TEST_F(WasiP2FilesystemTest, Filesystem_OpenAtWithNoReadPermission) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    wasi_descriptor_t file_fd;
    int err;

    int fd = open(test_file, O_CREAT | O_WRONLY, 0666);
    ASSERT_NE(fd, -1);
    close(fd);

    chmod(test_file, 0222);

    wasi_filesystem_open_at(dir_fd, 0, "test_file.txt", 0,
                            WASI_DESCRIPTOR_FLAGS_READ, 0, &file_fd, &err);
    ASSERT_EQ(err, EACCES);

    close(dir_fd);
}

// Test opening a file with no write permission.
TEST_F(WasiP2FilesystemTest, Filesystem_OpenAtWithNoWritePermission) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    wasi_descriptor_t file_fd;
    int err;

    int fd = open(test_file, O_CREAT | O_RDONLY, 0666);
    ASSERT_NE(fd, -1);
    close(fd);

    chmod(test_file, 0444);

    wasi_filesystem_open_at(dir_fd, 0, "test_file.txt", 0,
                            WASI_DESCRIPTOR_FLAGS_WRITE, 0, &file_fd, &err);
    ASSERT_EQ(err, EACCES);

    close(dir_fd);
}

// Test opening and closing a file.
TEST_F(WasiP2FilesystemTest, Filesystem_OpenAndClose) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    wasi_descriptor_t file_fd;
    int err;

    wasi_filesystem_open_at(dir_fd, 0, "test_file.txt", WASI_OPEN_FLAGS_CREATE,
                            WASI_DESCRIPTOR_FLAGS_WRITE, 0777, &file_fd, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_GT(file_fd, 0);

    ASSERT_EQ(wasi_filesystem_close(file_fd), WASI_ERROR_CODE_SUCCESS);

    close(dir_fd);
}

// Test opening a symbolic link without following it.
TEST_F(WasiP2FilesystemTest, Filesystem_OpenAtSymlinkNoFollow) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    int target_fd = open(test_file, O_CREAT | O_WRONLY, 0644);
    ASSERT_NE(target_fd, -1);
    close(target_fd);

    ASSERT_EQ(symlink("test_file.txt", test_link), 0);

    wasi_descriptor_t file_fd;
    int err;

    wasi_filesystem_open_at(dir_fd, 0, "test_link.txt", 0,
                            WASI_DESCRIPTOR_FLAGS_READ, 0, &file_fd, &err);

    ASSERT_EQ(err, ELOOP);

    close(dir_fd);
}

// Test reading and writing to a file via streams.
TEST_F(WasiP2FilesystemTest, Filesystem_StreamReadWrite) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    wasi_input_stream_t read_stream;
    wasi_output_stream_t write_stream;
    int err;

    wasi_filesystem_read_via_stream(fd, 0, &read_stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(read_stream, -1);

    wasi_filesystem_write_via_stream(fd, 0, &write_stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(write_stream, -1);

    const char *msg = "hello";
    ssize_t n = write(write_stream, msg, strlen(msg));
    ASSERT_EQ(n, strlen(msg));

    lseek(read_stream, 0, SEEK_SET);

    char buf[16] = { 0 };
    n = read(read_stream, buf, sizeof(buf));
    ASSERT_EQ(n, strlen(msg));
    ASSERT_STREQ(buf, msg);

    close(read_stream);
    close(write_stream);
    close(fd);
}

// Test appending to a file via a stream.
TEST_F(WasiP2FilesystemTest, Filesystem_StreamAppend) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    wasi_output_stream_t append_stream;
    int err;

    const char *msg1 = "hello";
    ssize_t n = write(fd, msg1, strlen(msg1));
    ASSERT_EQ(n, strlen(msg1));

    wasi_filesystem_append_via_stream(fd, &append_stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(append_stream, -1);

    const char *msg2 = " world";
    n = write(append_stream, msg2, strlen(msg2));
    ASSERT_EQ(n, strlen(msg2));

    char buf[32] = { 0 };
    lseek(fd, 0, SEEK_SET);
    n = read(fd, buf, sizeof(buf));
    ASSERT_EQ(n, strlen(msg1) + strlen(msg2));
    ASSERT_STREQ(buf, "hello world");

    close(append_stream);
    close(fd);
}

// Test appending to a read-only file via a stream.
TEST_F(WasiP2FilesystemTest, Filesystem_StreamAppendOnReadOnlyFile) {
    // Open a file as read-only
    int fd = open(test_file, O_CREAT | O_RDONLY, 0644);
    ASSERT_NE(fd, -1);

    wasi_output_stream_t append_stream;
    int err;

    // Attempting to create an append stream from a read-only descriptor
    // should fail with an access error.
    wasi_filesystem_append_via_stream(fd, &append_stream, &err);
    ASSERT_EQ(err, EACCES);
    ASSERT_EQ(append_stream, -1);

    close(fd);
}

// Test getting file metadata.
TEST_F(WasiP2FilesystemTest, Filesystem_GetStat) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    wasi_descriptor_stat_t stat;
    int err;

    wasi_filesystem_stat(fd, &stat, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(stat.type, WASI_DESCRIPTOR_TYPE_REGULAR_FILE);

    close(fd);
}

// Test getting file metadata at a path.
TEST_F(WasiP2FilesystemTest, Filesystem_GetStatAt) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);
    close(fd);

    wasi_descriptor_stat_t stat;
    int err;

    wasi_filesystem_stat_at(dir_fd, 0, "test_file.txt", &stat, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(stat.type, WASI_DESCRIPTOR_TYPE_REGULAR_FILE);

    close(dir_fd);
}

// Test advising the kernel about file access patterns.
TEST_F(WasiP2FilesystemTest, Filesystem_Advise) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    ASSERT_EQ(wasi_filesystem_advise(fd, 0, 0, WASI_ADVICE_NORMAL), 0);

    close(fd);
}

// Test synchronizing file data and metadata to disk.
TEST_F(WasiP2FilesystemTest, Filesystem_Sync) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    ASSERT_EQ(wasi_filesystem_sync(fd), 0);
    ASSERT_EQ(wasi_filesystem_sync_data(fd), 0);

    close(fd);
}

// Test setting file timestamps.
TEST_F(WasiP2FilesystemTest, Filesystem_SetTimes) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    wasi_new_timestamp_t now = { .tag = WASI_NEW_TIMESTAMP_TAG_NOW, .timestamp = {.seconds = 0, .nanoseconds = 0} };
    wasi_new_timestamp_t no_change = {
        .tag = WASI_NEW_TIMESTAMP_TAG_NO_CHANGE,
        .timestamp = { .seconds = 0, .nanoseconds = 0 }
    };
    wasi_new_timestamp_t specific_time = {
        .tag = WASI_NEW_TIMESTAMP_TAG_TIMESTAMP,
        .timestamp = { .seconds = 100, .nanoseconds = 200 }
    };

    ASSERT_EQ(wasi_filesystem_set_times(fd, now, now), 0);
    ASSERT_EQ(wasi_filesystem_set_times(fd, no_change, no_change), 0);
    ASSERT_EQ(wasi_filesystem_set_times(fd, specific_time, specific_time), 0);

    close(fd);
}

// Test setting file timestamps at a path.
TEST_F(WasiP2FilesystemTest, Filesystem_SetTimesAt) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);
    close(fd);

    wasi_new_timestamp_t now = { 
        .tag = WASI_NEW_TIMESTAMP_TAG_NOW,
        .timestamp = { .seconds = 0, .nanoseconds = 0 }
    };
    wasi_new_timestamp_t no_change = {
        .tag = WASI_NEW_TIMESTAMP_TAG_NO_CHANGE,
        .timestamp = { .seconds = 0, .nanoseconds = 0 }
    };
    wasi_new_timestamp_t specific_time = {
        .tag = WASI_NEW_TIMESTAMP_TAG_TIMESTAMP,
        .timestamp = { .seconds = 100, .nanoseconds = 200 }
    };

    ASSERT_EQ(
        wasi_filesystem_set_times_at(dir_fd, 0, "test_file.txt", now, now), 0);
    ASSERT_EQ(wasi_filesystem_set_times_at(dir_fd, 0, "test_file.txt",
                                           no_change, no_change),
              0);
    ASSERT_EQ(wasi_filesystem_set_times_at(dir_fd, 0, "test_file.txt",
                                           specific_time, specific_time),
              0);

    close(dir_fd);
}

// Test basic reading and writing to a file.
TEST_F(WasiP2FilesystemTest, Filesystem_ReadAndWrite) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    const char *msg = "hello";
    wasi_filesize_t written;
    int err;

    wasi_filesystem_write(fd, (const uint8_t *)msg, strlen(msg), 0, &written,
                          &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(written, strlen(msg));

    wasi_list_u8_t read_buf;
    bool end_of_stream;
    wasi_filesystem_read(fd, 16, 0, &read_buf, &end_of_stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(read_buf.buf_len, strlen(msg));
    ASSERT_TRUE(end_of_stream);
    ASSERT_EQ(memcmp(read_buf.buf, msg, strlen(msg)), 0);

    wasm_runtime_free(read_buf.buf);
    close(fd);
}

// Test reading directory entries.
TEST_F(WasiP2FilesystemTest, Filesystem_ReadDirectory) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    int file_fd = open(test_file, O_CREAT | O_WRONLY, 0644);
    ASSERT_NE(file_fd, -1);
    close(file_fd);

    wasi_directory_entry_stream_t stream;
    int err;

    wasi_filesystem_read_directory(dir_fd, &stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(stream, nullptr);

    wasi_directory_entry_t entry;
    bool is_some;

    wasi_filesystem_read_directory_entry(stream, &entry, &is_some, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_TRUE(is_some);
    ASSERT_STREQ(entry.name, "test_file.txt");
    wasm_runtime_free(entry.name);

    wasi_filesystem_read_directory_entry(stream, &entry, &is_some, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_FALSE(is_some);

    close(dir_fd);
}

// Test creating and removing a hard link.
TEST_F(WasiP2FilesystemTest, Filesystem_LinkAndUnlink) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);
    close(fd);

    ASSERT_EQ(wasi_filesystem_link_at(dir_fd, 0, "test_file.txt", dir_fd,
                                      "test_link.txt"),
              0);

    struct stat st;
    ASSERT_EQ(stat(test_link, &st), 0);

    ASSERT_EQ(wasi_filesystem_unlink_file_at(dir_fd, "test_link.txt"), 0);
    ASSERT_NE(stat(test_link, &st), 0);

    close(dir_fd);
}

// Test reading a symbolic link.
TEST_F(WasiP2FilesystemTest, Filesystem_Readlink) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    ASSERT_EQ(symlink("test_file.txt", test_link), 0);

    char *ret_path;
    int err;
    wasi_filesystem_readlink_at(dir_fd, "test_link.txt", &ret_path, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_STREQ(ret_path, "test_file.txt");
    wasm_runtime_free(ret_path);

    close(dir_fd);
}

// Test reading a symbolic link with a long path.
TEST_F(WasiP2FilesystemTest, Filesystem_ReadlinkLongPath) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    // Create a long path that will force realloc in
    // wasi_filesystem_readlink_at
    std::string long_target(260, 'a');
    char long_link_path[512];
    snprintf(long_link_path, sizeof(long_link_path), "%s/long_link",
             test_dir);
    ASSERT_EQ(symlink(long_target.c_str(), long_link_path), 0);

    char *ret_path;
    int err;
    wasi_filesystem_readlink_at(dir_fd, "long_link", &ret_path, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_STREQ(ret_path, long_target.c_str());
    wasm_runtime_free(ret_path);

    close(dir_fd);
}

// Test checking if two descriptors refer to the same object.
TEST_F(WasiP2FilesystemTest, Filesystem_IsSameObject) {
    int fd1 = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd1, -1);
    int fd2 = open(test_file, O_RDONLY);
    ASSERT_NE(fd2, -1);
    int fd3 = open(test_link, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd3, -1);

    ASSERT_TRUE(wasi_filesystem_is_same_object(fd1, fd2));
    ASSERT_FALSE(wasi_filesystem_is_same_object(fd1, fd3));

    close(fd1);
    close(fd2);
    close(fd3);
}

// Test getting the metadata hash of a file.
TEST_F(WasiP2FilesystemTest, Filesystem_GetMetadataHash) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    wasi_metadata_hash_value_t hash;
    int err;
    wasi_filesystem_metadata_hash(fd, &hash, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(hash.lower, 0);
    ASSERT_NE(hash.upper, 0);

    close(fd);
}

// Test getting the metadata hash of a file at a path.
TEST_F(WasiP2FilesystemTest, Filesystem_GetMetadataHashAt) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);
    close(fd);

    wasi_metadata_hash_value_t hash;
    int err;
    wasi_filesystem_metadata_hash_at(dir_fd, 0, "test_file.txt", &hash, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(hash.lower, 0);
    ASSERT_NE(hash.upper, 0);

    close(dir_fd);
}

// Test getting the type and flags of a descriptor.
TEST_F(WasiP2FilesystemTest, Filesystem_GetTypeAndFlags) {
    int fd = open(test_file, O_CREAT | O_RDWR | O_APPEND, 0644);
    ASSERT_NE(fd, -1);

    wasi_descriptor_type_t type;
    int err;
    wasi_filesystem_get_type(fd, &type, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(type, WASI_DESCRIPTOR_TYPE_REGULAR_FILE);

    wasi_descriptor_flags_t flags;
    wasi_filesystem_get_flags(fd, &flags, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_TRUE(flags & WASI_DESCRIPTOR_FLAGS_READ);
    ASSERT_TRUE(flags & WASI_DESCRIPTOR_FLAGS_WRITE);

    close(fd);
}

// Test getting the type of an invalid descriptor.
TEST_F(WasiP2FilesystemTest, Filesystem_GetTypeOnInvalidDescriptor) {
    wasi_descriptor_type_t type;
    int err;
    // Use an invalid file descriptor
    wasi_filesystem_get_type(-1, &type, &err);
    ASSERT_EQ(err, EBADF);
}

// Test setting the size of a file.
TEST_F(WasiP2FilesystemTest, Filesystem_SetSize) {
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    ASSERT_EQ(wasi_filesystem_set_size(fd, 123), WASI_ERROR_CODE_SUCCESS);

    struct stat st;
    ASSERT_EQ(fstat(fd, &st), 0);
    ASSERT_EQ(st.st_size, 123);

    close(fd);
}

// Test renaming a file.
TEST_F(WasiP2FilesystemTest, Filesystem_Rename) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);
    close(fd);

    ASSERT_EQ(wasi_filesystem_rename_at(dir_fd, "test_file.txt", dir_fd,
                                        "renamed.txt"),
              0);

    struct stat st;
    char renamed_path[128];
    snprintf(renamed_path, sizeof(renamed_path), "%s/renamed.txt", test_dir);
    ASSERT_EQ(stat(renamed_path, &st), 0);
    ASSERT_NE(stat(test_file, &st), 0);

    close(dir_fd);
}

// Test creating a symbolic link.
TEST_F(WasiP2FilesystemTest, Filesystem_Symlink) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    const char *old_path = "test_file.txt";
    const char *new_path = "symlink.txt";
    char new_path_full[256];
    snprintf(new_path_full, sizeof(new_path_full), "%s/%s", test_dir,
             new_path);

    ASSERT_EQ(wasi_filesystem_symlink_at(dir_fd, old_path, new_path),
              WASI_ERROR_CODE_SUCCESS);

    struct stat st;
    ASSERT_EQ(lstat(new_path_full, &st), 0);
    ASSERT_TRUE(S_ISLNK(st.st_mode));

    close(dir_fd);
}

// Test error code conversion.
TEST_F(WasiP2FilesystemTest, Filesystem_ErrorCode) {
    bool is_some;
    ASSERT_EQ(wasi_filesystem_error_code(0, &is_some), 0);
    ASSERT_FALSE(is_some);
    ASSERT_EQ(wasi_filesystem_error_code(EIO, &is_some),
              EIO);
    ASSERT_TRUE(is_some);
}

// Test file type conversion indirectly via stat.
TEST_F(WasiP2FilesystemTest, Filesystem_ConvertFileType) {
    // This is a static function, so we can't test it directly.
    // However, we can test it indirectly via `wasi_filesystem_stat`.
    int fd = open(test_file, O_CREAT | O_RDWR, 0644);
    ASSERT_NE(fd, -1);

    wasi_descriptor_stat_t stat;
    int err;

    wasi_filesystem_stat(fd, &stat, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(stat.type, WASI_DESCRIPTOR_TYPE_REGULAR_FILE);

    close(fd);

    char dir_path[128];
    snprintf(dir_path, sizeof(dir_path), "%s/test_dir", test_dir);
    mkdir(dir_path, 0755);
    fd = open(dir_path, O_RDONLY);
    ASSERT_NE(fd, -1);
    wasi_filesystem_stat(fd, &stat, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(stat.type, WASI_DESCRIPTOR_TYPE_DIRECTORY);
    close(fd);
}

// Test linking a non-existent file.
TEST_F(WasiP2FilesystemTest, Filesystem_LinkAtWithErrors) {
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    // linking a non-existent file should fail
    int err = wasi_filesystem_link_at(
        dir_fd, 0, "non_existent_file.txt", dir_fd, "test_link.txt");
    ASSERT_EQ(err, ENOENT);

    close(dir_fd);
}

// Test opening a file with specific permissions.
TEST_F(WasiP2FilesystemTest, Filesystem_OpenAtWithPermissions) {
    umask(0);
    int dir_fd = open(test_dir, O_RDONLY);
    ASSERT_NE(dir_fd, -1);

    wasi_descriptor_t file_fd;
    int err;

    wasi_filesystem_open_at(
        dir_fd, 0, "test_file.txt", WASI_OPEN_FLAGS_CREATE,
        WASI_DESCRIPTOR_FLAGS_READ | WASI_DESCRIPTOR_FLAGS_WRITE, 0666,
        &file_fd, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_GT(file_fd, 0);

    struct stat st;
    fstat(file_fd, &st);
    ASSERT_EQ(st.st_mode & 0666, 0666);

    ASSERT_EQ(wasi_filesystem_close(file_fd), WASI_ERROR_CODE_SUCCESS);

    close(dir_fd);
}