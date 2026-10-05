#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
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

#define ROOT "test_io_tmp"

static void test_memory(void)
{
    const char data[] = "0123456789";
    char buf[16];
    IoStream *s = io_open_memory(data, 10);
    CHECK(s != NULL);
    CHECK(io_size(s) == 10);
    CHECK(io_read(s, buf, 4) == 4 && memcmp(buf, "0123", 4) == 0);
    CHECK(io_tell(s) == 4);
    CHECK(!io_eof(s));
    CHECK(io_seek(s, -2, IO_SEEK_END) == 8);
    CHECK(io_read(s, buf, 8) == 2 && memcmp(buf, "89", 2) == 0);
    CHECK(io_eof(s));
    CHECK(io_seek(s, 1, IO_SEEK_SET) == 1);
    CHECK(!io_eof(s));
    CHECK(io_seek(s, 2, IO_SEEK_CUR) == 3);
    CHECK(io_seek(s, 11, IO_SEEK_SET) == -1);
    CHECK(io_seek(s, -1, IO_SEEK_SET) == -1);
    CHECK(io_tell(s) == 3);
    CHECK(io_write(s, "x", 1) == 0);
    CHECK(platform_get_error()[0] != '\0');
    CHECK(io_close(s));

    size_t n = 0;
    s = io_open_memory(data, 10);
    io_seek(s, 6, IO_SEEK_SET);
    char *rest = io_load(s, &n, true);
    CHECK(rest && n == 4 && strcmp(rest, "6789") == 0);
    fs_free(rest);

    CHECK(io_open_memory(NULL, 4) == NULL);
    s = io_open_memory(NULL, 0);
    CHECK(s && io_size(s) == 0);
    io_close(s);
}

static void test_file_modes(void)
{
    char buf[32];
    IoStream *s = io_open_file(ROOT "/m.bin", "wb");
    CHECK(s != NULL);
    CHECK(io_write(s, "hello", 5) == 5);
    CHECK(io_size(s) == 5);
    CHECK(io_close(s));

    s = io_open_file(ROOT "/m.bin", "ab");
    CHECK(io_write(s, " world", 6) == 6);
    io_close(s);

    s = io_open_file(ROOT "/m.bin", "rb");
    CHECK(io_size(s) == 11);
    CHECK(io_read(s, buf, sizeof buf) == 11 && memcmp(buf, "hello world", 11) == 0);
    CHECK(io_eof(s));
    CHECK(io_write(s, "x", 1) == 0);
    CHECK(io_seek(s, 6, IO_SEEK_SET) == 6);
    CHECK(!io_eof(s));
    CHECK(io_read(s, buf, 5) == 5 && memcmp(buf, "world", 5) == 0);
    io_close(s);

    s = io_open_file(ROOT "/m.bin", "r+b");
    io_seek(s, 0, IO_SEEK_SET);
    CHECK(io_write(s, "J", 1) == 1);
    CHECK(io_flush(s));
    io_close(s);

    s = io_open_file(ROOT "/m.bin", "w+");
    CHECK(io_size(s) == 0);
    CHECK(io_write(s, "abc", 3) == 3);
    CHECK(io_seek(s, 0, IO_SEEK_SET) == 0);
    CHECK(io_read(s, buf, 3) == 3 && memcmp(buf, "abc", 3) == 0);
    io_close(s);
}

static void test_open_errors(void)
{
    platform_clear_error();
    CHECK(io_open_file(ROOT "/missing.bin", "rb") == NULL);
    CHECK(strstr(platform_get_error(), "missing.bin") != NULL);
    CHECK(io_open_file(ROOT "/m.bin", "x") == NULL);
    CHECK(io_open_file(ROOT "/m.bin", "rw") == NULL);
    CHECK(io_open_file(ROOT "/m.bin", NULL) == NULL);
    CHECK(io_open_file("", "rb") == NULL);
    CHECK(io_open_file(NULL, "rb") == NULL);
    CHECK(io_open_file(ROOT, "rb") == NULL);
    CHECK(io_close(NULL) == false);
}

static void test_load_save(void)
{
    size_t n = 99;
    CHECK(io_save_file(ROOT "/empty.bin", NULL, 0));
    char *e = io_load_file(ROOT "/empty.bin", &n);
    CHECK(e && n == 0 && e[0] == '\0');
    fs_free(e);

    CHECK(io_save_file(ROOT "/s.txt", "first", 5));
    CHECK(io_save_file(ROOT "/s.txt", "second", 6));
    char *t = io_load_file(ROOT "/s.txt", &n);
    CHECK(t && n == 6 && strcmp(t, "second") == 0);
    fs_free(t);
    CHECK(!file_exists(ROOT "/s.txt.tmp"));

    size_t big = (1u << 20) + 123;
    unsigned char *src = malloc(big);
    for (size_t i = 0; i < big; i++)
        src[i] = (unsigned char)(i * 31u + 7u);
    CHECK(io_save_file(ROOT "/big.bin", src, big));
    unsigned char *back = io_load_file(ROOT "/big.bin", &n);
    CHECK(back && n == big && memcmp(back, src, big) == 0);
    fs_free(back);
    free(src);

    CHECK(!io_save_file(ROOT "/nodir/x.bin", "x", 1));
    CHECK(!file_exists(ROOT "/nodir/x.bin.tmp"));
    CHECK(io_load_file(ROOT "/nodir/x.bin", &n) == NULL);
    CHECK(!io_save_file(NULL, "x", 1));
    CHECK(!io_save_file(ROOT "/n.bin", NULL, 3));
}

static void test_assets(void)
{
    CHECK(io_save_file(ROOT "/asset.txt", "packed", 6));
    asset_set_root(ROOT);
    IoStream *s = io_open_asset("asset.txt");
    CHECK(s != NULL);
    CHECK(io_size(s) == 6);
    CHECK(io_write(s, "x", 1) == 0);
    io_close(s);
    size_t n = 0;
    uint8_t *a = asset_read("asset.txt", &n);
    CHECK(a && n == 6 && memcmp(a, "packed", 6) == 0);
    fs_free(a);
    CHECK(io_open_asset("none.txt") == NULL);
    CHECK(io_open_asset("") == NULL);
    asset_set_root(NULL);
}

static void cleanup(void)
{
    remove(ROOT "/m.bin");
    remove(ROOT "/empty.bin");
    remove(ROOT "/s.txt");
    remove(ROOT "/big.bin");
    remove(ROOT "/asset.txt");
    remove(ROOT);
}

int main(void)
{
    dir_make(ROOT);

    test_memory();
    test_file_modes();
    test_open_errors();
    test_load_save();
    test_assets();

    cleanup();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
