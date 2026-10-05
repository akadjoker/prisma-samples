#include "platform.h"
#include "os_backend.h"
#include "win32_util.h"
#include "error_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_CAP
#define PATH_CAP 4096
#endif

#define EPOCH_DIFF_100NS 116444736000000000ULL

static int64_t filetime_ns(FILETIME ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (int64_t)((u.QuadPart - EPOCH_DIFF_100NS) * 100ULL);
}

static PathType type_from_attributes(DWORD attributes)
{
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        return PATH_TYPE_DIRECTORY;
    if (attributes & (FILE_ATTRIBUTE_DEVICE))
        return PATH_TYPE_OTHER;
    return PATH_TYPE_FILE;
}

bool fs_get_path_info(const char *path, PathInfo *out)
{
    wchar_t wide[PATH_CAP];
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!path || !path[0])
        return error_set("empty path");
    if (!win32_widen(path, wide, PATH_CAP))
        return false;
    if (!GetFileAttributesExW(wide, GetFileExInfoStandard, &data))
        return win32_error("cannot stat", path);
    if (out)
    {
        ULARGE_INTEGER size;
        size.LowPart = data.nFileSizeLow;
        size.HighPart = data.nFileSizeHigh;
        out->type = type_from_attributes(data.dwFileAttributes);
        out->size = (int64_t)size.QuadPart;
        out->modify_time_ns = filetime_ns(data.ftLastWriteTime);
    }
    return true;
}

