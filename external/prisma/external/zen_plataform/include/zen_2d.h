/*
 * zen_2d.h - r2d, a small batched 2D engine on OpenGL.
 *
 * Two roles, one code path:
 *   1. A public 2D API for apps: quads, lines, textured sprites, a scissor.
 *   2. The GL renderer for zen_ui (zui_render_gl), so the toolkit core stays
 *      free of OpenGL. This library links GL; libzen_ui never does.
 *
 * One dynamic vertex buffer, one shader, batches flushed on texture, clip or
 * buffer-full. Requires a current GL context (a RENDER_GL window).
 */
#ifndef ZEN_2D_H
#define ZEN_2D_H

#include "platform.h"
#include "zen_ui.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Bring up shaders, the vertex buffer and the default white texture. Loads
       GL through gl_proc_address. Call once after window_make_current. */
    bool r2d_init(void);
    void r2d_shutdown(void);

    /* Per-frame bracket. r2d_begin sets the pixel-space projection for a
       framebuffer of fb_w x fb_h and resets the batch; r2d_end flushes. */
    void r2d_begin(int fb_w, int fb_h);
    void r2d_end(void);
    void r2d_flush(void);        /* force a draw call now (e.g. before raw GL) */
    unsigned r2d_draw_calls(void); /* draw calls issued this frame, for profiling */

    /* Scissor in framebuffer pixels (origin top-left). r2d_clip_none disables. */
    void r2d_clip(int x, int y, int w, int h);
    void r2d_clip_none(void);

    /* Shapes a fill function plus a `_line` outline where it makes
       sense. Color is 0xAARRGGBB; thickness and radius are pixels. */
    void r2d_rect(float x, float y, float w, float h, uint32_t color);
    void r2d_rect_line(float x, float y, float w, float h, float thickness, uint32_t color);
    void r2d_round_rect(float x, float y, float w, float h, float radius, uint32_t color);
    void r2d_round_rect_line(float x, float y, float w, float h, float radius, float thickness, uint32_t color);
    void r2d_circle(float cx, float cy, float r, uint32_t color);
    void r2d_circle_line(float cx, float cy, float r, float thickness, uint32_t color);
    void r2d_ellipse(float cx, float cy, float rx, float ry, uint32_t color);
    void r2d_triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color);
    /* Convex polygon fill from interleaved x,y pairs (count points). */
    void r2d_polygon(const float *xy, int count, uint32_t color);
    void r2d_line(float x0, float y0, float x1, float y1, float thickness, uint32_t color);
    /* Vertical two-color gradient rect (top to bottom): XP title bars, knobs. */
    void r2d_rect_gradient_v(float x, float y, float w, float h, uint32_t top, uint32_t bottom);

    /* Textures (RGBA 0xAARRGGBB in memory, same as Framebuffer). */
    typedef struct
    {
        unsigned id;
        int      w, h;
    } R2dTexture;

    R2dTexture r2d_texture_create(const uint32_t *pixels, int w, int h);
    R2dTexture r2d_texture_from_framebuffer(const Framebuffer *fb);
    void       r2d_texture_update(R2dTexture tex, const uint32_t *pixels, int w, int h);
    /* Upload into a sub-region; safe to call mid-frame (flushes the batch). */
    void       r2d_texture_subimage(R2dTexture tex, int x, int y, int w, int h, const uint32_t *pixels);
    void       r2d_texture_free(R2dTexture *tex);

    /* Draw a whole texture, or a sub-region in 0..1 UV, tinted by color. */
    void r2d_texture_draw(R2dTexture tex, float x, float y, float w, float h, uint32_t tint);
    void r2d_texture_draw_uv(R2dTexture tex, float u0, float v0, float u1, float v1,
                             float x, float y, float w, float h, uint32_t tint);

    /* Accelerated sprite blit, sf::Sprite-style: draw a source sub-rectangle in
       texture pixels (sw/sh <= 0 means the whole texture), placed so its origin
       (ox, oy in source pixels) lands at (dx, dy), scaled, rotated rot_deg about
       that origin, and optionally flipped. */
    enum { R2D_FLIP_NONE = 0, R2D_FLIP_X = 1, R2D_FLIP_Y = 2 };
    void r2d_sprite(R2dTexture tex,
                    float sx, float sy, float sw, float sh,
                    float dx, float dy, float ox, float oy,
                    float scale_x, float scale_y, float rot_deg, int flip,
                    uint32_t tint);

    /* The zen_ui GL renderer: walk a UiDrawList into batched geometry. Lives here
       so zen_ui.h declares no GL symbols. */
    void zui_render_gl(const UiDrawList *list, int fb_w, int fb_h);

#ifdef __cplusplus
}
#endif

#endif /* ZEN_2D_H */
