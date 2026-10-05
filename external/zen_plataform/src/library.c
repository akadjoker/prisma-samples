/*
 * library.c - run-time loading of shared libraries (dlopen / LoadLibrary), the
 * counterpart of SDL_LoadObject. Not available on the web.
 */
#include "platform.h"
#include "error_internal.h"

#if defined(__EMSCRIPTEN__)

SharedLibrary *library_open(const char *path)
{
    (void)path;
    error_set("loading shared libraries is not supported on the web");
    return NULL;
}
void *library_symbol(SharedLibrary *lib, const char *name)
{
    (void)lib;
    (void)name;
    return NULL;
}
void library_close(SharedLibrary *lib)
{
    (void)lib;
}

#elif defined(_WIN32)

#include "win32_util.h"

SharedLibrary *library_open(const char *path)
{
    wchar_t wide[MAX_PATH * 2];
    if (!path || !win32_widen(path, wide, sizeof wide / sizeof wide[0]))
    {
        error_set("invalid library path");
        return NULL;
    }
    HMODULE m = LoadLibraryW(wide);
    if (!m)
        win32_error("cannot load the library", path);
    return (SharedLibrary *)m;
}

void *library_symbol(SharedLibrary *lib, const char *name)
{
    void *p = lib && name ? (void *)GetProcAddress((HMODULE)lib, name) : NULL;
    if (!p)
        error_set("symbol not found: %s", name ? name : "(null)");
    return p;
}

void library_close(SharedLibrary *lib)
{
    if (lib)
        FreeLibrary((HMODULE)lib);
}

#else

#include <dlfcn.h>

SharedLibrary *library_open(const char *path)
{
    if (!path)
    {
        error_set("invalid library path");
        return NULL;
    }
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h)
    {
        const char *e = dlerror();
        error_set("cannot load %s: %s", path, e ? e : "unknown error");
    }
    return (SharedLibrary *)h;
}

void *library_symbol(SharedLibrary *lib, const char *name)
{
    void *p = lib && name ? dlsym(lib, name) : NULL;
    if (!p)
        error_set("symbol not found: %s", name ? name : "(null)");
    return p;
}

void library_close(SharedLibrary *lib)
{
    if (lib)
        dlclose(lib);
}

#endif
