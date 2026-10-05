/*
 * test_library.c - library_open / library_symbol / library_close.
 */
#include "platform.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define KNOWN_LIB "kernel32.dll"
#define KNOWN_SYM "GetCurrentProcessId"
#elif defined(__APPLE__)
#define KNOWN_LIB "libSystem.B.dylib"
#define KNOWN_SYM "strlen"
#else
#define KNOWN_LIB "libc.so.6"
#define KNOWN_SYM "strlen"
#endif

static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
            g_pass++;                                              \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

int main(void)
{
    SharedLibrary *lib = library_open(KNOWN_LIB);
    CHECK(lib != NULL);
    if (lib)
    {
        CHECK(library_symbol(lib, KNOWN_SYM) != NULL);

        platform_clear_error();
        CHECK(library_symbol(lib, "zen_no_such_symbol") == NULL);
        CHECK(strstr(platform_get_error(), "zen_no_such_symbol") != NULL);

        library_close(lib);
    }

    platform_clear_error();
    CHECK(library_open("zen_no_such_library.so") == NULL);
    CHECK(platform_get_error()[0] != '\0');

    /* NULL is tolerated everywhere */
    CHECK(library_open(NULL) == NULL);
    CHECK(library_symbol(NULL, "x") == NULL);
    library_close(NULL);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
