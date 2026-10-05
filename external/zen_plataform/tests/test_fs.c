/*
 * test_fs.c - PHASE 1 (fs). Exercises os.c headless: file round-trips, queries,
 * path-string helpers, recursive dir make/list, and the asset_* read path against
 * a configured root. Runs in the test's working directory, cleaning up after.
 */
#include "platform.h"

#include <stdio.h>
#include <string.h>

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

#define ROOT "test_fs_tmp"

static void test_file_roundtrip(void)
{
    const char bytes[] = {0x00, 0x01, 0x02, 'z', (char)0xFF};
    CHECK(file_write(ROOT "/a.bin", bytes, sizeof bytes));
    CHECK(file_exists(ROOT "/a.bin"));
    CHECK(!file_exists(ROOT "/missing.bin"));
    CHECK(file_size(ROOT "/a.bin") == (int64_t)sizeof bytes);

    size_t n = 0;
    uint8_t *back = file_read(ROOT "/a.bin", &n);
    CHECK(back != NULL);
    CHECK(n == sizeof bytes);
    CHECK(back && memcmp(back, bytes, sizeof bytes) == 0);
    fs_free(back);

    CHECK(file_write_text(ROOT "/note.txt", "hello"));
    char *txt = file_read_text(ROOT "/note.txt");
    CHECK(txt && strcmp(txt, "hello") == 0); /* NUL-terminated */
    fs_free(txt);

    CHECK(file_mod_time(ROOT "/a.bin") > 0);
}

static void test_path_helpers(void)
{
    CHECK(strcmp(path_filename("a/b/c.txt"), "c.txt") == 0);
    CHECK(strcmp(path_filename("noslash"), "noslash") == 0);
    CHECK(strcmp(path_extension("a/b/c.txt"), ".txt") == 0);
    CHECK(strcmp(path_extension("a/b/c"), "") == 0);
    CHECK(strcmp(path_extension(".hidden"), "") == 0); /* leading dot is not an extension */

    char dir[256];
    path_directory("a/b/c.txt", dir, sizeof dir);
    CHECK(strcmp(dir, "a/b") == 0);
    path_directory("noslash", dir, sizeof dir);
    CHECK(dir[0] == '\0');

    CHECK(path_has_extension("image.PNG", "png")); /* case-insensitive */
    CHECK(path_has_extension("image.png", ".png"));
    CHECK(!path_has_extension("image.png", "jpg"));
}

static void test_directories(void)
{
    CHECK(dir_make(ROOT "/sub/deep"));
    CHECK(dir_exists(ROOT "/sub/deep"));
    CHECK(!dir_exists(ROOT "/sub/none"));
    CHECK(dir_make(ROOT "/sub/deep")); /* idempotent */

    CHECK(file_write_text(ROOT "/sub/one.txt", "1"));
    CHECK(file_write_text(ROOT "/sub/two.txt", "2"));

    DirList list;
    CHECK(dir_list(ROOT "/sub", &list));
    int files = 0;
    bool saw_deep = false;
    for (int i = 0; i < list.count; i++)
    {
        const char *name = path_filename(list.paths[i]);
        if (strcmp(name, "one.txt") == 0 || strcmp(name, "two.txt") == 0)
            files++;
        if (strcmp(name, "deep") == 0)
            saw_deep = true;
    }
    CHECK(files == 2);
    CHECK(saw_deep);
    dir_list_free(&list);

    const char *cwd = dir_current();
    CHECK(cwd && cwd[0] != '\0');
    const char *app = dir_app();
    CHECK(app && app[0] != '\0');
}

static void test_assets(void)
{
    CHECK(file_write_text(ROOT "/hello.txt", "from asset root"));
    asset_set_root(ROOT);
    CHECK(asset_exists("hello.txt"));
    CHECK(!asset_exists("nope.txt"));
    char *txt = asset_read_text("hello.txt");
    CHECK(txt && strcmp(txt, "from asset root") == 0);
    fs_free(txt);
    asset_set_root(NULL);
}

static void cleanup(void)
{
    remove(ROOT "/a.bin");
    remove(ROOT "/note.txt");
    remove(ROOT "/hello.txt");
    remove(ROOT "/sub/one.txt");
    remove(ROOT "/sub/two.txt");
    remove(ROOT "/sub/deep");
    remove(ROOT "/sub");
    remove(ROOT);
}

int main(void)
{
    dir_make(ROOT);

    test_file_roundtrip();
    test_path_helpers();
    test_directories();
    test_assets();

    cleanup();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
