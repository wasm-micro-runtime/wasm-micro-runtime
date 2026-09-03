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
// fd's 0-2 are reserved for standard streams, hence the by-3 offsets.
#define GET_FILE_SYSTEM_DESCRIPTOR(fd, ptr)                   \
    do {                                                      \
        if (os_is_virtual_fd(fd)) {                           \
            ptr = NULL;                                       \
            break;                                            \
        }                                                     \
        if (fd < 3 || fd >= CONFIG_WASI_MAX_OPEN_FILES + 3) { \
            return __WASI_EBADF;                              \
        }                                                     \
        k_mutex_lock(&desc_array_mutex, K_FOREVER);           \
        ptr = &desc_array[(int)fd - 3];                       \
        if (!ptr->used) {                                     \
            k_mutex_unlock(&desc_array_mutex);                \
            return __WASI_EBADF;                              \
        }                                                     \
        k_mutex_unlock(&desc_array_mutex);                    \
    } while (0)

// Array to keep track of file system descriptors.
static struct zephyr_fs_desc desc_array[CONFIG_WASI_MAX_OPEN_FILES];

// mutex to protect the file descriptor array
K_MUTEX_DEFINE(desc_array_mutex);

static char prestat_dir[MAX_FILE_NAME + 1];

bool
build_absolute_path(char *abs_path, size_t abs_path_len, const char *path)
{
    if (!path) {
        abs_path[0] = '\0';
        return false;
    }

    size_t len1 = strlen(prestat_dir);
    size_t len2 = strlen(path);

    if (len1 + 1 + len2 + 1 > abs_path_len) {
        abs_path[0] = '\0'; // Empty string on error
        return false;       // Truncation would occur
    }

    snprintf(abs_path, abs_path_len, "%s/%s", prestat_dir, path);
    return true;
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
            *index = i + 3;
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
    BH_FREE(ptr->path);
    ptr->path = NULL;
    ptr->used = false;
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
        rc = fs_stat(ptr->path, &entry);
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
    int rc;

    if (handle->fd < 0) {
        return __WASI_EBADF;
    }

    char abs_path[MAX_FILE_NAME + 1];

    if (handle == NULL) {
        return __WASI_EINVAL; // Or another appropriate error code
    }

    if (!build_absolute_path(abs_path, sizeof(abs_path), path)) {
        return __WASI_ENOMEM;
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

    strncpy(prestat_dir, path, MAX_FILE_NAME + 1);
    prestat_dir[MAX_FILE_NAME] = '\0';

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
     * `handle` will be unused because zephyr doesn't expose an openat
     * function and don't seem to have the concept of relative path.
     * We fill `out` with a new file descriptor.
     */
    int rc, index;
    struct zephyr_fs_desc *ptr = NULL;
    char abs_path[MAX_FILE_NAME + 1];

    *out = BH_MALLOC(sizeof(struct zephyr_handle));
    if (*out == NULL) {
        return __WASI_ENOMEM;
    }

    if (!build_absolute_path(abs_path, sizeof(abs_path), path)) {
        BH_FREE(*out);
        return __WASI_ENOMEM;
    }

    // Treat directories as a special case
    bool is_dir = oflags & __WASI_O_DIRECTORY;

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
    struct zephyr_fs_desc *ptr = NULL;
    int index, rc;
    char abs_path[MAX_FILE_NAME + 1];

    if (handle == NULL) {
        return __WASI_EINVAL; // Or another appropriate error code
    }

    if (!build_absolute_path(abs_path, sizeof(abs_path), path)) {
        return __WASI_ENOMEM;
    }

    rc = fs_mkdir(abs_path);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    ptr = zephyr_fs_alloc_obj(true, abs_path, &index);
    if (!ptr) {
        fs_unlink(abs_path);
        return __WASI_EMFILE;
    }
    fs_dir_t_init(&ptr->dir);

    handle->fd = index;
    handle->is_sock = false;

    return __WASI_ESUCCESS;
}

// DSK: Somewhere along the WASI libc implementation path, the knowledge
// was lost that `old_handle` and `new_handle` refer to directories that
// contain the files to be renamed, rather than the file fds themselves:
//
// __wasilibc_nocwd_renameat(old_dirfd, old_relative_path,
//                           new_dirfd, new_relative_path);
//
// Therefore we won't mess with the supplied fd's, and work only off
// of the supplied paths. Note: this will change when more than one
// pre-opened dir is supported in the future.
__wasi_errno_t
os_renameat(os_file_handle old_handle, const char *old_path,
            os_file_handle new_handle, const char *new_path)
{
    // directories, safe to ignore
    (void)old_handle;
    (void)new_handle;

    char abs_old_path[MAX_FILE_NAME + 1];
    char abs_new_path[MAX_FILE_NAME + 1];

    if (!build_absolute_path(abs_old_path, sizeof(abs_old_path), old_path)) {
        return __WASI_ENOMEM;
    }

    if (!build_absolute_path(abs_new_path, sizeof(abs_new_path), new_path)) {
        return __WASI_ENOMEM;
    }

    // Attempt to perform the rename
    int rc = fs_rename(abs_old_path, abs_new_path);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    // If there is an allocated fd in our table, update the descriptor table
    // entry DSK: better approach here?
    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    for (int i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        struct zephyr_fs_desc *ptr = &desc_array[i];
        if (ptr->used && ptr->path && strcmp(ptr->path, abs_old_path) == 0) {
            size_t new_path_len = strlen(abs_new_path) + 1;
            char *new_path_copy = BH_MALLOC(new_path_len);
            if (new_path_copy != NULL) {
                strcpy(new_path_copy, abs_new_path);
                BH_FREE(ptr->path);
                ptr->path = new_path_copy;
            }
            else {
                k_mutex_unlock(&desc_array_mutex);
                return __WASI_ENOMEM;
            }
            break; // Only one descriptor should match
        }
    }
    k_mutex_unlock(&desc_array_mutex);

    return __WASI_ESUCCESS;
}

// DSK: Same thing as renameat: `handle` refers to the containing directory,
// not the file handle to unlink. We ignore the handle and use the path
// exclusively.
//
// TODO: is there anything we need to do in case is_dir=true?
__wasi_errno_t
os_unlinkat(os_file_handle handle, const char *path, bool is_dir)
{
    (void)handle;

    char abs_path[MAX_FILE_NAME + 1];

    if (!build_absolute_path(abs_path, sizeof(abs_path), path)) {
        return __WASI_ENOMEM;
    }

    int rc = fs_unlink(abs_path);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    // Search for any active descriptor referencing this path and free it.
    k_mutex_lock(&desc_array_mutex, K_FOREVER);
    for (int i = 0; i < CONFIG_WASI_MAX_OPEN_FILES; i++) {
        struct zephyr_fs_desc *ptr = &desc_array[i];
        if (ptr->used && ptr->path && strcmp(ptr->path, abs_path) == 0) {
            zephyr_fs_free_obj(ptr);
            break;
        }
    }
    k_mutex_unlock(&desc_array_mutex);

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

    off_t rc = fs_seek(&ptr->file, (off_t)offset, zwhence);
    if (rc < 0) {
        return convert_errno(-rc);
    }

    *new_offset = (__wasi_filesize_t)rc;

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

    int rc = fs_opendir(&ptr->dir, ptr->path);
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

    rc = fs_opendir(&ptr->dir, ptr->path); // Reopen from start
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

    rc = fs_opendir(&ptr->dir, ptr->path);
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
