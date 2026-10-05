/*
 * bench_sprite.c - benchmark focused on the sprite batcher (r2d_sprite).
 *
 * Measures draw-call efficiency and throughput for several real-world
 * scenarios: same-texture batches, texture switches, clip changes, flip,
 * and interleaved shapes.  Good batcher behaviour is 1 draw call for
 * hundreds of thousands of sprites sharing one texture.
 */
#include "platform.h"
#include "zen_2d.h"
#include <glad/gl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PlatformWindow *w;
static int W, H;
static R2dTexture tex16, tex32;

/* ------------------------------------------------------------------ */
/* benchmark harness (same pattern as examples/bench.c)                */
/* ------------------------------------------------------------------ */
static double run(const char *name, int count, int frames,
                  void (*draw)(int), int *out_draws)
{
    /* warmup */
    for (int f = 0; f < 3; f++)
    {
        window_begin_frame(w);
        glClear(GL_COLOR_BUFFER_BIT);
        r2d_begin(W, H);
        draw(count);
        r2d_end();
        glFinish();
    }
    double t0 = time_seconds();
    unsigned dc = 0;
    for (int f = 0; f < frames; f++)
    {
        window_begin_frame(w);
        glClear(GL_COLOR_BUFFER_BIT);
        r2d_begin(W, H);
        draw(count);
        r2d_end();
        glFinish();
        dc = r2d_draw_calls();
    }
    double t = time_seconds() - t0;
    double mspf = 1000.0 * t / frames;
    double fps  = frames / t;
    double max60 = count * (16.6667 / mspf);
    double batch_eff = dc ? (double)count / dc : (double)count;
    printf("%-29s %8d  %6u  %9.1f  %7.2f  %7.0f  %7.0fk\n",
           name, count, dc, batch_eff, mspf, fps, max60 / 1000.0);
    if (out_draws) *out_draws = (int)dc;
    return mspf;
}

/* ------------------------------------------------------------------ */
/* scenario drawing callbacks                                          */
/* ------------------------------------------------------------------ */

/* 1) All sprites share tex16, no rotation / scale → 1 draw call */
static void d_same_tex(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W, y = (i * 53) % H;
        r2d_sprite(tex16, 0, 0, 16, 16, x, y, 8, 8, 1, 1, 0, 0, 0xFFFFFFFF);
    }
}

/* 2) Same texture, with rotation → still 1 draw call */
static void d_same_tex_rot(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W, y = (i * 53) % H;
        r2d_sprite(tex16, 0, 0, 16, 16, x, y, 8, 8, 1, 1,
                   (float)(i % 360), 0, 0xFFFFFFFF);
    }
}

/* 3) Same texture, flip X/Y alternating → 1 draw call */
static void d_flip(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W, y = (i * 53) % H;
        int flip = (i % 3 == 0) ? R2D_FLIP_X :
                   (i % 3 == 1) ? R2D_FLIP_Y : R2D_FLIP_NONE;
        r2d_sprite(tex16, 0, 0, 16, 16, x, y, 8, 8, 1, 1, 0, flip, 0xFFFFFFFF);
    }
}

/* 4) Alternating between two textures → flush per switch */
static void d_multi_tex(int n)
{
    for (int i = 0; i < n; i++)
    {
        R2dTexture t = (i & 1) ? tex32 : tex16;
        float x = (i * 37) % W, y = (i * 53) % H;
        float s = (i & 1) ? 8.0f : 4.0f; /* different sizes */
        r2d_sprite(t, 0, 0, (i & 1) ? 32 : 16, (i & 1) ? 32 : 16,
                   x, y, s, s, 1, 1, 0, 0, 0xAAFFFFFF);
    }
}

/* 5) Clip changes every K sprites → flush per clip change */
static void d_clip(int n)
{
    int K = 50; /* change clip every 50 sprites */
    for (int i = 0; i < n; i++)
    {
        if (i % K == 0)
        {
            int cx = (i / K) * 16;
            r2d_clip(cx % W, 0, 128, 128);
        }
        float x = (i * 37) % W, y = (i * 53) % H;
        r2d_sprite(tex16, 0, 0, 16, 16, x, y, 8, 8, 1, 1, 0, 0, 0xFFFFFFFF);
    }
}

