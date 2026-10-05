/*
 * os.c - portable OS/filesystem core: file helpers over io.c, string logic for
 * paths. Directory and stat work lives in fs_posix.c.
 */
#define _POSIX_C_SOURCE 200809L

#include "platform.h"
#include "os_backend.h"
#include "error_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#if defined(_WIN32)
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#include <errno.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#define getcwd _getcwd
#define chdir _chdir
#else
#include <unistd.h>
#endif

#ifndef PATH_CAP
#define PATH_CAP 4096
#endif

/* ---- read-write files ---- */

uint8_t *file_read(const char *path, size_t *out_size)
{
    return io_load_file(path, out_size);
}

char *file_read_text(const char *path)
{
    return io_load_file(path, NULL);
}

bool file_write(const char *path, const void *data, size_t size)
{
    return io_save_file(path, data, size);
}

bool file_write_text(const char *path, const char *text)
{
    return io_save_file(path, text, text ? strlen(text) : 0);
}

void fs_free(void *data)
{
    free(data);
}

/* ---- queries ---- */

bool file_exists(const char *path)
{
    PathInfo info;
    return fs_get_path_info(path, &info) && info.type != PATH_TYPE_DIRECTORY;
}

bool dir_exists(const char *path)
{
    PathInfo info;
    return fs_get_path_info(path, &info) && info.type == PATH_TYPE_DIRECTORY;
}

int64_t file_size(const char *path)
{
    PathInfo info;
    return fs_get_path_info(path, &info) ? info.size : -1;
}

int64_t file_mod_time(const char *path)
{
    PathInfo info;
    return fs_get_path_info(path, &info) ? info.modify_time_ns / 1000000000 : -1;
}

/* ---- path string helpers ---- */

static const char *last_sep(const char *path)
{
    const char *slash = strrchr(path, '/');
#if defined(_WIN32)
    const char *back = strrchr(path, '\\');
    if (back > slash)
        slash = back;
#endif
    return slash;
}

const char *path_filename(const char *path)
{
    const char *sep = last_sep(path);
    return sep ? sep + 1 : path;
}

const char *path_extension(const char *path)
{
    const char *name = path_filename(path);
    const char *dot = strrchr(name, '.');
    return (dot && dot != name) ? dot : "";
}

