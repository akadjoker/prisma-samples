/*
 * draw2d.c - software rasterizer over a Framebuffer. Single format (0xAARRGGBB),
 * integer math only (no libm), every primitive clips to the framebuffer. The API
 * is declared in platform.h.
 */
#include "platform.h"

#include <stdlib.h>
#include <string.h>

/* ---- optional scissor clip ------------------------------------------------ */

static struct { bool on; int x, y, w, h; } g_clip;

void draw_set_clip(int x, int y, int w, int h)
{
    g_clip.on = true;
    g_clip.x = x;
    g_clip.y = y;
    g_clip.w = w;
    g_clip.h = h;
}

void draw_reset_clip(void)
{
    g_clip.on = false;
}

static inline bool clip_reject(int x, int y)
{
    return g_clip.on &&
        (x < g_clip.x || y < g_clip.y || x >= g_clip.x + g_clip.w || y >= g_clip.y + g_clip.h);
}

/* ---- framebuffer ownership ---- */

bool framebuffer_alloc(Framebuffer *fb, int width, int height)
{
    if (!fb || width <= 0 || height <= 0)
        return false;
    fb->pixels = calloc((size_t)width * height, sizeof *fb->pixels);
    if (!fb->pixels)
        return false;
    fb->width = width;
    fb->height = height;
    fb->stride = width;
    return true;
}

bool framebuffer_from_pixels(Framebuffer *fb, const uint32_t *pixels, int width, int height)
{
    if (!pixels || !framebuffer_alloc(fb, width, height))
        return false;
    memcpy(fb->pixels, pixels, (size_t)width * height * sizeof *fb->pixels);
    return true;
}

void framebuffer_free(Framebuffer *fb)
{
    if (!fb)
        return;
    free(fb->pixels);
    fb->pixels = NULL;
    fb->width = fb->height = fb->stride = 0;
}

/* ---- color helpers ---- */

static inline uint32_t blend_over(uint32_t dst, uint32_t src)
{
    uint32_t sa = src >> 24;
    if (sa == 0xFF)
        return src | 0xFF000000u;
    if (sa == 0)
        return dst;
    uint32_t ia = 255 - sa;
    uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    uint32_t r = (sr * sa + dr * ia + 127) / 255;
    uint32_t g = (sg * sa + dg * ia + 127) / 255;
    uint32_t b = (sb * sa + db * ia + 127) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static long isqrt_l(long v)
{
    if (v <= 0)
        return 0;
    long x = v, y = (x + 1) / 2;
    while (y < x)
    {
        x = y;
        y = (x + v / x) / 2;
    }
    return x;
}

/* ---- pixel ---- */

void draw_pixel(Framebuffer *fb, int x, int y, uint32_t color, BlendMode blend)
{
    if (!fb || !fb->pixels || x < 0 || y < 0 || x >= fb->width || y >= fb->height)
        return;
    if (clip_reject(x, y))
        return;
    uint32_t *p = &fb->pixels[(size_t)y * fb->stride + x];
    *p = blend == BLEND_ALPHA ? blend_over(*p, color) : color;
}

uint32_t draw_get_pixel(const Framebuffer *fb, int x, int y)
{
    if (!fb || !fb->pixels || x < 0 || y < 0 || x >= fb->width || y >= fb->height)
        return 0;
    return fb->pixels[(size_t)y * fb->stride + x];
}

void draw_clear(Framebuffer *fb, uint32_t color)
{
    if (!fb || !fb->pixels)
        return;
    for (int y = 0; y < fb->height; y++)
    {
        uint32_t *row = fb->pixels + (size_t)y * fb->stride;
        for (int x = 0; x < fb->width; x++)
            row[x] = color;
    }
}

/* ---- lines, rects ---- */

void draw_line(Framebuffer *fb, int x0, int y0, int x1, int y1, uint32_t color, BlendMode blend)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    int err = (dx > dy ? dx : -dy) / 2;
    for (;;)
    {
        draw_pixel(fb, x0, y0, color, blend);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = err;
        if (e2 > -dx)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void draw_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color, BlendMode blend)
{
    if (!fb || !fb->pixels || w <= 0 || h <= 0)
        return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > fb->width ? fb->width : x + w;
    int y1 = y + h > fb->height ? fb->height : y + h;
    if (g_clip.on) /* honour the scissor like the per-pixel primitives do */
    {
        if (x0 < g_clip.x) x0 = g_clip.x;
        if (y0 < g_clip.y) y0 = g_clip.y;
        if (x1 > g_clip.x + g_clip.w) x1 = g_clip.x + g_clip.w;
        if (y1 > g_clip.y + g_clip.h) y1 = g_clip.y + g_clip.h;
    }
    for (int yy = y0; yy < y1; yy++)
    {
        uint32_t *row = fb->pixels + (size_t)yy * fb->stride;
        for (int xx = x0; xx < x1; xx++)
            row[xx] = blend == BLEND_ALPHA ? blend_over(row[xx], color) : color;
    }
}

void draw_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t color, BlendMode blend)
{
    if (w <= 0 || h <= 0)
        return;
    draw_line(fb, x, y, x + w - 1, y, color, blend);
    draw_line(fb, x, y + h - 1, x + w - 1, y + h - 1, color, blend);
    draw_line(fb, x, y, x, y + h - 1, color, blend);
    draw_line(fb, x + w - 1, y, x + w - 1, y + h - 1, color, blend);
}

