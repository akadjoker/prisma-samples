#define _POSIX_C_SOURCE 200809L

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <direct.h>
#include <stdlib.h>
#define getcwd _getcwd

static int setenv(const char *name, const char *value, int overwrite)
{
    (void)overwrite;
    return _putenv_s(name, value);
}

static int unsetenv(const char *name)
{
    return _putenv_s(name, "");
}

#define LINKS 0
#else
#include <unistd.h>
#define LINKS 3
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

#define ROOT "test_fsys_tmp"

static char g_abs[1024];

static void test_path_info(void)
{
    PathInfo info;
    CHECK(file_write_text(ROOT "/f.txt", "12345"));
    CHECK(fs_get_path_info(ROOT "/f.txt", &info));
    CHECK(info.type == PATH_TYPE_FILE);
    CHECK(info.size == 5);
    int64_t now = (int64_t)time(NULL) * 1000000000;
    CHECK(info.modify_time_ns > now - 60 * 1000000000LL && info.modify_time_ns < now + 60 * 1000000000LL);

    CHECK(fs_get_path_info(ROOT, &info));
    CHECK(info.type == PATH_TYPE_DIRECTORY);
    CHECK(fs_get_path_info(ROOT "/f.txt", NULL));

    platform_clear_error();
    CHECK(!fs_get_path_info(ROOT "/missing", &info));
    CHECK(strstr(platform_get_error(), "missing") != NULL);
    CHECK(!fs_get_path_info("", &info));
    CHECK(!fs_get_path_info(NULL, &info));

    CHECK(file_mod_time(ROOT "/f.txt") > 0);
    CHECK(file_mod_time(ROOT "/missing") == -1);
    CHECK(file_size(ROOT "/missing") == -1);
    CHECK(file_exists(ROOT "/f.txt") && !file_exists(ROOT));
    CHECK(dir_exists(ROOT) && !dir_exists(ROOT "/f.txt"));
}

static void test_create_remove_rename(void)
{
    CHECK(fs_create_directory(ROOT "/a/b/c"));
    CHECK(dir_exists(ROOT "/a/b/c"));
    CHECK(fs_create_directory(ROOT "/a/b/c"));
    CHECK(fs_create_directory(ROOT "/a/b/c/"));
    CHECK(!fs_create_directory(ROOT "/f.txt"));
    CHECK(!fs_create_directory(ROOT "/f.txt/sub"));
    CHECK(!fs_create_directory(""));

    CHECK(!fs_remove_path(ROOT "/a"));
    CHECK(dir_exists(ROOT "/a"));
    CHECK(!fs_remove_path(ROOT "/nothing"));
    CHECK(fs_remove_path(ROOT "/a/b/c"));
    CHECK(!dir_exists(ROOT "/a/b/c"));

    CHECK(file_write_text(ROOT "/r1.txt", "one"));
    CHECK(file_write_text(ROOT "/r2.txt", "two"));
    CHECK(fs_rename_path(ROOT "/r1.txt", ROOT "/r2.txt"));
    CHECK(!file_exists(ROOT "/r1.txt"));
    char *t = file_read_text(ROOT "/r2.txt");
    CHECK(t && strcmp(t, "one") == 0);
    fs_free(t);
    CHECK(fs_rename_path(ROOT "/a/b", ROOT "/a/b2"));
    CHECK(dir_exists(ROOT "/a/b2") && !dir_exists(ROOT "/a/b"));
    CHECK(!fs_rename_path(ROOT "/nothing", ROOT "/x"));
    CHECK(!fs_rename_path("", ROOT "/x"));
    CHECK(fs_remove_path(ROOT "/r2.txt"));
    CHECK(fs_remove_path(ROOT "/a/b2"));
    CHECK(fs_remove_path(ROOT "/a"));
}

typedef struct
{
    int files, dirs, others, total;
    int stop_after;
    char seen[16][128];
} Seen;

static bool collect(const char *path, PathType type, void *user)
{
    Seen *s = user;
    if (s->total < 16)
        snprintf(s->seen[s->total], sizeof s->seen[0], "%s", path);
    s->total++;
    if (type == PATH_TYPE_FILE)
        s->files++;
    else if (type == PATH_TYPE_DIRECTORY)
        s->dirs++;
    else
        s->others++;
    return s->stop_after == 0 || s->total < s->stop_after;
}

static bool has(const Seen *s, const char *path)
{
    for (int i = 0; i < s->total && i < 16; i++)
        if (strcmp(s->seen[i], path) == 0)
            return true;
    return false;
}

