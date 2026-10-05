#include "platform.h"

#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
        {                                                          \
            g_pass++;                                              \
        }                                                          \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

#define CHECK_STR(got, want)                                                           \
    do                                                                                 \
    {                                                                                  \
        if (strcmp((got), (want)) == 0)                                                \
        {                                                                              \
            g_pass++;                                                                  \
        }                                                                              \
        else                                                                           \
        {                                                                              \
            g_fail++;                                                                  \
            printf("FAIL %s:%d  got '%s' want '%s'\n", __FILE__, __LINE__, got, want); \
        }                                                                              \
    } while (0)

static void test_normalize(void)
{
    static const char *cases[][2] = {
        {"", "."},
        {".", "."},
        {"./", "."},
        {"a", "a"},
        {"a/", "a"},
        {"a//b", "a/b"},
        {"a/./b", "a/b"},
        {"a/b/../c", "a/c"},
        {"a/..", "."},
        {"a/../..", ".."},
        {"../a", "../a"},
        {"../../a/../b", "../../b"},
        {"/", "/"},
#if !defined(_WIN32)
        {"//", "/"},
#endif
        {"/..", "/"},
        {"/../a", "/a"},
        {"/a/b/../../..", "/"},
        {"/a/./b/", "/a/b"},
        {"assets/../assets/tex/./a.png", "assets/tex/a.png"},
    };
    char out[256];
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        CHECK(path_normalize(out, sizeof out, cases[i][0]));
        CHECK_STR(out, cases[i][1]);
    }

    strcpy(out, "x/./y/../z");
    CHECK(path_normalize(out, sizeof out, out));
    CHECK_STR(out, "x/z");

    char tiny[4];
    CHECK(!path_normalize(tiny, sizeof tiny, "abcd"));
    CHECK_STR(tiny, "");
    CHECK(strlen(platform_get_error()) > 0);
    CHECK(path_normalize(tiny, sizeof tiny, "abc"));
    CHECK_STR(tiny, "abc");
}

static void test_join(void)
{
    char out[256];
    CHECK(path_join(out, sizeof out, "a", "b"));
    CHECK_STR(out, "a/b");
    CHECK(path_join(out, sizeof out, "a/", "b"));
    CHECK_STR(out, "a/b");
    CHECK(path_join(out, sizeof out, "", "b"));
    CHECK_STR(out, "b");
    CHECK(path_join(out, sizeof out, "a", ""));
    CHECK_STR(out, "a");
    CHECK(path_join(out, sizeof out, "a", "/abs"));
    CHECK_STR(out, "/abs");
    CHECK(path_join(out, sizeof out, NULL, NULL));
    CHECK_STR(out, "");

    strcpy(out, "base");
    CHECK(path_join(out, sizeof out, out, "file.txt"));
    CHECK_STR(out, "base/file.txt");

    char tiny[4];
    CHECK(!path_join(tiny, sizeof tiny, "ab", "c"));
    CHECK_STR(tiny, "");
}

static void test_absolute(void)
{
    char cwd[1024];
    char want[1200];
    char out[1200];
    CHECK(getcwd(cwd, sizeof cwd) != NULL);
    for (char *p = cwd; *p; p++)
    {
        if (*p == '\\')
            *p = '/';
    }

    CHECK(path_is_absolute("/x"));
    CHECK(!path_is_absolute("x"));
    CHECK(!path_is_absolute(""));
    CHECK(!path_is_absolute(NULL));

    CHECK(path_absolute(out, sizeof out, "/a/../b"));
    CHECK_STR(out, "/b");

    snprintf(want, sizeof want, "%s/sub/f.txt", cwd);
    CHECK(path_absolute(out, sizeof out, "./sub/x/../f.txt"));
    CHECK_STR(out, want);

    CHECK(path_absolute(out, sizeof out, "."));
    CHECK_STR(out, cwd);
}

static void test_relative(void)
{
    static const char *cases[][3] = {
        {"/a/b/c", "/a", "b/c"},
        {"/a", "/a/b/c", "../.."},
        {"/a/b", "/a/b", "."},
        {"/a/x/y", "/a/b/c", "../../x/y"},
        {"/a/bc", "/a/b", "../bc"},
        {"/", "/a", ".."},
        {"/a", "/", "a"},
        {"/p/assets/../assets/t.png", "/p/./", "assets/t.png"},
    };
    char out[256];
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        CHECK(path_relative(out, sizeof out, cases[i][0], cases[i][1]));
        CHECK_STR(out, cases[i][2]);
    }

    CHECK(path_relative(out, sizeof out, "sub/f.txt", "."));
    CHECK_STR(out, "sub/f.txt");
}

static void test_error(void)
{
    char tiny[2];
    platform_clear_error();
    CHECK_STR(platform_get_error(), "");
    CHECK(!path_join(tiny, sizeof tiny, "a", "b"));
    CHECK(platform_get_error()[0] != '\0');
    platform_clear_error();
    CHECK_STR(platform_get_error(), "");
}

int main(void)
{
    test_normalize();
    test_join();
    test_absolute();
    test_relative();
    test_error();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
