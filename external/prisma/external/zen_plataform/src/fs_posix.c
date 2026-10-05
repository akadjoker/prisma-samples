#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "platform.h"
#include "os_backend.h"
#include "error_internal.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_CAP
#define PATH_CAP 4096
#endif

#if defined(__APPLE__)
#define MTIME_NS(st) ((int64_t)(st).st_mtimespec.tv_sec * 1000000000 + (st).st_mtimespec.tv_nsec)
#else
#define MTIME_NS(st) ((int64_t)(st).st_mtim.tv_sec * 1000000000 + (st).st_mtim.tv_nsec)
#endif

static PathType type_from_mode(mode_t mode)
{
    if (S_ISREG(mode))
        return PATH_TYPE_FILE;
    if (S_ISDIR(mode))
        return PATH_TYPE_DIRECTORY;
    return PATH_TYPE_OTHER;
}

bool fs_get_path_info(const char *path, PathInfo *out)
{
    struct stat st;
    if (!path || !path[0])
        return error_set("empty path");
    if (stat(path, &st) != 0)
        return error_set("cannot stat '%s': %s", path, strerror(errno));
    if (out)
    {
        out->type = type_from_mode(st.st_mode);
        out->size = (int64_t)st.st_size;
        out->modify_time_ns = MTIME_NS(st);
    }
    return true;
}

static bool make_one(const char *path)
{
    struct stat st;
    if (mkdir(path, 0777) == 0)
        return true;
    if (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return true;
    return error_set("cannot create directory '%s': %s", path, strerror(errno));
}

bool fs_create_directory(const char *path)
{
    char tmp[PATH_CAP];
    if (!path || !path[0])
        return error_set("empty path");
    if (strlen(path) >= sizeof tmp)
        return error_set("path too long");
    strcpy(tmp, path);
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p != '/')
            continue;
        *p = '\0';
        if (!make_one(tmp))
            return false;
        *p = '/';
    }
    return make_one(tmp);
}

bool fs_remove_path(const char *path)
{
    struct stat st;
    if (!path || !path[0])
        return error_set("empty path");
    if (lstat(path, &st) != 0)
        return error_set("cannot remove '%s': %s", path, strerror(errno));
    int r = S_ISDIR(st.st_mode) ? rmdir(path) : unlink(path);
    if (r != 0)
        return error_set("cannot remove '%s': %s", path, strerror(errno));
    return true;
}

bool fs_rename_path(const char *from, const char *to)
{
    if (!from || !from[0] || !to || !to[0])
        return error_set("empty path");
    if (rename(from, to) != 0)
        return error_set("cannot rename '%s' to '%s': %s", from, to, strerror(errno));
    return true;
}

typedef struct
{
    FsEnumCallback cb;
    void *user;
    bool recursive;
    char path[PATH_CAP];
} EnumState;

static bool classify(const char *path, unsigned char d_type, PathType *type, bool *descend)
{
    struct stat st;
    *descend = false;
    switch (d_type)
    {
    case DT_DIR:
        *type = PATH_TYPE_DIRECTORY;
        *descend = true;
        return true;
    case DT_REG:
        *type = PATH_TYPE_FILE;
        return true;
    case DT_LNK:
        *type = stat(path, &st) == 0 ? type_from_mode(st.st_mode) : PATH_TYPE_OTHER;
        return true;
    case DT_UNKNOWN:
        if (lstat(path, &st) != 0)
            return error_set("cannot stat '%s': %s", path, strerror(errno));
        if (S_ISLNK(st.st_mode))
        {
            *type = stat(path, &st) == 0 ? type_from_mode(st.st_mode) : PATH_TYPE_OTHER;
            return true;
        }
        *type = type_from_mode(st.st_mode);
        *descend = *type == PATH_TYPE_DIRECTORY;
        return true;
    default:
        *type = PATH_TYPE_OTHER;
        return true;
    }
}

