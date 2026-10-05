#include "io_internal.h"
#include "os_backend.h"
#include "error_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_CAP
#define PATH_CAP 4096
#endif

#define IO_LOAD_CHUNK 4096

IoStream *io_stream_alloc(const IoVTable *vt)
{
    IoStream *s = calloc(1, sizeof *s);
    if (!s)
    {
        error_set("out of memory");
        return NULL;
    }
    s->vt = vt;
    return s;
}

int io_parse_mode(const char *mode)
{
    if (!mode)
        return 0;
    int flags;
    switch (mode[0])
    {
    case 'r':
        flags = IO_MODE_READ;
        break;
    case 'w':
        flags = IO_MODE_WRITE | IO_MODE_CREATE | IO_MODE_TRUNCATE;
        break;
    case 'a':
        flags = IO_MODE_WRITE | IO_MODE_CREATE | IO_MODE_APPEND;
        break;
    default:
        return 0;
    }
    for (const char *m = mode + 1; *m; m++)
    {
        if (*m == '+')
            flags |= IO_MODE_READ | IO_MODE_WRITE;
        else if (*m != 'b')
            return 0;
    }
    return flags;
}

/* ---- memory stream (read-only) ---- */

static int64_t mem_size(IoStream *s)
{
    return (int64_t)s->u.mem.size;
}

static int64_t mem_seek(IoStream *s, int64_t offset, IoWhence whence)
{
    int64_t base = whence == IO_SEEK_SET ? 0 : whence == IO_SEEK_CUR ? (int64_t)s->u.mem.pos
                                                                     : (int64_t)s->u.mem.size;
    int64_t pos = base + offset;
    if (pos < 0 || pos > (int64_t)s->u.mem.size)
    {
        error_set("seek out of range");
        return -1;
    }
    s->u.mem.pos = (size_t)pos;
    return pos;
}

static size_t mem_read(IoStream *s, void *dst, size_t n)
{
    size_t left = s->u.mem.size - s->u.mem.pos;
    if (n > left)
    {
        n = left;
        s->eof = true;
    }
    memcpy(dst, s->u.mem.base + s->u.mem.pos, n);
    s->u.mem.pos += n;
    return n;
}

static size_t mem_write(IoStream *s, const void *src, size_t n)
{
    (void)s;
    (void)src;
    (void)n;
    error_set("memory stream is read-only");
    return 0;
}

static bool mem_flush(IoStream *s)
{
    (void)s;
    return true;
}

static bool mem_close(IoStream *s)
{
    (void)s;
    return true;
}

static const IoVTable k_mem_vt = {mem_size, mem_seek, mem_read, mem_write, mem_flush, mem_close};

IoStream *io_open_memory(const void *mem, size_t size)
{
    if (!mem && size)
    {
        error_set("invalid argument");
        return NULL;
    }
    IoStream *s = io_stream_alloc(&k_mem_vt);
    if (!s)
        return NULL;
    s->u.mem.base = mem;
    s->u.mem.size = size;
    return s;
}

/* ---- files ---- */

#if defined(__ANDROID__)
static const char *android_data_path(const char *path, char *buf, size_t cap)
{
    const char *data = os_backend_data_dir();
    if (path_is_absolute(path) || !data)
        return path;
    return path_join(buf, cap, data, path) ? buf : NULL;
}
#endif

IoStream *io_open_file(const char *path, const char *mode)
{
    int flags = io_parse_mode(mode);
    if (!path || !path[0])
    {
        error_set("empty path");
        return NULL;
    }
    if (!flags)
    {
        error_set("invalid mode '%s'", mode ? mode : "(null)");
        return NULL;
    }
#if defined(__ANDROID__)
    if (!path_is_absolute(path))
    {
        char buf[PATH_CAP];
        const char *full = android_data_path(path, buf, sizeof buf);
        IoStream *s = full ? io_platform_open_file(full, flags) : NULL;
        if (s || flags != IO_MODE_READ)
            return s;
        s = os_backend_asset_open(path);
        if (!s)
            error_set("cannot open '%s'", path);
        return s;
    }
#endif
    return io_platform_open_file(path, flags);
}

size_t io_read(IoStream *s, void *dst, size_t n)
{
    if (!s || (!dst && n))
    {
        error_set("invalid argument");
        return 0;
    }
    return n ? s->vt->read(s, dst, n) : 0;
}

size_t io_write(IoStream *s, const void *src, size_t n)
{
    if (!s || (!src && n))
    {
        error_set("invalid argument");
        return 0;
    }
    return n ? s->vt->write(s, src, n) : 0;
}

int64_t io_seek(IoStream *s, int64_t offset, IoWhence whence)
{
    if (!s || whence < IO_SEEK_SET || whence > IO_SEEK_END)
    {
        error_set("invalid argument");
        return -1;
    }
    int64_t pos = s->vt->seek(s, offset, whence);
    if (pos >= 0)
        s->eof = false;
    return pos;
}