static bool make_one(const wchar_t *wide, const char *path)
{
    if (CreateDirectoryW(wide, NULL))
        return true;
    DWORD attributes = GetFileAttributesW(wide);
    if (GetLastError() == ERROR_ALREADY_EXISTS && attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return true;
    return win32_error("cannot create directory", path);
}

bool fs_create_directory(const char *path)
{
    wchar_t wide[PATH_CAP];
    if (!path || !path[0])
        return error_set("empty path");
    if (!win32_widen(path, wide, PATH_CAP))
        return false;
    size_t root = 0;
    if (wide[0] && wide[1] == L':')
        root = (wide[2] == L'\\' || wide[2] == L'/') ? 3 : 2;
    else if ((wide[0] == L'\\' || wide[0] == L'/') && (wide[1] == L'\\' || wide[1] == L'/'))
        root = 2;
    for (wchar_t *p = wide + (root ? root : 1); *p; p++)
    {
        if (*p != L'\\' && *p != L'/')
            continue;
        wchar_t saved = *p;
        *p = L'\0';
        if (!make_one(wide, path))
            return false;
        *p = saved;
    }
    return make_one(wide, path);
}

bool fs_remove_path(const char *path)
{
    wchar_t wide[PATH_CAP];
    if (!path || !path[0])
        return error_set("empty path");
    if (!win32_widen(path, wide, PATH_CAP))
        return false;
    DWORD attributes = GetFileAttributesW(wide);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return win32_error("cannot remove", path);
    if (attributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(wide, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    BOOL ok = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(wide) : DeleteFileW(wide);
    return ok ? true : win32_error("cannot remove", path);
}

bool fs_rename_path(const char *from, const char *to)
{
    wchar_t wfrom[PATH_CAP], wto[PATH_CAP];
    if (!from || !from[0] || !to || !to[0])
        return error_set("empty path");
    if (!win32_widen(from, wfrom, PATH_CAP) || !win32_widen(to, wto, PATH_CAP))
        return false;
    if (!MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING))
        return win32_error("cannot rename", from);
    return true;
}

typedef struct
{
    FsEnumCallback cb;
    void *user;
    bool recursive;
    char path[PATH_CAP];
} EnumState;

/* Returns 1 to continue, 0 when the callback stopped it, -1 on error. */
static int enum_dir(EnumState *es)
{
    wchar_t pattern[PATH_CAP];
    char name[PATH_CAP];
    size_t base = strlen(es->path);
    if (!win32_widen(es->path, pattern, PATH_CAP - 3))
        return -1;
    size_t wl = wcslen(pattern);
    if (wl > 0 && pattern[wl - 1] != L'\\' && pattern[wl - 1] != L'/')
        pattern[wl++] = L'\\';
    pattern[wl++] = L'*';
    pattern[wl] = L'\0';

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (h == INVALID_HANDLE_VALUE)
    {
        win32_error("cannot open directory", es->path);
        return -1;
    }
    if (base > 0 && es->path[base - 1] != '/' && es->path[base - 1] != '\\')
        es->path[base++] = '/';

    int result = 1;
    do
    {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        if (!win32_narrow(fd.cFileName, name, sizeof name))
        {
            result = -1;
            break;
        }
        size_t n = strlen(name);
        if (base + n >= sizeof es->path)
        {
            error_set("path too long");
            result = -1;
            break;
        }
        memcpy(es->path + base, name, n + 1);

        PathType type = type_from_attributes(fd.dwFileAttributes);
        bool descend = type == PATH_TYPE_DIRECTORY && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
        if (!es->cb(es->path, type, es->user))
            result = 0;
        else if (es->recursive && descend)
            result = enum_dir(es);
        es->path[base] = '\0';
    } while (result == 1 && FindNextFileW(h, &fd));
    FindClose(h);
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
    wchar_t wide[PATH_CAP];
    char utf8[PATH_CAP];
    DWORD n = GetModuleFileNameW(NULL, wide, PATH_CAP);
    if (n == 0 || n >= PATH_CAP)
        return win32_error("cannot determine the base path", NULL);
    wchar_t *sep = wcsrchr(wide, L'\\');
    if (!sep)
        return error_set("cannot determine the base path");
    sep[1] = L'\0';
    if (!win32_narrow(wide, utf8, sizeof utf8))
        return false;
    win32_slashes(utf8);
    if (strlen(utf8) >= cap)
    {
        if (cap)
            out[0] = '\0';
        return error_set("path too long");
    }
    strcpy(out, utf8);
    return true;
}

static bool valid_name(const char *name)
{
    return name[0] && strcmp(name, ".") != 0 && strcmp(name, "..") != 0 && !strpbrk(name, "/\\:*?\"<>|");
}

bool fs_get_pref_path(char *out, size_t cap, const char *org, const char *app)
{
    wchar_t wide[PATH_CAP];
    char dir[PATH_CAP];
    if (!out || cap == 0)
        return error_set("invalid argument");
    out[0] = '\0';
    if (!app || !valid_name(app) || (org && org[0] && !valid_name(org)))
        return error_set("invalid organization or application name");

    DWORD n = GetEnvironmentVariableW(L"APPDATA", wide, PATH_CAP);
    if (n == 0 || n >= PATH_CAP)
        return error_set("APPDATA is not set");
    if (!win32_narrow(wide, dir, sizeof dir))
        return false;
    win32_slashes(dir);
    if (org && org[0] && !path_join(dir, sizeof dir, dir, org))
        return false;
    if (!path_join(dir, sizeof dir, dir, app))
        return false;
    if (!fs_create_directory(dir))
        return false;
    size_t len = strlen(dir);
    if (len + 2 > cap)
        return error_set("path too long");
    memcpy(out, dir, len);
    if (out[len - 1] != '/')
        out[len++] = '/';
    out[len] = '\0';
    return true;
}

bool fs_get_temp_path(char *out, size_t cap)
{
    wchar_t wide[PATH_CAP];
    char utf8[PATH_CAP];
    if (!out || cap == 0)
        return error_set("invalid argument");
    DWORD n = GetTempPathW(PATH_CAP, wide);
    if (n == 0 || n >= PATH_CAP)
        return win32_error("cannot determine the temp path", NULL);
    if (!win32_narrow(wide, utf8, sizeof utf8))
        return false;
    win32_slashes(utf8);
    return path_normalize(out, cap, utf8);
}
