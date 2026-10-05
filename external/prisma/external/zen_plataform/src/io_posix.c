#define _FILE_OFFSET_BITS 64

#include "io_internal.h"
#include "error_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#define IO_CHUNK_MAX ((size_t)0x7ffff000)

static int64_t fd_size(IoStream *s)
{
    struct stat st;
    if (fstat(s->u.fd, &st) != 0)
    {
        error_set("fstat failed: %s", strerror(errno));
        return -1;
    }
    return (int64_t)st.st_size;
}

static int64_t fd_seek(IoStream *s, int64_t offset, IoWhence whence)
{
    static const int k_whence[] = {SEEK_SET, SEEK_CUR, SEEK_END};
    off_t pos = lseek(s->u.fd, (off_t)offset, k_whence[whence]);
    if (pos < 0)
    {
        error_set("seek failed: %s", strerror(errno));
        return -1;
    }
    return (int64_t)pos;
}

static size_t fd_read(IoStream *s, void *dst, size_t n)
{
    uint8_t *p = dst;
    size_t done = 0;
    while (done < n)
    {
        size_t chunk = n - done < IO_CHUNK_MAX ? n - done : IO_CHUNK_MAX;
        ssize_t r = read(s->u.fd, p + done, chunk);
        if (r > 0)
        {
            done += (size_t)r;
            continue;
        }
        if (r == 0)
        {
            s->eof = true;
            break;
        }
        if (errno == EINTR)
            continue;
        error_set("read failed: %s", strerror(errno));
        break;
    }
    return done;
}

static size_t fd_write(IoStream *s, const void *src, size_t n)
{
    const uint8_t *p = src;
    size_t done = 0;
    while (done < n)
    {
        size_t chunk = n - done < IO_CHUNK_MAX ? n - done : IO_CHUNK_MAX;
        ssize_t r = write(s->u.fd, p + done, chunk);
        if (r > 0)
        {
            done += (size_t)r;
            continue;
        }
        if (r < 0 && errno == EINTR)
            continue;
        error_set("write failed: %s", r < 0 ? strerror(errno) : "no progress");
        break;
    }
    return done;
}

static bool fd_flush(IoStream *s)
{
    if (fsync(s->u.fd) == 0)
        return true;
    return error_set("fsync failed: %s", strerror(errno));
}

static bool fd_close(IoStream *s)
{
    if (close(s->u.fd) == 0 || errno == EINTR)
        return true;
    return error_set("close failed: %s", strerror(errno));
}

static const IoVTable k_fd_vt = {fd_size, fd_seek, fd_read, fd_write, fd_flush, fd_close};

IoStream *io_platform_open_file(const char *path, int flags)
{
    int oflags = O_CLOEXEC;
    if ((flags & IO_MODE_READ) && (flags & IO_MODE_WRITE))
        oflags |= O_RDWR;
    else if (flags & IO_MODE_WRITE)
        oflags |= O_WRONLY;
    else
        oflags |= O_RDONLY;
    if (flags & IO_MODE_CREATE)
        oflags |= O_CREAT;
    if (flags & IO_MODE_TRUNCATE)
        oflags |= O_TRUNC;
    if (flags & IO_MODE_APPEND)
        oflags |= O_APPEND;

    int fd;
    do
    {
        fd = open(path, oflags, 0666);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
    {
        error_set("cannot open '%s': %s", path, strerror(errno));
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode))
    {
        close(fd);
        error_set("cannot open '%s': is a directory", path);
        return NULL;
    }

    IoStream *s = io_stream_alloc(&k_fd_vt);
    if (!s)
    {
        close(fd);
        return NULL;
    }
    s->u.fd = fd;
    return s;
}

static void sync_parent_dir(const char *path)
{
    char dir[4096];
    path_directory(path, dir, sizeof dir);
    int fd = open(dir[0] ? dir : (path[0] == '/' ? "/" : "."), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    fsync(fd);
    close(fd);
}

bool io_platform_replace_file(const char *from, const char *to)
{
    if (rename(from, to) != 0)
        return error_set("cannot rename '%s' to '%s': %s", from, to, strerror(errno));
    sync_parent_dir(to);
    return true;
}

bool io_platform_remove_file(const char *path)
{
    return unlink(path) == 0;
}
