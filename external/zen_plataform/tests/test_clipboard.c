/*
 * test_clipboard.c - the clipboard API over the fake backend's in-memory store:
 * text, several representations, MIME matching, and PNG images through the
 * encoder and decoder.
 */
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void test_text(void)
{
    clipboard_set(NULL);
    CHECK(strcmp(clipboard_get(), "") == 0); /* never NULL */
    CHECK(!clipboard_has_data(CLIPBOARD_TEXT));

    clipboard_set("zen plataforma \xC3\xA7");
    CHECK(strcmp(clipboard_get(), "zen plataforma \xC3\xA7") == 0);
    CHECK(clipboard_has_data(CLIPBOARD_TEXT));

    clipboard_set("");
    CHECK(strcmp(clipboard_get(), "") == 0);

    clipboard_set(NULL);
    CHECK(!clipboard_has_data(CLIPBOARD_TEXT));
}

static void test_items(void)
{
    const char html[] = "<b>hi</b>";
    const uint8_t blob[4] = {0, 1, 2, 3}; /* binary, with a zero byte */
    ClipboardItem items[] = {
        {CLIPBOARD_TEXT, "hi", 2},
        {"text/html", html, sizeof html - 1},
        {"application/x-zen", blob, sizeof blob},
    };
    CHECK(clipboard_set_items(items, 3));

    CHECK(clipboard_has_data("text/html"));
    CHECK(clipboard_has_data("application/x-zen"));
    CHECK(!clipboard_has_data("image/png"));

    size_t n = 99;
    char *h = clipboard_get_data("text/html", &n);
    CHECK(h && n == sizeof html - 1 && memcmp(h, html, n) == 0);
    CHECK(h && h[n] == '\0'); /* the NUL after the data */
    fs_free(h);

    uint8_t *b = clipboard_get_data("application/x-zen", &n);
    CHECK(b && n == 4 && memcmp(b, blob, 4) == 0);
    fs_free(b);

    n = 99;
    CHECK(clipboard_get_data("image/png", &n) == NULL);
    CHECK(n == 0);

    /* the text API sees the text representation */
    CHECK(strcmp(clipboard_get(), "hi") == 0);

    /* types match ignoring case and a ;parameter tail */
    CHECK(clipboard_has_data("TEXT/PLAIN;charset=utf-8"));
    CHECK(clipboard_has_data("Text/Html"));

    /* setting again replaces everything */
    clipboard_set("only text");
    CHECK(!clipboard_has_data("text/html"));
    CHECK(clipboard_has_data(CLIPBOARD_TEXT));

    CHECK(clipboard_set_items(NULL, 0)); /* clear */
    CHECK(!clipboard_has_data(CLIPBOARD_TEXT));
}

static void test_bad_arguments(void)
{
    CHECK(!clipboard_set_data(NULL, "x", 1));
    CHECK(!clipboard_set_data("", "x", 1));
    CHECK(!clipboard_set_data("text/plain", NULL, 3)); /* size without data */
    CHECK(clipboard_set_data("application/x-empty", NULL, 0)); /* empty is fine */
    size_t n = 5;
    void *e = clipboard_get_data("application/x-empty", &n);
    CHECK(e != NULL && n == 0);
    fs_free(e);
    CHECK(!clipboard_has_data(NULL));
    CHECK(clipboard_get_data(NULL, &n) == NULL);
    CHECK(!clipboard_set_items(NULL, 2));
    clipboard_set(NULL);
}

static uint32_t next_pixel(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed; /* every channel, alpha included, is exercised */
}

static void check_image_round_trip(int w, int h, uint32_t seed)
{
    Framebuffer src;
    CHECK(framebuffer_alloc(&src, w, h));
    for (int i = 0; i < w * h; i++)
        src.pixels[i] = next_pixel(&seed);

    CHECK(clipboard_set_image(&src));
    CHECK(clipboard_has_data(CLIPBOARD_PNG));

    size_t n;
    uint8_t *png = clipboard_get_data(CLIPBOARD_PNG, &n);
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(png && n > 8 && memcmp(png, sig, 8) == 0);
    fs_free(png);

    Framebuffer out = {0};
    CHECK(clipboard_get_image(&out));
    CHECK(out.width == w && out.height == h);
    bool same = out.pixels != NULL;
    for (int i = 0; same && i < w * h; i++)
        same = out.pixels[i] == src.pixels[i];
    CHECK(same);

    framebuffer_free(&out);
    framebuffer_free(&src);
}

static void test_images(void)
{
    check_image_round_trip(1, 1, 7);
    check_image_round_trip(3, 2, 11);
    check_image_round_trip(255, 3, 13);
    /* 300x200x4 = 240 KB: several 64 KB stored blocks, and a row that straddles one */
    check_image_round_trip(300, 200, 17);

    /* a source with a stride wider than the width is read row by row */
    uint32_t px[4 * 3];
    for (int i = 0; i < 12; i++)
        px[i] = 0xFF000000u | (uint32_t)i * 0x010101u;
    Framebuffer view = {.pixels = px, .width = 3, .height = 3, .stride = 4};
    CHECK(clipboard_set_image(&view));
    Framebuffer out = {0};
    CHECK(clipboard_get_image(&out));
    CHECK(out.width == 3 && out.height == 3);
    CHECK(out.pixels[0] == px[0] && out.pixels[3 * 1 + 2] == px[4 * 1 + 2] && out.pixels[3 * 2 + 1] == px[4 * 2 + 1]);
    framebuffer_free(&out);

    /* no image, or bytes that are not a PNG, fail cleanly */
    clipboard_set("text only");
    CHECK(!clipboard_get_image(&out));
    CHECK(strstr(platform_get_error(), "no image") != NULL);

    CHECK(clipboard_set_data(CLIPBOARD_PNG, "this is not a png", 17));
    CHECK(!clipboard_get_image(&out));

    Framebuffer empty = {0};
    CHECK(!clipboard_set_image(&empty));
    CHECK(!clipboard_set_image(NULL));
    clipboard_set(NULL);
}

int main(void)
{
    if (!platform_init())
        return 1;
    test_text();
    test_items();
    test_bad_arguments();
    test_images();
    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
