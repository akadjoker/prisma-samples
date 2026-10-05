#include "io_internal.h"
#include "os_backend.h"
#include "error_internal.h"

#include <android/asset_manager.h>
#include <limits.h>
#include <stdio.h>

#ifndef PATH_CAP
#define PATH_CAP 4096
#endif

AAssetManager *android_asset_manager(void);

static int64_t aa_size(IoStream *s)
{
    return (int64_t)AAsset_getLength64(s->u.handle);
}

static int64_t aa_seek(IoStream *s, int64_t offset, IoWhence whence)
{
    static const int k_whence[] = {SEEK_SET, SEEK_CUR, SEEK_END};
    off64_t pos = AAsset_seek64(s->u.handle, (off64_t)offset, k_whence[whence]);
    if (pos < 0)
    {
        error_set("asset seek failed");
        return -1;
    }
    return (int64_t)pos;
}

static size_t aa_read(IoStream *s, void *dst, size_t n)
{
    char *p = dst;
    size_t done = 0;
    while (done < n)
    {
        size_t chunk = n - done < (size_t)INT_MAX ? n - done : (size_t)INT_MAX;
        int r = AAsset_read(s->u.handle, p + done, chunk);
        if (r > 0)
        {
            done += (size_t)r;
            continue;
        }
        if (r == 0)
            s->eof = true;
        else
            error_set("asset read failed");
        break;
    }
    return done;
}

static size_t aa_write(IoStream *s, const void *src, size_t n)
{
    (void)s;
    (void)src;
    (void)n;
    error_set("assets are read-only");
    return 0;
}

static bool aa_flush(IoStream *s)
{
    (void)s;
    return true;
}

static bool aa_close(IoStream *s)
{
    AAsset_close(s->u.handle);
    return true;
}

static const IoVTable k_aa_vt = {aa_size, aa_seek, aa_read, aa_write, aa_flush, aa_close};

static AAsset *aa_open(const char *path, int mode)
{
    char clean[PATH_CAP];
    AAssetManager *mgr = android_asset_manager();
    if (!mgr || !path_normalize(clean, sizeof clean, path))
        return NULL;
    return AAssetManager_open(mgr, clean, mode);
}

IoStream *os_backend_asset_open(const char *path)
{
    AAsset *a = aa_open(path, AASSET_MODE_RANDOM);
    if (!a)
        return NULL;
    IoStream *s = io_stream_alloc(&k_aa_vt);
    if (!s)
    {
        AAsset_close(a);
        return NULL;
    }
    s->u.handle = a;
    return s;
}

int os_backend_asset_exists(const char *path)
{
    if (!android_asset_manager())
        return -1;
    AAsset *a = aa_open(path, AASSET_MODE_UNKNOWN);
    if (!a)
        return 0;
    AAsset_close(a);
    return 1;
}