static void test_enumerate(void)
{
    CHECK(fs_create_directory(ROOT "/e/sub/deep"));
    CHECK(file_write_text(ROOT "/e/one.txt", "1"));
    CHECK(file_write_text(ROOT "/e/sub/two.txt", "2"));
    CHECK(file_write_text(ROOT "/e/sub/deep/three.txt", "3"));
#if LINKS
    CHECK(symlink("..", ROOT "/e/sub/up") == 0);
    CHECK(symlink("one.txt", ROOT "/e/link.txt") == 0);
    CHECK(symlink("nowhere", ROOT "/e/dangling") == 0);
#endif

    Seen s;
    memset(&s, 0, sizeof s);
    CHECK(fs_enumerate_directory(ROOT "/e", false, collect, &s));
    CHECK(s.total == 2 + (LINKS ? 2 : 0));
    CHECK(s.files == 1 + (LINKS ? 1 : 0) && s.dirs == 1 && s.others == (LINKS ? 1 : 0));
    CHECK(has(&s, ROOT "/e/one.txt") && has(&s, ROOT "/e/sub"));

    memset(&s, 0, sizeof s);
    CHECK(fs_enumerate_directory(ROOT "/e/", true, collect, &s));
    CHECK(s.total == 5 + (LINKS ? 3 : 0));
    CHECK(s.dirs == 2 + (LINKS ? 1 : 0));
    CHECK(has(&s, ROOT "/e/sub/deep/three.txt"));
    CHECK(has(&s, ROOT "/e/sub/two.txt"));
#if LINKS
    CHECK(has(&s, ROOT "/e/sub/up"));
    CHECK(!has(&s, ROOT "/e/sub/up/one.txt"));
#endif

    memset(&s, 0, sizeof s);
    s.stop_after = 3;
    CHECK(fs_enumerate_directory(ROOT "/e", true, collect, &s));
    CHECK(s.total == 3);

    platform_clear_error();
    memset(&s, 0, sizeof s);
    CHECK(!fs_enumerate_directory(ROOT "/missing", false, collect, &s));
    CHECK(platform_get_error()[0] != '\0');
    CHECK(!fs_enumerate_directory(ROOT "/e/one.txt", false, collect, &s));
    CHECK(!fs_enumerate_directory("", false, collect, &s));
    CHECK(!fs_enumerate_directory(ROOT "/e", false, NULL, &s));
    CHECK(s.total == 0);

    DirList list;
    CHECK(dir_list(ROOT "/e/sub/deep", &list));
    CHECK(list.count == 1 && strcmp(list.paths[0], ROOT "/e/sub/deep/three.txt") == 0);
    dir_list_free(&list);
    CHECK(!dir_list(ROOT "/missing", &list));
    CHECK(list.count == 0 && list.paths == NULL);

#if LINKS
    CHECK(fs_remove_path(ROOT "/e/dangling"));
    CHECK(fs_remove_path(ROOT "/e/link.txt"));
    CHECK(fs_remove_path(ROOT "/e/sub/up"));
#endif
    CHECK(fs_remove_path(ROOT "/e/sub/deep/three.txt"));
    CHECK(fs_remove_path(ROOT "/e/sub/deep"));
    CHECK(fs_remove_path(ROOT "/e/sub/two.txt"));
    CHECK(fs_remove_path(ROOT "/e/sub"));
    CHECK(fs_remove_path(ROOT "/e/one.txt"));
    CHECK(fs_remove_path(ROOT "/e"));
}

static void test_base_path(void)
{
    char base[1024];
    CHECK(fs_get_base_path(base, sizeof base));
    size_t n = strlen(base);
    CHECK(n > 1 && base[n - 1] == '/' && path_is_absolute(base));
    CHECK(dir_exists(base));

    char tiny[4];
    CHECK(!fs_get_base_path(tiny, sizeof tiny));

    const char *app = dir_app();
    CHECK(strncmp(app, base, n - 1) == 0 && strlen(app) == n - 1);
}

#if defined(_WIN32)
static void test_pref_path(void)
{
    char out[1024];
    char want[1200];

    snprintf(want, sizeof want, "%s/" ROOT "/appdata", g_abs);
    CHECK(setenv("APPDATA", want, 1) == 0);
    CHECK(fs_get_pref_path(out, sizeof out, "Org", "App"));
    snprintf(want, sizeof want, "%s/" ROOT "/appdata/Org/App/", g_abs);
    CHECK_STR(out, want);
    CHECK(dir_exists(ROOT "/appdata/Org/App"));
    CHECK(fs_get_pref_path(out, sizeof out, "Org", "App"));
    CHECK_STR(out, want);

    CHECK(fs_get_pref_path(out, sizeof out, "", "Solo"));
    snprintf(want, sizeof want, "%s/" ROOT "/appdata/Solo/", g_abs);
    CHECK_STR(out, want);
    CHECK(fs_get_pref_path(out, sizeof out, NULL, "Solo2"));
    CHECK(dir_exists(ROOT "/appdata/Solo2"));

    CHECK(!fs_get_pref_path(out, sizeof out, "Org", ""));
    CHECK(!fs_get_pref_path(out, sizeof out, "Org", NULL));
    CHECK(!fs_get_pref_path(out, sizeof out, "Org", ".."));
    CHECK(!fs_get_pref_path(out, sizeof out, "../x", "App"));
    CHECK(!fs_get_pref_path(out, sizeof out, "Org", "a/b"));
    CHECK(!fs_get_pref_path(out, sizeof out, "Org", "a:b"));
    CHECK(out[0] == '\0');
    CHECK(!fs_get_pref_path(out, 8, "Org", "App"));

    unsetenv("APPDATA");
    CHECK(!fs_get_pref_path(out, sizeof out, "Org", "App"));
    CHECK(strstr(platform_get_error(), "APPDATA") != NULL);
}

