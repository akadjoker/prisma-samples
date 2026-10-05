/*
 * test_draw2d.c - the software rasterizer, headless. Draws into a heap Framebuffer
 * and asserts pixels: clear, pixel/clip, alpha blend, rect/line/circle/triangle,
 * and nearest/bilinear blit.
 */
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>

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

static Framebuffer make_fb(int w, int h)
{
    Framebuffer fb;
    fb.width = w;
    fb.height = h;
    fb.stride = w;
    fb.pixels = calloc((size_t)w * h, sizeof *fb.pixels);
    return fb;
}

static void test_clear_pixel(void)
{
    Framebuffer fb = make_fb(8, 8);
    draw_clear(&fb, 0xFF112233);
    CHECK(draw_get_pixel(&fb, 0, 0) == 0xFF112233);
    CHECK(draw_get_pixel(&fb, 7, 7) == 0xFF112233);

    draw_pixel(&fb, 3, 4, 0xFFAABBCC, BLEND_NONE);
    CHECK(draw_get_pixel(&fb, 3, 4) == 0xFFAABBCC);

    /* out of bounds must not crash and reads as 0 */
    draw_pixel(&fb, -1, -1, 0xFFFFFFFF, BLEND_NONE);
    draw_pixel(&fb, 100, 100, 0xFFFFFFFF, BLEND_NONE);
    CHECK(draw_get_pixel(&fb, -1, 0) == 0);
    CHECK(draw_get_pixel(&fb, 0, 99) == 0);
    free(fb.pixels);
}

static void test_blend(void)
{
    Framebuffer fb = make_fb(4, 4);
    draw_clear(&fb, 0xFF000000);

    draw_pixel(&fb, 0, 0, 0x80FFFFFF, BLEND_ALPHA); /* 50% white over black */
    uint32_t p = draw_get_pixel(&fb, 0, 0);
    CHECK(((p >> 16) & 0xFF) == 128 && ((p >> 8) & 0xFF) == 128 && (p & 0xFF) == 128);
    CHECK((p >> 24) == 0xFF);

    draw_pixel(&fb, 1, 1, 0x00FF0000, BLEND_ALPHA); /* fully transparent */
    CHECK(draw_get_pixel(&fb, 1, 1) == 0xFF000000);

    draw_pixel(&fb, 2, 2, 0xFFFF0000, BLEND_ALPHA); /* opaque red */
    CHECK(draw_get_pixel(&fb, 2, 2) == 0xFFFF0000);
    free(fb.pixels);
}

static void test_rect_line(void)
{
    Framebuffer fb = make_fb(10, 10);
    draw_clear(&fb, 0xFF000000);

    draw_fill_rect(&fb, 2, 2, 3, 3, 0xFF00FF00, BLEND_NONE);
    CHECK(draw_get_pixel(&fb, 2, 2) == 0xFF00FF00);
    CHECK(draw_get_pixel(&fb, 4, 4) == 0xFF00FF00);
    CHECK(draw_get_pixel(&fb, 5, 5) == 0xFF000000); /* just outside */
    CHECK(draw_get_pixel(&fb, 1, 1) == 0xFF000000);

    /* clipped fill: negative origin */
    draw_fill_rect(&fb, -2, -2, 4, 4, 0xFF0000FF, BLEND_NONE);
    CHECK(draw_get_pixel(&fb, 0, 0) == 0xFF0000FF);
    CHECK(draw_get_pixel(&fb, 1, 1) == 0xFF0000FF);

    draw_line(&fb, 0, 9, 9, 9, 0xFFFFFFFF, BLEND_NONE);
    CHECK(draw_get_pixel(&fb, 0, 9) == 0xFFFFFFFF);
    CHECK(draw_get_pixel(&fb, 5, 9) == 0xFFFFFFFF);
    CHECK(draw_get_pixel(&fb, 9, 9) == 0xFFFFFFFF);

    draw_rect(&fb, 0, 0, 10, 10, 0xFFAAAAAA, BLEND_NONE);
    CHECK(draw_get_pixel(&fb, 0, 0) == 0xFFAAAAAA); /* corner on the outline */
    CHECK(draw_get_pixel(&fb, 9, 0) == 0xFFAAAAAA);
    free(fb.pixels);
}

static void test_circle_triangle(void)
{
    Framebuffer fb = make_fb(21, 21);
    draw_clear(&fb, 0xFF000000);

    draw_fill_circle(&fb, 10, 10, 6, 0xFFFF00FF, BLEND_NONE);
    CHECK(draw_get_pixel(&fb, 10, 10) == 0xFFFF00FF); /* center */
    CHECK(draw_get_pixel(&fb, 14, 10) == 0xFFFF00FF); /* inside radius */
    CHECK(draw_get_pixel(&fb, 18, 10) == 0xFF000000); /* beyond radius */

    draw_circle(&fb, 10, 10, 6, 0xFF00FFFF, BLEND_NONE);
    CHECK(draw_get_pixel(&fb, 16, 10) == 0xFF00FFFF); /* cardinal point on the ring */

    Framebuffer t = make_fb(8, 8);
    draw_clear(&t, 0xFF000000);
    draw_fill_triangle(&t, 0, 0, 6, 0, 0, 6, 0xFFFFFFFF, BLEND_NONE);
    CHECK(draw_get_pixel(&t, 1, 1) == 0xFFFFFFFF); /* inside */
    CHECK(draw_get_pixel(&t, 5, 5) == 0xFF000000); /* outside the hypotenuse */
    free(t.pixels);
    free(fb.pixels);
}

static void test_blit(void)
{
    Framebuffer src = make_fb(2, 2);
    src.pixels[0] = 0xFF000000; /* (0,0) black */
    src.pixels[1] = 0xFFFFFFFF; /* (1,0) white */
    src.pixels[2] = 0xFFFFFFFF; /* (0,1) white */
    src.pixels[3] = 0xFF000000; /* (1,1) black */

    /* nearest 2x upscale: each source texel becomes a 2x2 block */
    Framebuffer dn = make_fb(4, 4);
    draw_blit(&dn, &src, 0, 0, 2, 2, 0, 0, 4, 4, BLEND_NONE, SCALE_NEAREST);
    CHECK(draw_get_pixel(&dn, 0, 0) == 0xFF000000);
    CHECK(draw_get_pixel(&dn, 1, 1) == 0xFF000000);
    CHECK(draw_get_pixel(&dn, 3, 0) == 0xFFFFFFFF);
    CHECK(draw_get_pixel(&dn, 3, 3) == 0xFF000000);

    /* bilinear upscale: a pixel between black and white is a grey */
    Framebuffer db = make_fb(8, 8);
    draw_blit(&db, &src, 0, 0, 2, 2, 0, 0, 8, 8, BLEND_NONE, SCALE_BILINEAR);
    uint32_t mid = draw_get_pixel(&db, 4, 0); /* along the top edge, black->white */
    int r = (mid >> 16) & 0xFF;
    CHECK(r > 40 && r < 215); /* genuinely interpolated, not a hard edge */

    free(src.pixels);
    free(dn.pixels);
    free(db.pixels);
}

int main(void)
{
    test_clear_pixel();
    test_blend();
    test_rect_line();
    test_circle_triangle();
    test_blit();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
