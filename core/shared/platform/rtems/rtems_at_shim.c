/*
 * Copyright (C) 2026 KylinSoft.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * RTEMS/newlib declares openat/fstatat/mkdirat/unlinkat/symlinkat/readlinkat/
 * linkat/renameat in its headers but does not provide the symbols, so libc-
 * wasi needs these shims.  They are implemented by switching the current
 * working directory with fchdir, which limits correctness when two different
 * dirfds are referenced in one call; that path is not exercised by libc-wasi.
 */

static int
enter_dir(int dirfd)
{
    if (dirfd == AT_FDCWD)
        return 0;

    return fchdir(dirfd);
}

int
openat(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    va_list ap;

    if (dirfd != AT_FDCWD) {
        if (fchdir(dirfd) < 0)
            return -1;
    }

    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }

    return open(path, flags, mode);
}

int
fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
    (void)flags;

    if (dirfd != AT_FDCWD) {
        if (fchdir(dirfd) < 0)
            return -1;
    }

    return stat(path, st);
}

int
mkdirat(int dirfd, const char *path, mode_t mode)
{
    if (dirfd != AT_FDCWD) {
        if (fchdir(dirfd) < 0)
            return -1;
    }

    return mkdir(path, mode);
}

int
unlinkat(int dirfd, const char *path, int flags)
{
    if (dirfd != AT_FDCWD) {
        if (fchdir(dirfd) < 0)
            return -1;
    }

    if (flags & AT_REMOVEDIR)
        return rmdir(path);

    return unlink(path);
}

int
symlinkat(const char *target, int dirfd, const char *linkpath)
{
    if (dirfd != AT_FDCWD) {
        if (fchdir(dirfd) < 0)
            return -1;
    }

    return symlink(target, linkpath);
}

ssize_t
readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz)
{
    if (dirfd != AT_FDCWD) {
        if (fchdir(dirfd) < 0)
            return -1;
    }

    return readlink(path, buf, bufsiz);
}

int
linkat(int dirfd, const char *path1, int newdirfd, const char *path2,
       int flags)
{
    (void)flags;

    if (dirfd != AT_FDCWD) {
        if (fchdir(dirfd) < 0)
            return -1;
    }

    if (newdirfd != dirfd && newdirfd != AT_FDCWD) {
        errno = ENOTSUP;
        return -1;
    }

    return link(path1, path2);
}

int
renameat(int dirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    if (dirfd != AT_FDCWD) {
        if (fchdir(dirfd) < 0)
            return -1;
    }

    if (newdirfd != dirfd && newdirfd != AT_FDCWD) {
        errno = ENOTSUP;
        return -1;
    }

    return rename(oldpath, newpath);
}