/* Returns 1 to continue, 0 when the callback stopped it, -1 on error. */
static int enum_dir(EnumState *es)
{
    size_t base = strlen(es->path);
    DIR *d = opendir(es->path);
    if (!d)
    {
        error_set("cannot open directory '%s': %s", es->path, strerror(errno));
        return -1;
    }
    if (base > 0 && es->path[base - 1] != '/')
        es->path[base++] = '/';

    int result = 1;
    struct dirent *e;
    while (result == 1 && (e = readdir(d)))
    {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        size_t n = strlen(e->d_name);
        if (base + n >= sizeof es->path)
        {
            error_set("path too long");
            result = -1;
            break;
        }
        memcpy(es->path + base, e->d_name, n + 1);

        PathType type;
        bool descend;
        if (!classify(es->path, e->d_type, &type, &descend))
            result = -1;
        else if (!es->cb(es->path, type, es->user))
            result = 0;
        else if (es->recursive && descend)
            result = enum_dir(es);
        es->path[base] = '\0';
    }
    closedir(d);
    return result;
}

bool fs_enumerate_directory(const char *path, bool recursive, FsEnumCallback cb, void *user)
{
    if (!path || !path[0] || !cb)
        return error_set("invalid argument");
    EnumState *es = malloc(sizeof *es);
    if (!es)
        return error_set("out of memory");
    es->cb = cb;
    es->user = user;
    es->recursive = recursive;
    bool ok = strlen(path) < sizeof es->path;
    if (ok)
    {
        strcpy(es->path, path);
        ok = enum_dir(es) >= 0;
    }
    else
    {
        error_set("path too long");
    }
    free(es);
    return ok;
}

bool fs_get_base_path(char *out, size_t cap)
{
#if defined(__ANDROID__)
    (void)out;
    (void)cap;
    return error_set("no base path on this platform, use assets");
#else
    char exe[PATH_CAP];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
    {
        if (!getcwd(exe, sizeof exe))
            return error_set("cannot determine the base path: %s", strerror(errno));
        n = (ssize_t)strlen(exe);
        if (n + 1 >= (ssize_t)sizeof exe)
            return error_set("path too long");
        exe[n++] = '/';
        exe[n] = '\0';
    }
    else
    {
        exe[n] = '\0';
        char *sep = strrchr(exe, '/');
        if (!sep)
            return error_set("cannot determine the base path");
        sep[1] = '\0';
    }
    if (strlen(exe) >= cap)
    {
        if (cap)
            out[0] = '\0';
        return error_set("path too long");
    }
    strcpy(out, exe);
    return true;
#endif
}

static bool valid_name(const char *name)
{
    return name[0] && strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && !strchr(name, '/');
}

bool fs_get_pref_path(char *out, size_t cap, const char *org, const char *app)
{
    char dir[PATH_CAP];
    if (!out || cap == 0)
        return error_set("invalid argument");
    out[0] = '\0';
    if (!app || !valid_name(app) || (org && org[0] && !valid_name(org)))
        return error_set("invalid organization or application name");

#if defined(__ANDROID__)
    const char *data = os_backend_data_dir();
    if (!data)
        return error_set("no data directory available");
    if (!path_join(dir, sizeof dir, data, ""))
        return false;
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (xdg && path_is_absolute(xdg))
    {
        if (!path_join(dir, sizeof dir, xdg, ""))
            return false;
    }
    else if (home && path_is_absolute(home))
    {
        if (!path_join(dir, sizeof dir, home, ".local/share"))
            return false;
    }
    else
    {
        return error_set("neither XDG_DATA_HOME nor HOME is set to an absolute path");
    }
    if (org && org[0] && !path_join(dir, sizeof dir, dir, org))
        return false;
    if (!path_join(dir, sizeof dir, dir, app))
        return false;
#endif

    if (!fs_create_directory(dir))
        return false;
    size_t n = strlen(dir);
    if (n + 2 > cap)
        return error_set("path too long");
    memcpy(out, dir, n);
    if (out[n - 1] != '/')
        out[n++] = '/';
    out[n] = '\0';
    return true;
}

bool fs_get_temp_path(char *out, size_t cap)
{
    if (!out || cap == 0)
        return error_set("invalid argument");
#if defined(__ANDROID__)
    const char *data = os_backend_data_dir();
    if (!data)
        return error_set("no data directory available");
    return path_join(out, cap, data, "cache") && fs_create_directory(out);
#else
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !path_is_absolute(tmp))
        tmp = "/tmp";
    return path_normalize(out, cap, tmp);
#endif
}