/* ---- rounded rects ---- */

static int round_radius(int w, int h, int r)
{
    int m = (w < h ? w : h) / 2;
    if (r > m)
        r = m;
    return r;
}

void draw_fill_round_rect(Framebuffer *fb, int x, int y, int w, int h, int radius, uint32_t color, BlendMode blend)
{
    if (!fb || !fb->pixels || w <= 0 || h <= 0)
        return;
    int r = round_radius(w, h, radius);
    if (r <= 0)
    {
        draw_fill_rect(fb, x, y, w, h, color, blend);
        return;
    }
    /* Middle band spans the full width; the two end bands inset each row by the
       corner arc. vd is the vertical distance from the corner centre. */
    draw_fill_rect(fb, x, y + r, w, h - 2 * r, color, blend);
    for (int dy = 0; dy < r; dy++)
    {
        int vd = r - dy;
        int chord = (int)isqrt_l((long)r * r - (long)vd * vd);
        int inset = r - chord;
        draw_fill_rect(fb, x + inset, y + dy, w - 2 * inset, 1, color, blend);
        draw_fill_rect(fb, x + inset, y + h - 1 - dy, w - 2 * inset, 1, color, blend);
    }
}

void draw_round_rect(Framebuffer *fb, int x, int y, int w, int h, int radius, uint32_t color, BlendMode blend)
{
    if (w <= 0 || h <= 0)
        return;
    int r = round_radius(w, h, radius);
    if (r <= 0)
    {
        draw_rect(fb, x, y, w, h, color, blend);
        return;
    }
    /* Straight edges between the corner arcs. */
    draw_line(fb, x + r, y, x + w - 1 - r, y, color, blend);
    draw_line(fb, x + r, y + h - 1, x + w - 1 - r, y + h - 1, color, blend);
    draw_line(fb, x, y + r, x, y + h - 1 - r, color, blend);
    draw_line(fb, x + w - 1, y + r, x + w - 1, y + h - 1 - r, color, blend);
    /* Four quarter arcs; plotting both (i,j) and (j,i) leaves no gaps. */
    int cxl = x + r, cxr = x + w - 1 - r, cyt = y + r, cyb = y + h - 1 - r;
    for (int i = 0; i <= r; i++)
    {
        int j = (int)isqrt_l((long)r * r - (long)i * i);
        draw_pixel(fb, cxl - i, cyt - j, color, blend);
        draw_pixel(fb, cxl - j, cyt - i, color, blend);
        draw_pixel(fb, cxr + i, cyt - j, color, blend);
        draw_pixel(fb, cxr + j, cyt - i, color, blend);
        draw_pixel(fb, cxl - i, cyb + j, color, blend);
        draw_pixel(fb, cxl - j, cyb + i, color, blend);
        draw_pixel(fb, cxr + i, cyb + j, color, blend);
        draw_pixel(fb, cxr + j, cyb + i, color, blend);
    }
}

/* ---- circles ---- */