void path_directory(const char *path, char *out, size_t cap)
{
    const char *sep = last_sep(path);
    if (!sep || cap == 0)
    {
        if (cap)
            out[0] = '\0';
        return;
    }
    size_t n = (size_t)(sep - path);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

bool path_has_extension(const char *path, const char *ext)
{
    const char *e = path_extension(path);
    if (!e[0])
        return false;
    if (e[0] == '.')
        e++;
    if (ext[0] == '.')
        ext++;
    return strcasecmp(e, ext) == 0;
}

/* ---- path builders ---- */

static bool is_sep(char c)
{
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

static size_t root_len(const char *p)
{
#if defined(_WIN32)
    if (isalpha((unsigned char)p[0]) && p[1] == ':')
        return is_sep(p[2]) ? 3 : 2;
    if (is_sep(p[0]) && is_sep(p[1]))
        return 2;
#endif
    return is_sep(p[0]) ? 1 : 0;
}

static bool name_eq(const char *a, const char *b, size_t n)
{
#if defined(_WIN32)
    return _strnicmp(a, b, n) == 0;
#else
    return strncmp(a, b, n) == 0;
#endif
}

static size_t component_len(const char *s)
{
    size_t n = 0;
    while (s[n] && !is_sep(s[n]))
        n++;
    return n;
}

static bool path_fail(char *out, size_t cap)
{
    if (cap)
        out[0] = '\0';
    return error_set("path too long");
}

static bool path_put(char *out, size_t cap, const char *src, size_t n)
{
    if (n >= cap)
        return path_fail(out, cap);
    memmove(out, src, n);
    out[n] = '\0';
    return true;
}

bool path_is_absolute(const char *path)
{
    if (!path)
        return false;
    size_t root = root_len(path);
#if defined(_WIN32)
    if (root == 2 && path[1] == ':')
        return false;
#endif
    return root > 0;
}

bool path_join(char *out, size_t cap, const char *a, const char *b)
{
    char res[PATH_CAP];
    if (!a)
        a = "";
    if (!b)
        b = "";
    if (!a[0] || path_is_absolute(b))
        return path_put(out, cap, b, strlen(b));

    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t sep = (is_sep(a[la - 1]) || !b[0]) ? 0 : 1;
    if (la + sep + lb >= sizeof res)
        return path_fail(out, cap);
    memcpy(res, a, la);
    if (sep)
        res[la] = '/';
    memcpy(res + la + sep, b, lb);
    return path_put(out, cap, res, la + sep + lb);
}

bool path_normalize(char *out, size_t cap, const char *path)
{
    char res[PATH_CAP];
    if (!path)
        path = "";
    size_t root = root_len(path);
    bool absolute = path_is_absolute(path);
    size_t n = 0;
    for (; n < root; n++)
        res[n] = is_sep(path[n]) ? '/' : path[n];

    const char *p = path + root;
    for (;;)
    {
        while (is_sep(*p))
            p++;
        if (!*p)
            break;
        const char *name = p;
        size_t len = component_len(p);
        p += len;

        if (len == 1 && name[0] == '.')
            continue;
        if (len == 2 && name[0] == '.' && name[1] == '.')
        {
            size_t last = n;
            while (last > root && res[last - 1] != '/')
                last--;
            bool parent_is_dotdot = n - last == 2 && res[last] == '.' && res[last + 1] == '.';
            if (n > root && !parent_is_dotdot)
            {
                n = last > root ? last - 1 : root;
                continue;
            }
            if (absolute)
                continue;
        }

        size_t sep = n > root ? 1 : 0;
        if (n + sep + len >= sizeof res)
            return path_fail(out, cap);
        if (sep)
            res[n++] = '/';
        memcpy(res + n, name, len);
        n += len;
    }
    if (n == 0)
        res[n++] = '.';
    return path_put(out, cap, res, n);
}

bool path_absolute(char *out, size_t cap, const char *path)
{
    char cwd[PATH_CAP];
    char joined[PATH_CAP];
    if (path_is_absolute(path))
        return path_normalize(out, cap, path);
    if (!getcwd(cwd, sizeof cwd))
    {
        if (cap)
            out[0] = '\0';
        return error_set("getcwd failed: %s", strerror(errno));
    }
    if (!path_join(joined, sizeof joined, cwd, path))
        return path_fail(out, cap);
    return path_normalize(out, cap, joined);
}

bool path_relative(char *out, size_t cap, const char *path, const char *base)
{
    char p[PATH_CAP];
    char b[PATH_CAP];
    char res[PATH_CAP];
    if (!path_absolute(p, sizeof p, path) || !path_absolute(b, sizeof b, base))
    {
        if (cap)
            out[0] = '\0';
        return false;
    }

    size_t root = root_len(p);
    if (root != root_len(b) || !name_eq(p, b, root))
    {
        if (cap)
            out[0] = '\0';
        return error_set("no relative path from '%s' to '%s'", b, p);
    }

    size_t i = root;
    size_t j = root;
    for (;;)
    {
        size_t lp = component_len(p + i);
        size_t lb = component_len(b + j);
        if (lp == 0 || lp != lb || !name_eq(p + i, b + j, lp))
            break;
        i += lp;
        j += lb;
        if (p[i] == '/')
            i++;
        if (b[j] == '/')
            j++;
    }

    size_t n = 0;
    while (b[j])
    {
        size_t lb = component_len(b + j);
        if (n + 3 >= sizeof res)
            return path_fail(out, cap);
        if (n)
            res[n++] = '/';
        res[n++] = '.';
        res[n++] = '.';
        j += lb;
        if (b[j] == '/')
            j++;
    }
    size_t rest = strlen(p + i);
    if (rest)
    {
        if (n + 1 + rest >= sizeof res)
            return path_fail(out, cap);
        if (n)
            res[n++] = '/';
        memcpy(res + n, p + i, rest);
        n += rest;
    }
    if (n == 0)
        res[n++] = '.';
    return path_put(out, cap, res, n);
}

/* ---- directories ---- */

const char *dir_current(void)
{
    static char cwd[PATH_CAP];
    if (!getcwd(cwd, sizeof cwd))
        cwd[0] = '\0';
    return cwd;
}

const char *dir_app(void)
{
    static char app[PATH_CAP];
    if (!fs_get_base_path(app, sizeof app))
        return dir_current();
    size_t n = strlen(app);
    if (n > 1 && app[n - 1] == '/')
        app[n - 1] = '\0';
    return app;
}

const char *dir_data(void)
{
    const char *routed = os_backend_data_dir();
    return routed ? routed : dir_app();
}

bool dir_change(const char *path)
{
    return chdir(path) == 0;
}

bool dir_make(const char *path)
{
    return fs_create_directory(path);
}

typedef struct
{
    DirList *list;
    int cap;
    bool failed;
} ListState;

static bool list_add(const char *path, PathType type, void *user)
{
    (void)type;
    ListState *ls = user;
    DirList *list = ls->list;
    if (list->count == ls->cap)
    {
        int cap = ls->cap ? ls->cap * 2 : 16;
        char **grown = realloc(list->paths, (size_t)cap * sizeof *grown);
        if (!grown)
        {
            ls->failed = true;
            return false;
        }
        list->paths = grown;
        ls->cap = cap;
    }
    char *copy = malloc(strlen(path) + 1);
    if (!copy)
    {
        ls->failed = true;
        return false;
    }
    list->paths[list->count++] = strcpy(copy, path);
    return true;
}

bool dir_list(const char *path, DirList *out)
{
    if (!out)
        return false;
    out->paths = NULL;
    out->count = 0;

    ListState ls = {out, 0, false};
    bool ok = fs_enumerate_directory(path, false, list_add, &ls);
    if (ok && ls.failed)
        ok = error_set("out of memory");
    if (!ok)
        dir_list_free(out);
    return ok;
}

void dir_list_free(DirList *list)
{
    if (!list || !list->paths)
        return;
    for (int i = 0; i < list->count; i++)
        free(list->paths[i]);
    free(list->paths);
    list->paths = NULL;
    list->count = 0;
}

/* Default OS-backend hooks: no routing. The Android backend overrides these. */
#if !defined(__ANDROID__)
IoStream *os_backend_asset_open(const char *path)
{
    (void)path;
    return NULL;
}
int os_backend_asset_exists(const char *path)
{
    (void)path;
    return -1;
}
const char *os_backend_data_dir(void)
{
    return NULL;
}
#endif