static void test_temp_path(void)
{
    char out[1024];
    char want[1200];
    snprintf(want, sizeof want, "%s/" ROOT "/tmp/", g_abs);
    for (char *p = want; *p; p++)
    {
        if (*p == '/')
            *p = '\\';
    }
    CHECK(setenv("TMP", want, 1) == 0);
    CHECK(fs_get_temp_path(out, sizeof out));
    snprintf(want, sizeof want, "%s/" ROOT "/tmp", g_abs);
    CHECK_STR(out, want);
    CHECK(!fs_get_temp_path(out, 3));
}
#else
static void test_pref_path(void)
{
    char out[1024];
    char want[1200];

    snprintf(want, sizeof want, "%s/" ROOT "/xdg", g_abs);
    setenv("XDG_DATA_HOME", want, 1);
    CHECK(fs_get_pref_path(out, sizeof out, "Org", "App"));
    snprintf(want, sizeof want, "%s/" ROOT "/xdg/Org/App/", g_abs);
    CHECK_STR(out, want);
    CHECK(dir_exists(ROOT "/xdg/Org/App"));
    CHECK(fs_get_pref_path(out, sizeof out, "Org", "App"));
    CHECK_STR(out, want);

    CHECK(fs_get_pref_path(out, sizeof out, "", "Solo"));
    snprintf(want, sizeof want, "%s/" ROOT "/xdg/Solo/", g_abs);
    CHECK_STR(out, want);
    CHECK(fs_get_pref_path(out, sizeof out, NULL, "Solo2"));
    CHECK(dir_exists(ROOT "/xdg/Solo2"));

    CHECK(!fs_get_pref_path(out, sizeof out, "Org", ""));
    CHECK(!fs_get_pref_path(out, sizeof out, "Org", NULL));
    CHECK(!fs_get_pref_path(out, sizeof out, "Org", ".."));
    CHECK(!fs_get_pref_path(out, sizeof out, "../x", "App"));
    CHECK(!fs_get_pref_path(out, sizeof out, "Org", "a/b"));
    CHECK(out[0] == '\0');
    CHECK(!fs_get_pref_path(out, 8, "Org", "App"));

    unsetenv("XDG_DATA_HOME");
    snprintf(want, sizeof want, "%s/" ROOT "/home", g_abs);
    setenv("HOME", want, 1);
    CHECK(fs_get_pref_path(out, sizeof out, "Org", "App"));
    snprintf(want, sizeof want, "%s/" ROOT "/home/.local/share/Org/App/", g_abs);
    CHECK_STR(out, want);

    setenv("XDG_DATA_HOME", "relative/path", 1);
    CHECK(fs_get_pref_path(out, sizeof out, "Org", "App"));
    CHECK_STR(out, want);

    unsetenv("XDG_DATA_HOME");
    unsetenv("HOME");
    CHECK(!fs_get_pref_path(out, sizeof out, "Org", "App"));
    CHECK(strstr(platform_get_error(), "HOME") != NULL);
}

static void test_temp_path(void)
{
    char out[256];
    setenv("TMPDIR", "/var/tmp/", 1);
    CHECK(fs_get_temp_path(out, sizeof out));
    CHECK_STR(out, "/var/tmp");
    setenv("TMPDIR", "relative", 1);
    CHECK(fs_get_temp_path(out, sizeof out));
    CHECK_STR(out, "/tmp");
    unsetenv("TMPDIR");
    CHECK(fs_get_temp_path(out, sizeof out));
    CHECK_STR(out, "/tmp");
    CHECK(!fs_get_temp_path(out, 3));
}

#endif

static void cleanup(void)
{
    static const char *dirs[] = {
        ROOT "/appdata/Org/App", ROOT "/appdata/Org", ROOT "/appdata/Solo", ROOT "/appdata/Solo2", ROOT "/appdata", ROOT "/tmp",
        ROOT "/xdg/Org/App", ROOT "/xdg/Org", ROOT "/xdg/Solo", ROOT "/xdg/Solo2", ROOT "/xdg",
        ROOT "/home/.local/share/Org/App", ROOT "/home/.local/share/Org", ROOT "/home/.local/share",
        ROOT "/home/.local", ROOT "/home", ROOT "/f.txt", ROOT};
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++)
        fs_remove_path(dirs[i]);
}

int main(void)
{
    if (!getcwd(g_abs, sizeof g_abs))
        return 1;
    for (char *p = g_abs; *p; p++)
    {
        if (*p == '\\')
            *p = '/';
    }
    fs_create_directory(ROOT);

    test_path_info();
    test_create_remove_rename();
    test_enumerate();
    test_base_path();
    test_pref_path();
    test_temp_path();

    cleanup();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
