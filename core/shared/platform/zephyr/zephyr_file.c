/*
 * Copyright (C) 2024 Grenoble INP - ESISAR.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "libc_errno.h"

#include <string.h>
#include <stdlib.h>
#include <zephyr/posix/fcntl.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_interface.h>
#include <zephyr/fs/fs_sys.h>
#include <zephyr/fs/littlefs.h>

/* Notes:
 * This is the implementation of a POSIX-like file system interface for Zephyr.
 * To manage our file descriptors, we created a struct `zephyr_fs_desc` that
 * represent a zephyr file descriptor and hold useful informations.
 * We also created a file descriptor table to keep track of all the file
 * descriptors.
 *
 * To pass the file descriptor reference to the higher level abstraction, we
 * pass the index of the fd table to an `os_file_handle` struct.
 * Then in the WASI implementation layer we can retrieve the file descriptor
 * reference.
 *
 * We also fake the stdin, stdout and stderr file descriptors.
 * We do not handle write on stdin and read on stdin, stdout and stderr.
 */

// No OS API wrapper (Zephyr):
// file:
//     off_t fs_tell(struct fs_file_t *zfp)
// file system:
//     int fs_mount(struct fs_mount_t *mp)
//     int fs_unmount(struct fs_mount_t *mp
//     int fs_readmount(int *index, const char **name)
//     int fs_statvfs(const char *path, struct fs_statvfs *stat)
//     int fs_mkfs(int fs_type, uintptr_t dev_id, void *cfg, int flags)
//     int fs_register(int type, const struct fs_file_system_t *fs)
//     int fs_unregister(int type, const struct fs_file_system_t *fs)

// We will take the maximum number of open files
// from the Zephyr POSIX configuration
#define CONFIG_WASI_MAX_OPEN_FILES CONFIG_ZVFS_OPEN_MAX

static inline bool
os_is_virtual_fd(int fd)
{
    switch (fd) {
        case STDIN_FILENO:
        case STDOUT_FILENO:
        case STDERR_FILENO:
            return true;
        default:
            return false;
    };
}

// Macro to retrieve a file system descriptor and check it's validity.
// fd's 0-2 are reserved for standard streams, so desc_array[i] is the fd
// i + STDERR_FILENO + 1.
#define GET_FILE_SYSTEM_DESCRIPTOR(fd, ptr)                            \
    do {                                                               \
        if (os_is_virtual_fd(fd)) {                                    \
            ptr = NULL;                                                \
            break;                                                     \
        }                                                              \
        if (fd < STDERR_FILENO + 1                                     \
            || fd >= CONFIG_WASI_MAX_OPEN_FILES + STDERR_FILENO + 1) { \
            return __WASI_EBADF;                                       \
        }                                                              \
        k_mutex_lock(&desc_array_mutex, K_FOREVER);                    \
        ptr = &desc_array[(int)fd - (STDERR_FILENO + 1)];              \
        if (!ptr->used) {                                              \
            k_mutex_unlock(&desc_array_mutex);                         \
            return __WASI_EBADF;                                       \
        }                                                              \
        k_mutex_unlock(&desc_array_mutex);                             \
    } while (0)

// Array to keep track of file system descriptors.
static struct zephyr_fs_desc desc_array[CONFIG_WASI_MAX_OPEN_FILES];

// mutex to protect the file descriptor array
K_MUTEX_DEFINE(desc_array_mutex);

/*
 * Build the absolute path of `path`, relative to the directory referred to
 * by `dir_handle`. Every directory descriptor keeps the absolute path it was
 * opened with, so a path is always resolved from its own directory: two
 * pre-opened directories (e.g. of two module instances) never share a base,
 * and a path relative to an opened subdirectory resolves inside it.
 *
 * Trailing slashes are removed from the result, so that it has the same form
 * as the paths kept in desc_array and can be compared with them. A trailing
 * slash means the path must name a directory: an existing file is rejected
 * here with ENOTDIR, since the file systems would ignore the slash. A path
 * that does not exist yet cannot be checked here: `trailing_slash`, if not
 * NULL, tells the caller that a slash was removed, so that it can refuse to
 * create or rename a regular file under that name.
 */
static __wasi_errno_t
build_absolute_path_ex(os_file_handle dir_handle, char *abs_path,
                       size_t abs_path_len, const char *path,
                       bool *trailing_slash)
{
    struct zephyr_fs_desc *ptr;
    struct fs_dirent entry;
    __wasi_errno_t err = __WASI_ESUCCESS;
    bool had_slash = false;
    size_t len;
    int fd, n;

    abs_path[0] = '\0';
    if (trailing_slash) {
        *trailing_slash = false;
    }

    if (!dir_handle || !path) {
        return __WASI_EINVAL;
    }

    fd = dir_handle->fd;
    if (os_is_virtual_fd(fd)) {
        return __WASI_ENOTDIR;
    }
    if (fd < STDERR_FILENO + 1
        || fd >= CONFIG_WASI_MAX_OPEN_FILES + STDERR_FILENO + 1) {
        return __WASI_EBADF;
    }

    /*
     * Keep the mutex held while reading ptr->path: a concurrent
     * os_renameat(), os_unlinkat() or os_close() may free it otherwise.
     */
    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    ptr = &desc_array[fd - (STDERR_FILENO + 1)];
    if (!ptr->used) {
        err = __WASI_EBADF;
    }
    else if (!ptr->is_dir || !ptr->path) {
        err = __WASI_ENOTDIR;
    }
    else {
        /* "." (or an empty path) names the directory itself */
        if (path[0] == '\0' || strcmp(path, ".") == 0) {
            n = snprintf(abs_path, abs_path_len, "%s", ptr->path);
        }
        else {
            n = snprintf(abs_path, abs_path_len, "%s/%s", ptr->path, path);
        }
        if (n < 0 || (size_t)n >= abs_path_len) {
            abs_path[0] = '\0';
            err = __WASI_ENAMETOOLONG;
        }
    }
    k_mutex_unlock(&desc_array_mutex);

    if (err != __WASI_ESUCCESS) {
        return err;
    }

    /* Keep the leading '/' of a mount point such as "/" */
    len = strlen(abs_path);
    while (len > 1 && abs_path[len - 1] == '/') {
        abs_path[--len] = '\0';
        had_slash = true;
    }

    if (had_slash && fs_stat(abs_path, &entry) == 0
        && entry.type != FS_DIR_ENTRY_DIR) {
        return __WASI_ENOTDIR;
    }

    if (trailing_slash) {
        *trailing_slash = had_slash;
    }

    return __WASI_ESUCCESS;
}

