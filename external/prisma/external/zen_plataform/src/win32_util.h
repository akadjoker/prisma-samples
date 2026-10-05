#ifndef WIN32_UTIL_H
#define WIN32_UTIL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "platform.h"

bool win32_widen(const char *utf8, wchar_t *out, size_t cap);
bool win32_narrow(const wchar_t *wide, char *out, size_t cap);
bool win32_error(const char *what, const char *path);
void win32_slashes(char *path);

#endif /* WIN32_UTIL_H */