void draw_circle(Framebuffer *fb, int cx, int cy, int radius, uint32_t color, BlendMode blend)
{
    if (radius < 0)
        return;
    int x = radius, y = 0, err = 0;
    while (x >= y)
    {
        draw_pixel(fb, cx + x, cy + y, color, blend);
        draw_pixel(fb, cx + y, cy + x, color, blend);
        draw_pixel(fb, cx - y, cy + x, color, blend);
        draw_pixel(fb, cx - x, cy + y, color, blend);
        draw_pixel(fb, cx - x, cy - y, color, blend);
        draw_pixel(fb, cx - y, cy - x, color, blend);
        draw_pixel(fb, cx + y, cy - x, color, blend);
        draw_pixel(fb, cx + x, cy - y, color, blend);
        y++;
        if (err <= 0)
            err += 2 * y + 1;
        if (err > 0)
        {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void draw_fill_circle(Framebuffer *fb, int cx, int cy, int radius, uint32_t color, BlendMode blend)
{
    if (radius < 0)
        return;
    for (int dy = -radius; dy <= radius; dy++)
    {
        int dx = (int)isqrt_l((long)radius * radius - (long)dy * dy);
        draw_fill_rect(fb, cx - dx, cy + dy, 2 * dx + 1, 1, color, blend);
    }
}

/* ---- triangle (edge function over the bounding box, either winding) ---- */

static long edge(int ax, int ay, int bx, int by, int px, int py)
{
    return (long)(bx - ax) * (py - ay) - (long)(by - ay) * (px - ax);
}

void draw_fill_triangle(Framebuffer *fb, int x0, int y0, int x1, int y1, int x2, int y2,
                        uint32_t color, BlendMode blend)
{
    if (!fb || !fb->pixels)
        return;
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    if (minx < 0)
        minx = 0;
    if (miny < 0)
        miny = 0;
    if (maxx >= fb->width)
        maxx = fb->width - 1;
    if (maxy >= fb->height)
        maxy = fb->height - 1;

    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++)
        {
            long w0 = edge(x1, y1, x2, y2, x, y);
            long w1 = edge(x2, y2, x0, y0, x, y);
            long w2 = edge(x0, y0, x1, y1, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                draw_pixel(fb, x, y, color, blend);
        }
}

/* ---- blit ---- */

static inline uint32_t src_at(const Framebuffer *src, int x, int y)
{
    return src->pixels[(size_t)y * src->stride + x];
}

static uint32_t sample_bilinear(const Framebuffer *src, int fx, int fy)
{
    /* fx, fy are 16.16 fixed-point source coordinates */
    int x0 = fx >> 16, y0 = fy >> 16;
    int tx = (fx >> 8) & 0xFF, ty = (fy >> 8) & 0xFF;
    int x1 = x0 + 1 < src->width ? x0 + 1 : x0;
    int y1 = y0 + 1 < src->height ? y0 + 1 : y0;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;

    uint32_t p00 = src_at(src, x0, y0), p10 = src_at(src, x1, y0);
    uint32_t p01 = src_at(src, x0, y1), p11 = src_at(src, x1, y1);

    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8)
    {
        int c00 = (p00 >> s) & 0xFF, c10 = (p10 >> s) & 0xFF;
        int c01 = (p01 >> s) & 0xFF, c11 = (p11 >> s) & 0xFF;
        int top = c00 + (((c10 - c00) * tx) >> 8);
        int bot = c01 + (((c11 - c01) * tx) >> 8);
        int v = top + (((bot - top) * ty) >> 8);
        out |= (uint32_t)(v & 0xFF) << s;
    }
    return out;
}

void draw_blit(Framebuffer *dst, const Framebuffer *src,
               int sx, int sy, int sw, int sh,
               int dx, int dy, int dw, int dh,
               BlendMode blend, ScaleMode scale)
{
    if (!dst || !dst->pixels || !src || !src->pixels || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;

    int stepx = (int)(((long)sw << 16) / dw);
    int stepy = (int)(((long)sh << 16) / dh);

    for (int j = 0; j < dh; j++)
    {
        int oy = dy + j;
        if (oy < 0 || oy >= dst->height)
            continue;
        int fy = (sy << 16) + j * stepy + (scale == SCALE_BILINEAR ? stepy / 2 - 32768 : 0);

        for (int i = 0; i < dw; i++)
        {
            int ox = dx + i;
            if (ox < 0 || ox >= dst->width)
                continue;
            int fx = (sx << 16) + i * stepx + (scale == SCALE_BILINEAR ? stepx / 2 - 32768 : 0);

            uint32_t c;
            if (scale == SCALE_BILINEAR)
            {
                c = sample_bilinear(src, fx, fy);
            }
            else
            {
                int srcx = fx >> 16, srcy = fy >> 16;
                if (srcx < 0)
                    srcx = 0;
                if (srcy < 0)
                    srcy = 0;
                if (srcx >= src->width)
                    srcx = src->width - 1;
                if (srcy >= src->height)
                    srcy = src->height - 1;
                c = src_at(src, srcx, srcy);
            }
            draw_pixel(dst, ox, oy, c, blend);
        }
    }
}