static inline __wasi_errno_t
build_absolute_path(os_file_handle dir_handle, char *abs_path,
                    size_t abs_path_len, const char *path)
{
    return build_absolute_path_ex(dir_handle, abs_path, abs_path_len, path,
                                  NULL);
}

static struct zephyr_fs_desc *
zephyr_fs_alloc_obj(bool is_dir, const char *path, int *index)
{
    struct zephyr_fs_desc *ptr = NULL;
    *index = -1; // give a default value to index in case table is full

    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    for (int i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        if (desc_array[i].used == false) {
            ptr = &desc_array[i];
            ptr->used = true;
            ptr->is_dir = is_dir;
            ptr->dir_index = 0;
            size_t path_len = strlen(path) + 1;
            ptr->path = BH_MALLOC(path_len);
            if (ptr->path == NULL) {
                ptr->used = false;
                k_mutex_unlock(&desc_array_mutex);
                return NULL;
            }
            strcpy(ptr->path, path);
            *index = i + STDERR_FILENO + 1;
            break;
        }
    }

    k_mutex_unlock(&desc_array_mutex);

    if (ptr == NULL) {
        printk("Error: all file descriptor slots are in use (max = %d)\n",
               CONFIG_WASI_MAX_OPEN_FILES);
    }

    return ptr;
}

static inline void
zephyr_fs_free_obj(struct zephyr_fs_desc *ptr)
{
    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    BH_FREE(ptr->path);
    ptr->path = NULL;
    ptr->used = false;
    k_mutex_unlock(&desc_array_mutex);
}

/* /!\ Needed for socket to work */
__wasi_errno_t
os_fstat(os_file_handle handle, struct __wasi_filestat_t *buf)
{
    struct zephyr_fs_desc *ptr = NULL;
    int socktype, rc;

    if (!handle->is_sock) {

        if (os_is_virtual_fd(handle->fd)) {
            buf->st_filetype = __WASI_FILETYPE_CHARACTER_DEVICE;
            buf->st_size = 0;
            buf->st_atim = 0;
            buf->st_mtim = 0;
            buf->st_ctim = 0;
            return __WASI_ESUCCESS;
        }

        GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

        // Get file information using Zephyr's fs_stat function
        struct fs_dirent entry;
        /* ptr->path may be replaced by a concurrent os_renameat() */
        k_mutex_lock(&desc_array_mutex, K_FOREVER);
        rc = fs_stat(ptr->path, &entry);
        k_mutex_unlock(&desc_array_mutex);
        if (rc < 0) {
            return convert_errno(-rc);
        }

        // Fill in the __wasi_filestat_t structure
        buf->st_dev = 0; // Zephyr's fs_stat doesn't provide a device ID
        buf->st_ino = 0; // Zephyr's fs_stat doesn't provide an inode number
        buf->st_filetype = entry.type == FS_DIR_ENTRY_DIR
                               ? __WASI_FILETYPE_DIRECTORY
                               : __WASI_FILETYPE_REGULAR_FILE;
        buf->st_nlink = 1; // Zephyr's fs_stat doesn't provide a link count
        buf->st_size = entry.size;
        buf->st_atim = 0; // Zephyr's fs_stat doesn't provide timestamps
        buf->st_mtim = 0;
        buf->st_ctim = 0;

        return __WASI_ESUCCESS;

        // return os_fstatat(handle, ptr->path, buf, 0);
    }
    else {
        // socklen_t socktypelen = sizeof(socktype);
        // rc = zsock_getsockopt(handle->fd, SOL_SOCKET, SO_TYPE, &socktype,
        // &socktypelen); Using `zsock_getsockopt` will add a dependency on the
        // network stack
        // TODO: may add a type to the `zephyr_handle`.
        rc = 1;
        socktype = SOCK_STREAM;
        if (rc < 0) {
            return convert_errno(-rc);
        }

        switch (socktype) {
            case SOCK_DGRAM:
                buf->st_filetype = __WASI_FILETYPE_SOCKET_DGRAM;
                break;
            case SOCK_STREAM:
                buf->st_filetype = __WASI_FILETYPE_SOCKET_STREAM;
                break;
            default:
                buf->st_filetype = __WASI_FILETYPE_UNKNOWN;
                break;
        }
        buf->st_size = 0;
        buf->st_atim = 0;
        buf->st_mtim = 0;
        buf->st_ctim = 0;
        return __WASI_ESUCCESS;
    }
}