/* 6) Sprites interleaved with shapes (white-texture draws) → flush on
 *    texture switch between tex16 and g.white each time we draw a rect. */
static void d_mixed(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W, y = (i * 53) % H;
        r2d_sprite(tex16, 0, 0, 16, 16, x, y, 8, 8, 1, 1, 0, 0, 0xFFFFFFFF);
        /* Insert a shape that uses the white texture → forces flush of
         * sprite batch, then another flush when returning to tex16. */
        r2d_rect(x + 20, y, 6, 6, 0xFFFF0000);
    }
}

/* 7) Full-screen particle-like stress: tiny sprites at scale 0.5 */
static void d_fullscreen(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W, y = (i * 53) % H;
        r2d_sprite(tex16, 0, 0, 16, 16, x, y, 4, 4, 0.5f, 0.5f,
                   (float)(i % 360), 0,
                   ((i & 3) ? 0xFF88CCFF : 0xFF44AAFF));
    }
}

/* ------------------------------------------------------------------ */
int main(void)
{
    platform_init();
    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width   = 1280;
    cfg.height  = 720;
    cfg.render  = RENDER_GL;
    cfg.gl.major = 3;
    cfg.gl.minor = 3;
    cfg.vsync   = false;
    w = window_create(&cfg);
    window_make_current(w);
    if (!r2d_init())
    {
        printf("r2d_init failed\n");
        return 1;
    }
    window_get_framebuffer_size(w, &W, &H);

    /* ---- build textures ---- */
    uint32_t px16[16 * 16];
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            int c = ((x / 4) + (y / 4)) & 1;
            px16[y * 16 + x] = c ? 0xFFEC6A5A : 0xFFFFD166;
        }
    tex16 = r2d_texture_create(px16, 16, 16);

    uint32_t px32[32 * 32];
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
        {
            int v = (x + y) % 32;
            px32[y * 32 + x] = 0xFF000000 | (v << 3) | ((32 - v) << 11) | (128 << 16);
        }
    tex32 = r2d_texture_create(px32, 32, 32);

    printf("=== r2d Sprite Batcher Benchmark (%dx%d, vsync off, glFinish) ===\n", W, H);
    printf("%-29s %8s  %5s  %9s  %7s  %7s  %8s\n",
           "Scenario", "Count", "Draws", "Sprites/Dr", "ms", "FPS", "@60fps");

    int draws;

    /* --- core batch-efficiency scenarios --- */
    run("1) same tex, no rot",    100000, 60, d_same_tex,     &draws);
    run("2) same tex, rotation",  100000, 60, d_same_tex_rot, &draws);
    run("3) same tex, flip",      100000, 60, d_flip,         &draws);

    /* --- multi-texture → expected high draw count --- */
    run("4) alt 2 textures",       50000, 60, d_multi_tex,    &draws);

    /* --- clip stress --- */
    run("5) clip change / 50",     50000, 60, d_clip,         &draws);

    /* --- mixed with shapes --- */
    run("6) sprite+rect interlv",  10000, 60, d_mixed,        &draws);

    /* --- fullscreen stress --- */
    run("7) fullscreen particles",200000, 60, d_fullscreen,   &draws);

    /* --- summary guidance --- */
    printf("\n");
    printf("VBO cap = 65536 verts (max ~10900 sprites per draw call)\n");
    printf("Scenarios 1-3,7: draws = ceil(sprites/10900) — pure batch, no break\n");
    printf("Scenario 4: draws = sprites (each texture switch flushes)\n");
    printf("Scenario 5: draws = clip changes = ceil(sprites/50)\n");
    printf("Scenario 6: draws = 2 × sprites (tex16 ↔ white per pair)\n");

    r2d_shutdown();
    window_destroy(w);
    platform_shutdown();
    return 0;
}