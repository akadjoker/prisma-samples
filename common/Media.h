#pragma once

#include <ct/vector.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dirent.h>
#include <strings.h>
#endif

#ifndef PRISMA_MEDIA_DIR
#define PRISMA_MEDIA_DIR "media"
#endif

namespace zenapp
{

inline bool mediaPath(const char* relative, char* out, size_t size)
{
    const char* root = getenv("PRISMA_MEDIA");
    if (!root || !*root) root = PRISMA_MEDIA_DIR;
    const int written = snprintf(out, size, "%s/%s", root, relative);
    return written > 0 && static_cast<size_t>(written) < size;
}

inline bool resolveCase(const char* path, char* out, size_t size)
{
    snprintf(out, size, "%s", path);
    FILE* file = fopen(out, "rb");
    if (file)
    {
        fclose(file);
        return true;
    }
#ifdef _WIN32
    return false;
#else
    char current[1024];
    size_t length = 0;
    const char* cursor = path;
    if (*cursor == '/')
    {
        current[length++] = '/';
        ++cursor;
    }
    current[length] = '\0';
    while (*cursor)
    {
        const char* end = strchr(cursor, '/');
        const size_t part = end ? static_cast<size_t>(end - cursor) : strlen(cursor);
        char name[256];
        if (part == 0 || part >= sizeof(name)) return false;
        memcpy(name, cursor, part);
        name[part] = '\0';

        char directory[1024];
        snprintf(directory, sizeof(directory), "%s", length ? current : ".");
        DIR* dir = opendir(directory);
        if (!dir) return false;
        bool found = false;
        while (dirent* entry = readdir(dir))
        {
            if (strcasecmp(entry->d_name, name) != 0) continue;
            const int written = snprintf(current + length, sizeof(current) - length, "%s%s",
                    length && current[length - 1] != '/' ? "/" : "", entry->d_name);
            if (written > 0) length += static_cast<size_t>(written);
            found = true;
            break;
        }
        closedir(dir);
        if (!found) return false;
        cursor += part;
        if (*cursor == '/') ++cursor;
    }
    snprintf(out, size, "%s", current);
    return true;
#endif
}

inline bool readFile(const char* requested, ct::Vector<unsigned char>* out)
{
    char path[1024];
    if (!resolveCase(requested, path, sizeof(path))) return false;
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length < 0)
    {
        fclose(file);
        return false;
    }
    out->resize(static_cast<size_t>(length));
    const size_t read = length > 0 ? fread(out->data(), 1, static_cast<size_t>(length), file) : 0;
    fclose(file);
    return read == static_cast<size_t>(length);
}

} // namespace zenapp