__wasi_errno_t
os_fstatat(os_file_handle handle, const char *path,
           struct __wasi_filestat_t *buf, __wasi_lookupflags_t lookup_flags)
{
    struct fs_dirent entry;
    char abs_path[_POSIX_PATH_MAX];
    __wasi_errno_t err;
    int rc;

    err = build_absolute_path(handle, abs_path, sizeof(abs_path), path);
    if (err != __WASI_ESUCCESS) {
        return err;
    }

    // Get file information using Zephyr's fs_stat function
    rc = fs_stat(abs_path, &entry);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    // Fill in the __wasi_filestat_t structure
    buf->st_dev = 0; // Zephyr's fs_stat doesn't provide a device ID
    // DSK: setting this to 0, in addition to d_ino = 1 causes failures with
    // readdir() So, here's a hack to to avoid this.
    buf->st_ino = 1; // Zephyr's fs_stat doesn't provide an inode number.
    buf->st_filetype = entry.type == FS_DIR_ENTRY_DIR
                           ? __WASI_FILETYPE_DIRECTORY
                           : __WASI_FILETYPE_REGULAR_FILE;
    buf->st_nlink = 1; // Zephyr's fs_stat doesn't provide a link count
    buf->st_size = entry.size;
    buf->st_atim = 0; // Zephyr's fs_stat doesn't provide timestamps
    buf->st_mtim = 0;
    buf->st_ctim = 0;

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_file_get_fdflags(os_file_handle handle, __wasi_fdflags_t *flags)
{
    if (handle->is_sock) {
        *flags = 0;
        int f = fcntl(handle->fd, F_GETFL, 0);
        if (f < 0) {
            return __WASI_EBADF;
        }

        if (f & O_NONBLOCK) {
            *flags |= __WASI_FDFLAG_NONBLOCK;
        }

        return __WASI_ESUCCESS;
    }

    struct zephyr_fs_desc *ptr = NULL;
    if (os_is_virtual_fd(handle->fd)) {
        *flags = 0;
        return __WASI_ESUCCESS;
    }

    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    if ((ptr->file.flags & FS_O_APPEND) != 0) {
        *flags |= __WASI_FDFLAG_APPEND;
    }
    /* Others flags:
     *     - __WASI_FDFLAG_DSYNC
     *     - __WASI_FDFLAG_RSYNC
     *     - __WASI_FDFLAG_SYNC
     *     - __WASI_FDFLAG_NONBLOCK
     * Have no equivalent in Zephyr.
     */
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_file_set_fdflags(os_file_handle handle, __wasi_fdflags_t flags)
{
    if (handle->is_sock) {
        int f = fcntl(handle->fd, F_GETFL, 0);
        if (f < 0) {
            return __WASI_EBADF;
        }

        if (flags & __WASI_FDFLAG_NONBLOCK) {
            f |= O_NONBLOCK;
        }
        else {
            f &= ~O_NONBLOCK;
        }

        if (fcntl(handle->fd, F_SETFL, f) < 0) {
            return __WASI_EBADF;
        }

        return __WASI_ESUCCESS;
    }

    /* Regular file descriptor path */
    if (os_is_virtual_fd(handle->fd)) {
        return __WASI_ESUCCESS;
    }

    struct zephyr_fs_desc *ptr = NULL;

    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    if ((flags & __WASI_FDFLAG_APPEND) != 0) {
        ptr->file.flags |= FS_O_APPEND;
    }
    /* Same as above */
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fdatasync(os_file_handle handle)
{
    return os_fsync(handle);
}

__wasi_errno_t
os_fsync(os_file_handle handle)
{
    if (os_is_virtual_fd(handle->fd)) {
        return __WASI_ESUCCESS;
    }

    struct zephyr_fs_desc *ptr = NULL;
    int rc = 0;

    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    if (ptr->is_dir) {
        return __WASI_EISDIR;
    }

    rc = fs_sync(&ptr->file);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_open_preopendir(const char *path, os_file_handle *out)
{
    int rc, index;
    struct zephyr_fs_desc *ptr;

    *out = BH_MALLOC(sizeof(struct zephyr_handle));
    if (*out == NULL) {
        return __WASI_ENOMEM;
    }

    ptr = zephyr_fs_alloc_obj(true, path, &index);
    if (ptr == NULL) {
        BH_FREE(*out);
        return __WASI_EMFILE;
    }

    fs_dir_t_init(&ptr->dir);

    rc = fs_opendir(&ptr->dir, path);
    if (rc < 0) {
        zephyr_fs_free_obj(ptr);
        BH_FREE(*out);
        return convert_errno(-rc);
    }

    (*out)->fd = index;
    (*out)->is_sock = false;

    return __WASI_ESUCCESS;
}

static int
wasi_flags_to_zephyr(__wasi_oflags_t oflags, __wasi_fdflags_t fd_flags,
                     __wasi_lookupflags_t lookup_flags,
                     wasi_libc_file_access_mode access_mode)
{
    int mode = 0;

    // Convert open flags.
    if ((oflags & __WASI_O_CREAT) != 0) {
        mode |= FS_O_CREATE;
    }
    if (((oflags & __WASI_O_EXCL) != 0) || ((oflags & __WASI_O_TRUNC) != 0)
        || ((oflags & __WASI_O_DIRECTORY) != 0)) {
        /* Zephyr is not POSIX no equivalent for these flags */
        /* __WASI_O_DIRECTORY: Open shouldn't handle directories */
        // TODO: log warning
    }

    // Convert file descriptor flags.
    if ((fd_flags & __WASI_FDFLAG_APPEND) != 0) {
        mode |= FS_O_APPEND;
    }
    if (((fd_flags & __WASI_FDFLAG_DSYNC) != 0)
        || ((fd_flags & __WASI_FDFLAG_RSYNC) != 0)
        || ((fd_flags & __WASI_FDFLAG_SYNC) != 0)
        || ((fd_flags & __WASI_FDFLAG_NONBLOCK) != 0)) {
        /* Zephyr is not POSIX no equivalent for these flags */
        // TODO: log warning
    }

    // Convert lookup flag.
    if ((lookup_flags & __WASI_LOOKUP_SYMLINK_FOLLOW) == 0) {
        /* Zephyr is not POSIX no equivalent for these flags */
        // TODO: log warning
        return __WASI_ENOTSUP;
    }

    // Convert access mode.
    switch (access_mode) {
        case WASI_LIBC_ACCESS_MODE_READ_WRITE:
            mode |= FS_O_RDWR;
            break;
        case WASI_LIBC_ACCESS_MODE_READ_ONLY:
            mode |= FS_O_READ;
            break;
        case WASI_LIBC_ACCESS_MODE_WRITE_ONLY:
            mode |= FS_O_WRITE;
            break;
        default:
            // TODO: log warning
            break;
    }
    return mode;
}

__wasi_errno_t
os_openat(os_file_handle handle, const char *path, __wasi_oflags_t oflags,
          __wasi_fdflags_t fd_flags, __wasi_lookupflags_t lookup_flags,
          wasi_libc_file_access_mode access_mode, os_file_handle *out)
{
    /*
     * Zephyr has no openat(): `path` is resolved against the directory of
     * `handle` into an absolute path. We fill `out` with a new descriptor.
     */
    int rc, index;
    struct zephyr_fs_desc *ptr = NULL;
    char abs_path[_POSIX_PATH_MAX];
    struct fs_dirent entry;
    bool trailing_slash;
    __wasi_errno_t err;

    err = build_absolute_path_ex(handle, abs_path, sizeof(abs_path), path,
                                 &trailing_slash);
    if (err != __WASI_ESUCCESS) {
        return err;
    }

    // Treat directories as a special case
    bool is_dir = oflags & __WASI_O_DIRECTORY;

    /* "name/" cannot be created as a regular file */
    if (trailing_slash && !is_dir && (oflags & __WASI_O_CREAT)
        && fs_stat(abs_path, &entry) != 0) {
        return __WASI_EISDIR;
    }

    *out = BH_MALLOC(sizeof(struct zephyr_handle));
    if (*out == NULL) {
        return __WASI_ENOMEM;
    }

    ptr = zephyr_fs_alloc_obj(is_dir, abs_path, &index);
    if (!ptr && (index < 0)) {
        BH_FREE(*out);
        return __WASI_EMFILE;
    }

    if (is_dir) {
        fs_dir_t_init(&ptr->dir);
        // fs_opendir() is called in libc later - don't call here
    }
    else {
        // Is a file
        int zmode =
            wasi_flags_to_zephyr(oflags, fd_flags, lookup_flags, access_mode);
        fs_file_t_init(&ptr->file);
        rc = fs_open(&ptr->file, abs_path, zmode);

        if (rc < 0) {
            zephyr_fs_free_obj(ptr);
            BH_FREE(*out);
            return convert_errno(-rc);
        }
    }

    (*out)->fd = index;
    (*out)->is_sock = false;

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_file_get_access_mode(os_file_handle handle,
                        wasi_libc_file_access_mode *access_mode)
{
    // Sockets must be classified before the stdio fd-number checks below.
    // Zephyr only reserves fdtable slots 0/1/2 for stdin/stdout/stderr when
    // CONFIG_POSIX_DEVICE_IO is enabled. Without it, zsock_socket() hands out
    // the lowest free descriptor, so a socket can legitimately own raw fd 0,
    // 1 or 2. Testing the fd number first would then mark that socket
    // read-only (fd 0) or write-only (fd 1/2), and fd_determine_type_rights()
    // strips __WASI_RIGHT_FD_WRITE / __WASI_RIGHT_FD_READ accordingly. The
    // result is that send()/sendto() or recv()/recvfrom() on an otherwise
    // valid socket fail with __WASI_ENOTCAPABLE. Sockets are always
    // bidirectional, so answer from is_sock before looking at the fd number.
    if (handle->is_sock) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;
        return __WASI_ESUCCESS;
    }

    if (handle->fd == STDIN_FILENO) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_ONLY;
        return __WASI_ESUCCESS;
    }
    else if (handle->fd == STDOUT_FILENO || handle->fd == STDERR_FILENO) {
        *access_mode = WASI_LIBC_ACCESS_MODE_WRITE_ONLY;
        return __WASI_ESUCCESS;
    }

    struct zephyr_fs_desc *ptr = NULL;

    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    if (ptr->is_dir) {
        // DSK: is this actually the correct mode for a dir?
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;
        return __WASI_ESUCCESS;
    }

    if ((ptr->file.flags & FS_O_RDWR) != 0) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;
    }
    else if ((ptr->file.flags & FS_O_READ) != 0) {
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_ONLY;
    }
    else if ((ptr->file.flags & FS_O_WRITE) != 0) {
        *access_mode = WASI_LIBC_ACCESS_MODE_WRITE_ONLY;
    }
    else {
        // we return read/write by default
        *access_mode = WASI_LIBC_ACCESS_MODE_READ_WRITE;
    }
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_close(os_file_handle handle, bool is_stdio)
{
    int rc;
    struct zephyr_fs_desc *ptr = NULL;

    if (is_stdio)
        return __WASI_ESUCCESS;

    if (handle->is_sock) {
        rc = zsock_close(handle->fd);
    }
    // Handle is assumed to be a file descriptor
    else {
        GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

        rc = ptr->is_dir ? fs_closedir(&ptr->dir) : fs_close(&ptr->file);
        zephyr_fs_free_obj(ptr); // free in any case.
    }

    BH_FREE(handle);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_preadv(os_file_handle handle, const struct __wasi_iovec_t *iov, int iovcnt,
          __wasi_filesize_t offset, size_t *nread)
{
    if (handle->fd == STDIN_FILENO) {
        return __WASI_ENOSYS;
    }

    struct zephyr_fs_desc *ptr = NULL;
    int rc;
    ssize_t total_read = 0;

    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    // Seek to the offset
    rc = fs_seek(&ptr->file, offset, FS_SEEK_SET);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    // Read data into each buffer
    for (int i = 0; i < iovcnt; i++) {
        ssize_t bytes_read = fs_read(&ptr->file, iov[i].buf, iov[i].buf_len);
        if (bytes_read < 0) {
            return convert_errno(-bytes_read);
        }

        total_read += bytes_read;

        /*  If we read less than we asked for, stop reading
            bytes_read being a non-negative value was already checked */
        if ((size_t)bytes_read < iov[i].buf_len) {
            break;
        }
    }

    *nread = total_read;

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_pwritev(os_file_handle handle, const struct __wasi_ciovec_t *iov, int iovcnt,
           __wasi_filesize_t offset, size_t *nwritten)
{
    struct zephyr_fs_desc *ptr = NULL;
    ssize_t total_written = 0;

    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    // Seek to the offset
    int rc = fs_seek(&ptr->file, offset, FS_SEEK_SET);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    // Write data from each buffer
    for (int i = 0; i < iovcnt; i++) {
        ssize_t bytes_written =
            fs_write(&ptr->file, iov[i].buf, iov[i].buf_len);
        if (bytes_written < 0) {
            return convert_errno(-bytes_written);
        }

        total_written += bytes_written;

        /*  If we wrote less than we asked for, stop writing
            bytes_read being a non-negative value was already checked */
        if ((size_t)bytes_written < iov[i].buf_len) {
            break;
        }
    }

    *nwritten = total_written;

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_readv(os_file_handle handle, const struct __wasi_iovec_t *iov, int iovcnt,
         size_t *nread)
{
    struct zephyr_fs_desc *ptr = NULL;
    ssize_t total_read = 0;

    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    // Read data into each buffer
    for (int i = 0; i < iovcnt; i++) {
        ssize_t bytes_read = fs_read(&ptr->file, iov[i].buf, iov[i].buf_len);
        if (bytes_read < 0) {
            // If an error occurred, return it
            return convert_errno(-bytes_read);
        }

        total_read += bytes_read;

        /*  If we read less than we asked for, stop reading
            bytes_read being a non-negative value was already checked */
        if ((size_t)bytes_read < iov[i].buf_len) {
            break;
        }
    }

    *nread = total_read;

    return __WASI_ESUCCESS;
}

/* With wasi-libc we need to redirect write on stdout/err to printf */
// TODO: handle write on stdin
__wasi_errno_t
os_writev(os_file_handle handle, const struct __wasi_ciovec_t *iov, int iovcnt,
          size_t *nwritten)
{
    ssize_t total_written = 0;

    if (os_is_virtual_fd(handle->fd)) {
        FILE *fd = (handle->fd == STDERR_FILENO) ? stderr : stdout;
        for (int i = 0; i < iovcnt; i++) {
            ssize_t bytes_written = fwrite(iov[i].buf, 1, iov[i].buf_len, fd);
            if (bytes_written < 0)
                return convert_errno(-bytes_written);
            total_written += bytes_written;
        }

        *nwritten = total_written;
        return __WASI_ESUCCESS;
    }

    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    // Write data from each buffer
    for (int i = 0; i < iovcnt; i++) {
        ssize_t bytes_written =
            fs_write(&ptr->file, iov[i].buf, iov[i].buf_len);
        if (bytes_written < 0) {
            return convert_errno(-bytes_written);
        }

        total_written += bytes_written;

        /*  If we wrote less than we asked for, stop writing
            bytes_read being a non-negative value was already checked */
        if ((size_t)bytes_written < iov[i].buf_len) {
            break;
        }
    }

    *nwritten = total_written;

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fallocate(os_file_handle handle, __wasi_filesize_t offset,
             __wasi_filesize_t length)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_ftruncate(os_file_handle handle, __wasi_filesize_t size)
{

    if (os_is_virtual_fd(handle->fd)) {
        return __WASI_EINVAL;
    }

    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    int rc = fs_truncate(&ptr->file, (off_t)size);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_futimens(os_file_handle handle, __wasi_timestamp_t access_time,
            __wasi_timestamp_t modification_time, __wasi_fstflags_t fstflags)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_utimensat(os_file_handle handle, const char *path,
             __wasi_timestamp_t access_time,
             __wasi_timestamp_t modification_time, __wasi_fstflags_t fstflags,
             __wasi_lookupflags_t lookup_flags)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_readlinkat(os_file_handle handle, const char *path, char *buf,
              size_t bufsize, size_t *nread)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_linkat(os_file_handle from_handle, const char *from_path,
          os_file_handle to_handle, const char *to_path,
          __wasi_lookupflags_t lookup_flags)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_symlinkat(const char *old_path, os_file_handle handle, const char *new_path)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_mkdirat(os_file_handle handle, const char *path)
{
    char abs_path[_POSIX_PATH_MAX];
    __wasi_errno_t err;
    int rc;

    /*
     * `handle` is the directory that will contain the new one. mkdirat()
     * creates no descriptor: the new directory is opened later through
     * openat(), so `handle` must be left untouched.
     */
    err = build_absolute_path(handle, abs_path, sizeof(abs_path), path);
    if (err != __WASI_ESUCCESS) {
        return err;
    }

    rc = fs_mkdir(abs_path);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    return __WASI_ESUCCESS;
}

/*
 * Return true if a descriptor is open on exactly `path`. If `files_only` is
 * set, only regular files count. The caller must hold desc_array_mutex.
 */
static bool
desc_path_is_open(const char *path, bool files_only)
{
    for (int i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        struct zephyr_fs_desc *ptr = &desc_array[i];

        if (ptr->used && ptr->path && strcmp(ptr->path, path) == 0
            && !(files_only && ptr->is_dir)) {
            return true;
        }
    }
    return false;
}

/*
 * Allocate in `copies` the new path of every descriptor equal to `old_path`
 * or located below it, with the `old_path` prefix replaced by `new_path`.
 * Nothing in the table changes: rename_desc_paths_commit() installs the
 * copies once the rename has succeeded, rename_desc_paths_discard() frees
 * them otherwise. The caller must hold desc_array_mutex until then.
 */
static __wasi_errno_t
rename_desc_paths_prepare(const char *old_path, const char *new_path,
                          char *copies[CONFIG_WASI_MAX_OPEN_FILES])
{
    size_t old_len = strlen(old_path);
    size_t new_len = strlen(new_path);
    int i;

    for (i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        copies[i] = NULL;
    }

    for (i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        struct zephyr_fs_desc *ptr = &desc_array[i];
        const char *tail;

        if (!ptr->used || !ptr->path
            || strncmp(ptr->path, old_path, old_len) != 0
            || (ptr->path[old_len] != '/' && ptr->path[old_len] != '\0')) {
            continue;
        }

        tail = ptr->path + old_len;
        copies[i] = BH_MALLOC(new_len + strlen(tail) + 1);
        if (copies[i] == NULL) {
            for (int j = 0; j < i; j++) {
                if (copies[j]) {
                    BH_FREE(copies[j]);
                }
            }
            return __WASI_ENOMEM;
        }
        memcpy(copies[i], new_path, new_len);
        strcpy(copies[i] + new_len, tail);
    }

    return __WASI_ESUCCESS;
}

static void
rename_desc_paths_commit(char *copies[CONFIG_WASI_MAX_OPEN_FILES])
{
    for (int i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        if (copies[i]) {
            BH_FREE(desc_array[i].path);
            desc_array[i].path = copies[i];
        }
    }
}

static void
rename_desc_paths_discard(char *copies[CONFIG_WASI_MAX_OPEN_FILES])
{
    for (int i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        if (copies[i]) {
            BH_FREE(copies[i]);
        }
    }
}

/*
 * `old_handle` and `new_handle` are the directories that contain
 * `old_path` and `new_path` (renameat semantics).
 *
 * Renaming an open file, or over an open file or directory, is refused with
 * EBUSY: FatFs (FF_FS_LOCK is 0) would later write the size of the open file
 * into a stale directory entry, and Zephyr removes the destination first.
 * Renaming a directory that is open, or a parent of open descriptors, is
 * allowed: their paths are updated below.
 */
__wasi_errno_t
os_renameat(os_file_handle old_handle, const char *old_path,
            os_file_handle new_handle, const char *new_path)
{
    char abs_old_path[_POSIX_PATH_MAX];
    char abs_new_path[_POSIX_PATH_MAX];
    char *copies[CONFIG_WASI_MAX_OPEN_FILES];
    struct fs_dirent entry;
    bool old_slash, new_slash;
    __wasi_errno_t err;
    int rc;

    err = build_absolute_path_ex(old_handle, abs_old_path,
                                 sizeof(abs_old_path), old_path, &old_slash);
    if (err != __WASI_ESUCCESS) {
        return err;
    }

    err = build_absolute_path_ex(new_handle, abs_new_path,
                                 sizeof(abs_new_path), new_path, &new_slash);
    if (err != __WASI_ESUCCESS) {
        return err;
    }

    /* A trailing slash on either name requires a directory source */
    if ((old_slash || new_slash) && fs_stat(abs_old_path, &entry) == 0
        && entry.type != FS_DIR_ENTRY_DIR) {
        return __WASI_ENOTDIR;
    }

    /*
     * Every descriptor whose path is abs_old_path, or lies below it, must
     * follow the rename, since its path is the base of later *at calls.
     * Their new paths are allocated before touching the disk, so that an
     * ENOMEM leaves both the disk and the table unchanged. The mutex is held
     * across fs_rename() so that no slot is freed or reused in between.
     */
    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    if (desc_path_is_open(abs_old_path, true)
        || desc_path_is_open(abs_new_path, false)) {
        k_mutex_unlock(&desc_array_mutex);
        return __WASI_EBUSY;
    }

    err = rename_desc_paths_prepare(abs_old_path, abs_new_path, copies);
    if (err != __WASI_ESUCCESS) {
        k_mutex_unlock(&desc_array_mutex);
        return err;
    }

    rc = fs_rename(abs_old_path, abs_new_path);
    if (rc < 0) {
        rename_desc_paths_discard(copies);
        k_mutex_unlock(&desc_array_mutex);
        return convert_errno(-rc);
    }

    rename_desc_paths_commit(copies);
    k_mutex_unlock(&desc_array_mutex);

    return __WASI_ESUCCESS;
}

/*
 * `handle` is the directory that contains `path` (unlinkat semantics).
 * fs_unlink() removes files and empty directories alike, so the type is
 * checked first to keep the path_unlink_file / path_remove_directory split.
 *
 * Removing a file or directory that is still open is refused with EBUSY.
 * Dropping the descriptor instead would leak the Zephyr file or directory
 * object (only CONFIG_FS_*_NUM_FILES/NUM_DIRS of them exist) and make a
 * later close() of that fd fail, or close another file reusing the slot.
 * FatFs is also unsafe on an open file, since FF_FS_LOCK is 0.
 */
__wasi_errno_t
os_unlinkat(os_file_handle handle, const char *path, bool is_dir)
{
    char abs_path[_POSIX_PATH_MAX];
    struct fs_dirent entry;
    __wasi_errno_t err;
    int rc;

    err = build_absolute_path(handle, abs_path, sizeof(abs_path), path);
    if (err != __WASI_ESUCCESS) {
        return err;
    }

    rc = fs_stat(abs_path, &entry);
    if (rc < 0) {
        return convert_errno(-rc);
    }
    if (is_dir && entry.type != FS_DIR_ENTRY_DIR) {
        return __WASI_ENOTDIR;
    }
    if (!is_dir && entry.type == FS_DIR_ENTRY_DIR) {
        return __WASI_EISDIR;
    }

    /* Held across fs_unlink() so that no open() can slip in after the check */
    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    if (desc_path_is_open(abs_path, false)) {
        k_mutex_unlock(&desc_array_mutex);
        return __WASI_EBUSY;
    }
    rc = fs_unlink(abs_path);
    k_mutex_unlock(&desc_array_mutex);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_lseek(os_file_handle handle, __wasi_filedelta_t offset,
         __wasi_whence_t whence, __wasi_filesize_t *new_offset)
{

    if (os_is_virtual_fd(handle->fd)) {
        return __WASI_ESPIPE; // Seeking not supported on character streams
    }

    struct zephyr_fs_desc *ptr = NULL;
    int zwhence;

    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);

    // They have the same value but this is more explicit
    switch (whence) {
        case __WASI_WHENCE_SET:
            zwhence = FS_SEEK_SET;
            break;
        case __WASI_WHENCE_CUR:
            zwhence = FS_SEEK_CUR;
            break;
        case __WASI_WHENCE_END:
            zwhence = FS_SEEK_END;
            break;
        default:
            return __WASI_EINVAL;
    }

    int rc = fs_seek(&ptr->file, (off_t)offset, zwhence);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    // fs_seek() returns 0 on success, not the new position: ask for it.
    off_t pos = fs_tell(&ptr->file);
    if (pos < 0) {
        return convert_errno((int)-pos);
    }

    *new_offset = (__wasi_filesize_t)pos;

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_fadvise(os_file_handle handle, __wasi_filesize_t offset,
           __wasi_filesize_t length, __wasi_advice_t advice)
{
    return __WASI_ENOSYS;
}

__wasi_errno_t
os_isatty(os_file_handle handle)
{
    if (os_is_virtual_fd(handle->fd)) {
        return __WASI_ESUCCESS;
    }

    return __WASI_ENOTTY;
}

os_file_handle
os_convert_stdin_handle(os_raw_file_handle raw_stdin)
{
    os_file_handle handle = BH_MALLOC(sizeof(struct zephyr_handle));
    if (!handle)
        return NULL;

    handle->fd = STDIN_FILENO;
    handle->is_sock = false;
    return handle;
}

os_file_handle
os_convert_stdout_handle(os_raw_file_handle raw_stdout)
{
    os_file_handle handle = BH_MALLOC(sizeof(struct zephyr_handle));
    if (!handle)
        return NULL;

    handle->fd = STDOUT_FILENO;
    handle->is_sock = false;
    return handle;
}

os_file_handle
os_convert_stderr_handle(os_raw_file_handle raw_stderr)
{
    os_file_handle handle = BH_MALLOC(sizeof(struct zephyr_handle));
    if (!handle)
        return NULL;

    handle->fd = STDERR_FILENO;
    handle->is_sock = false;
    return handle;
}

__wasi_errno_t
os_fdopendir(os_file_handle handle, os_dir_stream *dir_stream)
{
    /* Here we assume that either mdkdir or preopendir was called
     * before otherwise function will fail.
     */
    struct zephyr_fs_desc *ptr = NULL;

    GET_FILE_SYSTEM_DESCRIPTOR(handle->fd, ptr);
    if (!ptr->is_dir) {
        return __WASI_ENOTDIR;
    }

    /* ptr->path may be replaced by a concurrent os_renameat() */
    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    int rc = fs_opendir(&ptr->dir, ptr->path);
    k_mutex_unlock(&desc_array_mutex);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    /* we store the fd in the `os_dir_stream` to use other function */
    *dir_stream = handle->fd;

    return __WASI_ESUCCESS;
}

// DSK: simple open and close to rewind index.
__wasi_errno_t
os_rewinddir(os_dir_stream dir_stream)
{
    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(dir_stream, ptr);

    if (!ptr->is_dir)
        return __WASI_ENOTDIR;

    int rc = fs_closedir(&ptr->dir); // Close current stream
    if (rc < 0)
        return convert_errno(-rc);

    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    rc = fs_opendir(&ptr->dir, ptr->path); // Reopen from start
    k_mutex_unlock(&desc_array_mutex);
    if (rc < 0)
        return convert_errno(-rc);

    ptr->dir_index = 0; // Reset virtual position tracker
    return __WASI_ESUCCESS;
}

// DSK: start from 0 and linear seek since there's no cookies in the zephyr fs
// TODO: duplicated code with rewinddir
__wasi_errno_t
os_seekdir(os_dir_stream dir_stream, __wasi_dircookie_t position)
{
    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(dir_stream, ptr);

    if (!ptr->is_dir)
        return __WASI_ENOTDIR;

    int rc = fs_closedir(&ptr->dir);
    if (rc < 0)
        return convert_errno(-rc);

    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    rc = fs_opendir(&ptr->dir, ptr->path);
    k_mutex_unlock(&desc_array_mutex);
    if (rc < 0)
        return convert_errno(-rc);

    // Emulate seek by re-reading entries up to 'position'
    struct fs_dirent tmp;
    for (__wasi_dircookie_t i = 0; i < position; i++) {
        rc = fs_readdir(&ptr->dir, &tmp);
        if (rc < 0)
            return convert_errno(-rc);
        if (tmp.name[0] == '\0')
            break; // End of directory
    }

    ptr->dir_index = position;
    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_readdir(os_dir_stream dir_stream, __wasi_dirent_t *entry,
           const char **d_name)
{
    struct fs_dirent fs_entry;
    struct zephyr_fs_desc *ptr = NULL;
    GET_FILE_SYSTEM_DESCRIPTOR(dir_stream, ptr);
    if (!ptr->is_dir) {
        return __WASI_ENOTDIR;
    }

    int rc = fs_readdir(&ptr->dir, &fs_entry);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    if (fs_entry.name[0] == '\0') {
        // DSK: the caller expects the name buffer to be null
        // when we've reached the end of the directory.
        *d_name = NULL;
        return __WASI_ESUCCESS;
    }

    // DSK: emulated increasing value for rewinddir and seekdir
    entry->d_next = ++ptr->dir_index;

    // DSK: A hack to get readdir working. This needs to be non-zero along with
    // st_ino for the libc side of readdir to work correctly.
    entry->d_ino = 1 + ptr->dir_index;

    entry->d_namlen = strlen(fs_entry.name);
    entry->d_type = fs_entry.type == FS_DIR_ENTRY_DIR
                        ? __WASI_FILETYPE_DIRECTORY
                        : __WASI_FILETYPE_REGULAR_FILE;

    // DSK: name exists in fs_entry and we need to return it
    static char name_buf[MAX_FILE_NAME + 1];
    strncpy(name_buf, fs_entry.name, MAX_FILE_NAME);
    name_buf[MAX_FILE_NAME] = '\0';
    *d_name = name_buf;

    return __WASI_ESUCCESS;
}

__wasi_errno_t
os_closedir(os_dir_stream dir_stream)
{
    struct zephyr_fs_desc *ptr = NULL;

    GET_FILE_SYSTEM_DESCRIPTOR(dir_stream, ptr);
    if (!ptr->is_dir) {
        return __WASI_ENOTDIR;
    }

    int rc = fs_closedir(&ptr->dir);
    zephyr_fs_free_obj(ptr); // free in any case.
    if (rc < 0) {
        return convert_errno(-rc);
    }

    return __WASI_ESUCCESS;
}

os_dir_stream
os_get_invalid_dir_stream()
{
    return OS_DIR_STREAM_INVALID;
}

bool
os_is_dir_stream_valid(os_dir_stream *dir_stream)
{
    // DSK: this probably needs a check...
    return false;
}

bool
os_is_handle_valid(os_file_handle *handle)
{
    if (handle == NULL || *handle == NULL) {
        return false;
    }
    return (*handle)->fd > -1;
}

char *
os_realpath(const char *path, char *resolved_path)
{
    /* In fact we could implement a path resolving method, because every paths
     * are at one point put into memory.
     * We could then maintain a 'tree' to represent the file system.
     *    --> The file system root is easily accessable with:
     *            * (fs_dir_t) dir.mp->mnt_point
     *            * (fs_file_t) file.mp->mnt_point
     * But we will just use absolute path for now.
     */
    if ((!path) || (strlen(path) > PATH_MAX)) {
        // Invalid input, path has to be valid and less than PATH_MAX
        return NULL;
    }

    return strncpy(resolved_path, path, PATH_MAX);
}

bool
os_compare_file_handle(os_file_handle handle1, os_file_handle handle2)
{
    return handle1->fd == handle2->fd && handle1->is_sock == handle2->is_sock;
}

bool
os_is_stdin_handle(os_file_handle handle)
{
    return (handle == (os_file_handle)stdin);
}

bool
os_is_stdout_handle(os_file_handle handle)
{
    return (handle == (os_file_handle)stdout);
}

bool
os_is_stderr_handle(os_file_handle handle)
{
    return (handle == (os_file_handle)stderr);
}
