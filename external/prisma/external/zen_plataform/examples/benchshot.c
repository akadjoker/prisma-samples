#include "platform.h"
#include "zen_2d.h"
#include <glad/gl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
int main(void)
{
    platform_init();
    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 900;
    cfg.height = 560;
    cfg.render = RENDER_GL;
    cfg.gl.major = 3;
    cfg.gl.minor = 3;
    cfg.vsync = false;
    PlatformWindow *w = window_create(&cfg);
    window_make_current(w);
    if (!r2d_init())
    {
        printf("init fail\n");
        return 1;
    }
    int W, H;
    window_get_framebuffer_size(w, &W, &H);
    /* a 16x16 checker sprite */
    uint32_t px[16 * 16];
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            int c = ((x / 4) + (y / 4)) & 1;
            px[y * 16 + x] = c ? 0xFFEC6A5A : 0xFFFFD166;
        }
    R2dTexture tex = r2d_texture_create(px, 16, 16);

    window_begin_frame(w);
    glClearColor(0.07f, 0.08f, 0.10f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    r2d_begin(W, H);
    int N = 4000;
    for (int i = 0; i < N; i++)
    {
        float a = i * 0.121f, r = 20 + (i % 260);
        float x = W * 0.5f + cosf(a) * r, y = H * 0.5f + sinf(a) * r;
        r2d_sprite(tex, 0, 0, 16, 16, x, y, 8, 8, 1.2f, 1.2f, (float)(i % 360), (i & 1) ? R2D_FLIP_X : 0, 0xFFFFFFFF);
    }
    unsigned draws = r2d_draw_calls();
    r2d_end();

    /* count lit (non-background) pixels to prove rasterization happened */
    uint32_t *buf = malloc((size_t)W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    long lit = 0;
    for (long i = 0; i < (long)W * H; i++)
    {
        uint32_t p = buf[i] & 0x00FFFFFF;
        if (p > 0x141414)
            lit++;
    }
    printf("drew %d sprites in %u draw call(s); %ld lit pixels (%.1f%% of screen)\n",
           N, draws, lit, 100.0 * lit / ((double)W * H));
    /* save a screenshot (flip vertically; readback is RGBA, our BMP wants ARGB-ish) */
    Framebuffer fb;
    framebuffer_alloc(&fb, W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            uint32_t p = buf[(size_t)(H - 1 - y) * W + x];
            uint32_t r = p & 0xFF, g = (p >> 8) & 0xFF, b = (p >> 16) & 0xFF;
            fb.pixels[(size_t)y * W + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    framebuffer_save_bmp(&fb, "benchshot.bmp");
    window_swap(w); /* present so it is visibly on screen too */
    free(buf);
    framebuffer_free(&fb);
    r2d_shutdown();
    window_destroy(w);
    platform_shutdown();
    return 0;
}