int64_t io_tell(IoStream *s)
{
    if (!s)
    {
        error_set("invalid argument");
        return -1;
    }
    return s->vt->seek(s, 0, IO_SEEK_CUR);
}

int64_t io_size(IoStream *s)
{
    if (!s)
    {
        error_set("invalid argument");
        return -1;
    }
    return s->vt->size(s);
}

bool io_eof(IoStream *s)
{
    return s && s->eof;
}

bool io_flush(IoStream *s)
{
    if (!s)
        return error_set("invalid argument");
    return s->vt->flush(s);
}

bool io_close(IoStream *s)
{
    if (!s)
        return error_set("invalid argument");
    bool ok = s->vt->close(s);
    free(s);
    return ok;
}

static uint8_t *load_sized(IoStream *s, size_t want, size_t *len)
{
    uint8_t *buf = malloc(want + 1);
    if (!buf)
    {
        error_set("out of memory");
        return NULL;
    }
    *len = io_read(s, buf, want);
    if (*len != want && !s->eof)
    {
        free(buf);
        return NULL;
    }
    return buf;
}

static uint8_t *load_unsized(IoStream *s, size_t *len)
{
    uint8_t *buf = NULL;
    size_t cap = 0;
    *len = 0;
    for (;;)
    {
        if (cap - *len < 2)
        {
            size_t grown = cap ? cap * 2 : IO_LOAD_CHUNK;
            uint8_t *p = grown > cap ? realloc(buf, grown) : NULL;
            if (!p)
            {
                free(buf);
                error_set("out of memory");
                return NULL;
            }
            buf = p;
            cap = grown;
        }
        size_t got = io_read(s, buf + *len, cap - *len - 1);
        *len += got;
        if (got == 0)
        {
            if (s->eof)
                return buf;
            free(buf);
            return NULL;
        }
    }
}

void *io_load(IoStream *s, size_t *out_size, bool close)
{
    if (!s)
        return NULL;
    size_t len = 0;
    uint8_t *buf;
    int64_t size = s->vt->size(s);
    int64_t pos = size >= 0 ? s->vt->seek(s, 0, IO_SEEK_CUR) : -1;
    if (pos >= 0 && size >= pos && (uint64_t)(size - pos) < SIZE_MAX)
        buf = load_sized(s, (size_t)(size - pos), &len);
    else
        buf = load_unsized(s, &len);
    if (buf)
    {
        buf[len] = '\0';
        if (out_size)
            *out_size = len;
    }
    if (close)
        io_close(s);
    return buf;
}

void *io_load_file(const char *path, size_t *out_size)
{
    return io_load(io_open_file(path, "rb"), out_size, true);
}

bool io_save_file(const char *path, const void *data, size_t size)
{
    char tmp[PATH_CAP];
    if (!path || !path[0] || (!data && size))
        return error_set("invalid argument");
#if defined(__ANDROID__)
    char buf[PATH_CAP];
    path = android_data_path(path, buf, sizeof buf);
    if (!path)
        return false;
#endif
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp)
        return error_set("path too long");

    IoStream *s = io_platform_open_file(tmp, IO_MODE_WRITE | IO_MODE_CREATE | IO_MODE_TRUNCATE);
    if (!s)
        return false;
    bool ok = io_write(s, data, size) == size && io_flush(s);
    if (!io_close(s))
        ok = false;
    if (ok && io_platform_replace_file(tmp, path))
        return true;
    io_platform_remove_file(tmp);
    return false;
}

/* ---- assets ---- */

static char g_asset_root[PATH_CAP];

void asset_set_root(const char *path)
{
    snprintf(g_asset_root, sizeof g_asset_root, "%s", path ? path : "");
}

static bool asset_disk_path(const char *path, char *out, size_t cap)
{
    const char *root = g_asset_root[0] ? g_asset_root : dir_app();
    return path_join(out, cap, root ? root : "", path);
}

IoStream *io_open_asset(const char *path)
{
    char full[PATH_CAP];
    if (!path || !path[0])
    {
        error_set("empty path");
        return NULL;
    }
    IoStream *s = os_backend_asset_open(path);
    if (s)
        return s;
    if (!asset_disk_path(path, full, sizeof full))
        return NULL;
    return io_platform_open_file(full, IO_MODE_READ);
}

uint8_t *asset_read(const char *path, size_t *out_size)
{
    return io_load(io_open_asset(path), out_size, true);
}

char *asset_read_text(const char *path)
{
    return io_load(io_open_asset(path), NULL, true);
}

bool asset_exists(const char *path)
{
    char full[PATH_CAP];
    if (!path || !path[0])
        return false;
    int routed = os_backend_asset_exists(path);
    if (routed >= 0)
        return routed != 0;
    return asset_disk_path(path, full, sizeof full) && file_exists(full);
}
