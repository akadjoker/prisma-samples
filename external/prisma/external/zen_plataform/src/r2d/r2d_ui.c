/*
 * r2d_ui.c - the zen_ui GL renderer, built on r2d.
 *
 * Walks a UiDrawList into r2d primitives. Text uses a glyph atlas: each glyph's
 * CPU coverage (from the public zui_font_glyph) is packed once into an RGBA
 * texture (white RGB, coverage in alpha) and drawn as a tinted sub-quad, so a
 * whole text run batches under one texture.
 */
#include "zen_2d.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Minimal UTF-8 decode, so the renderer touches only the public zen_ui API and
   no toolkit internals. Returns bytes consumed; *cp is the codepoint. */
static int utf8_next(const char *s, int len, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    if (len <= 0)
    {
        *cp = 0;
        return 0;
    }
    if (u[0] < 0x80) { *cp = u[0]; return 1; }
    int n;
    uint32_t c;
    if ((u[0] & 0xE0) == 0xC0) { n = 2; c = u[0] & 0x1F; }
    else if ((u[0] & 0xF0) == 0xE0) { n = 3; c = u[0] & 0x0F; }
    else if ((u[0] & 0xF8) == 0xF0) { n = 4; c = u[0] & 0x07; }
    else { *cp = 0xFFFD; return 1; }
    if (n > len) { *cp = 0xFFFD; return 1; }
    for (int k = 1; k < n; ++k)
    {
        if ((u[k] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        c = (c << 6) | (u[k] & 0x3F);
    }
    *cp = c;
    return n;
}

#define ATLAS_DIM   1024
#define GLYPH_SLOTS 8192 /* power of two; open-addressed, never evicts */

typedef struct
{
    bool         used;
    const void  *font;
    uint32_t     cp;
    float        u0, v0, u1, v1;
    int          w, h, xoff, yoff, advance;
} GlyphSlot;

static struct
{
    bool       ready;
    R2dTexture atlas;
    int        shelf_x, shelf_y, shelf_h; /* simple shelf packer */
    GlyphSlot  slots[GLYPH_SLOTS];
} a;

static void atlas_init(void)
{
    if (a.ready)
        return;
    uint32_t *zero = (uint32_t *)calloc((size_t)ATLAS_DIM * ATLAS_DIM, sizeof(uint32_t));
    a.atlas = r2d_texture_create(zero, ATLAS_DIM, ATLAS_DIM);
    free(zero);
    a.shelf_x = a.shelf_y = a.shelf_h = 0;
    memset(a.slots, 0, sizeof(a.slots));
    a.ready = true;
}

static GlyphSlot *slot_find(const void *font, uint32_t cp)
{
    uintptr_t h = ((uintptr_t)font * 2654435761u) ^ ((uintptr_t)cp * 2246822519u);
    uint32_t i = (uint32_t)h & (GLYPH_SLOTS - 1);
    while (a.slots[i].used && !(a.slots[i].font == font && a.slots[i].cp == cp))
        i = (i + 1) & (GLYPH_SLOTS - 1);
    return &a.slots[i];
}

/* Pack a glyph into the atlas on first use. Returns NULL if the atlas is full. */
static GlyphSlot *glyph(const UiFont *font, uint32_t cp)
{
    GlyphSlot *s = slot_find(font, cp);
    if (s->used)
        return s;

    UiGlyphInfo gi;
    if (!zui_font_glyph(font, cp, &gi))
        return NULL;

    s->used = true;
    s->font = font;
    s->cp = cp;
    s->advance = gi.advance;
    s->xoff = gi.xoff;
    s->yoff = gi.yoff;
    s->w = gi.w;
    s->h = gi.h;
    if (!gi.coverage || gi.w <= 0 || gi.h <= 0)
        return s; /* blank glyph: advance only */

    if (a.shelf_x + gi.w > ATLAS_DIM) /* next shelf row */
    {
        a.shelf_x = 0;
        a.shelf_y += a.shelf_h + 1;
        a.shelf_h = 0;
    }
    if (a.shelf_y + gi.h > ATLAS_DIM)
    {
        s->w = s->h = 0; /* atlas full: render nothing, keep the advance */
        return s;
    }

    /* Expand coverage to RGBA (white, coverage in alpha) for the atlas. */
    uint32_t *rgba = (uint32_t *)malloc((size_t)gi.w * gi.h * sizeof(uint32_t));
    if (rgba)
    {
        for (int i = 0; i < gi.w * gi.h; ++i)
            rgba[i] = ((uint32_t)gi.coverage[i] << 24) | 0x00FFFFFFu;
        r2d_texture_subimage(a.atlas, a.shelf_x, a.shelf_y, gi.w, gi.h, rgba);
        free(rgba);
    }
    s->u0 = (float)a.shelf_x / ATLAS_DIM;
    s->v0 = (float)a.shelf_y / ATLAS_DIM;
    s->u1 = (float)(a.shelf_x + gi.w) / ATLAS_DIM;
    s->v1 = (float)(a.shelf_y + gi.h) / ATLAS_DIM;
    a.shelf_x += gi.w + 1;
    if (gi.h > a.shelf_h)
        a.shelf_h = gi.h;
    return s;
}

static void draw_text(const UiCmd *c)
{
    const UiFont *font = (const UiFont *)c->u.text.font;
    if (!font)
        return;
    int pen = c->u.text.x;
    int baseline = c->u.text.y + zui_font_ascent(font);
    int i = 0, len = c->u.text.len;
    while (i < len)
    {
        uint32_t cp;
        int adv = utf8_next(c->u.text.str + i, len - i, &cp);
        if (adv <= 0)
            break;
        i += adv;
        GlyphSlot *s = glyph(font, cp);
        if (!s)
            continue;
        if (s->w > 0 && s->h > 0)
            r2d_texture_draw_uv(a.atlas, s->u0, s->v0, s->u1, s->v1,
                                (float)(pen + s->xoff), (float)(baseline + s->yoff),
                                (float)s->w, (float)s->h, c->color);
        pen += s->advance;
    }
}

/* Procedural icons, mirroring ui_render_software.c's draw_icon so PIXELS and GL
   look the same. Built from r2d triangles / lines / rects. */
static void draw_icon_gl(UiRect box, int icon, uint32_t color)
{
    float cx = box.x + box.w / 2.0f, cy = box.y + box.h / 2.0f;
    int s = box.w < box.h ? box.w : box.h;
    int m = 2;
    float t = s > 16 ? 2.0f : 1.5f;

    switch ((UiIcon)icon)
    {
    case UI_ICON_ARROW_RIGHT:
        r2d_triangle((float)(box.x + m), (float)(box.y + m),
                     (float)(box.x + m), (float)(box.y + box.h - m - 1),
                     (float)(box.x + box.w - m - 1), cy, color);
        break;
    case UI_ICON_ARROW_DOWN:
        r2d_triangle((float)(box.x + m), (float)(box.y + m),
                     (float)(box.x + box.w - m - 1), (float)(box.y + m),
                     cx, (float)(box.y + box.h - m - 1), color);
        break;
    case UI_ICON_ARROW_LEFT:
        r2d_triangle((float)(box.x + box.w - m - 1), (float)(box.y + m),
                     (float)(box.x + box.w - m - 1), (float)(box.y + box.h - m - 1),
                     (float)(box.x + m), cy, color);
        break;
    case UI_ICON_CHECK:
        r2d_line((float)(box.x + m + 2), cy + s / 4.0f, cx - 2, (float)(box.y + box.h - m - 2), t, color);
        r2d_line(cx - 2, (float)(box.y + box.h - m - 2), cx + s / 3.0f, (float)(box.y + m + 2), t, color);
        break;
    case UI_ICON_CHEVRON:
    {
        float lx = cx - s / 4.0f, rx = cx + s / 4.0f;
        float ty = cy - s / 6.0f, by = cy + s / 6.0f;
        r2d_line(lx, by, cx, ty, t, color);
        r2d_line(cx, ty, rx, by, t, color);
        break;
    }
    case UI_ICON_CLOSE:
    {
        int inset = m + 2;
        r2d_line((float)(box.x + inset), (float)(box.y + inset),
                 (float)(box.x + box.w - inset - 1), (float)(box.y + box.h - inset - 1), t, color);
        r2d_line((float)(box.x + box.w - inset - 1), (float)(box.y + inset),
                 (float)(box.x + inset), (float)(box.y + box.h - inset - 1), t, color);
        break;
    }
    case UI_ICON_PLUS:
        r2d_line(cx, (float)(box.y + m + 1), cx, (float)(box.y + box.h - m - 1), t, color);
        r2d_line((float)(box.x + m + 1), cy, (float)(box.x + box.w - m - 1), cy, t, color);
        break;
    case UI_ICON_MINUS:
        r2d_line((float)(box.x + m + 3), cy, (float)(box.x + box.w - m - 3), cy, t, color);
        break;
    case UI_ICON_DOT:
        r2d_circle(cx, cy, s > 12 ? 3.0f : 2.0f, color);
        break;
    case UI_ICON_FILE:
        r2d_rect((float)(box.x + m), (float)(box.y + m),
                 (float)(box.w - 2 * m), (float)(box.h - 2 * m), color);
        break;
    case UI_ICON_FOLDER:
    {
        int h = box.h / 2;
        r2d_rect((float)(box.x + m), (float)(box.y + m + h / 2),
                 (float)(box.w - 2 * m), (float)(box.h - m - h / 2), color);
        break;
    }
    default:
        break;
    }
}

void zui_render_gl(const UiDrawList *list, int fb_w, int fb_h)
{
    atlas_init();
    r2d_begin(fb_w, fb_h);

    UiRect last_clip = {-1, -1, -1, -1};
    for (int i = 0; i < list->count; ++i)
    {
        const UiCmd *c = &list->cmds[i];
        if (c->clip.x != last_clip.x || c->clip.y != last_clip.y ||
            c->clip.w != last_clip.w || c->clip.h != last_clip.h)
        {
            r2d_clip(c->clip.x, c->clip.y, c->clip.w, c->clip.h);
            last_clip = c->clip;
        }

        switch (c->type)
        {
        case UI_CMD_RECT:
        {
            UiRect r = c->u.rect.rect;
            if (c->u.rect.thickness > 0)
                r2d_round_rect_line(r.x, r.y, r.w, r.h, c->u.rect.rounding,
                                    c->u.rect.thickness, c->color);
            else
                r2d_round_rect(r.x, r.y, r.w, r.h, c->u.rect.rounding, c->color);
            break;
        }
        case UI_CMD_LINE:
            r2d_line((float)c->u.line.x0, (float)c->u.line.y0,
                     (float)c->u.line.x1, (float)c->u.line.y1, 1.0f, c->color);
            break;
        case UI_CMD_TRI:
            r2d_triangle((float)c->u.tri.x0, (float)c->u.tri.y0,
                         (float)c->u.tri.x1, (float)c->u.tri.y1,
                         (float)c->u.tri.x2, (float)c->u.tri.y2, c->color);
            break;
        case UI_CMD_TEXT:
            draw_text(c);
            break;
        case UI_CMD_ICON:
            draw_icon_gl(c->u.icon.box, c->u.icon.icon, c->color);
            break;
        case UI_CMD_GRADIENT:
            r2d_rect_gradient_v((float)c->u.gradient.rect.x, (float)c->u.gradient.rect.y,
                                (float)c->u.gradient.rect.w, (float)c->u.gradient.rect.h,
                                c->u.gradient.top, c->u.gradient.bottom);
            break;
        case UI_CMD_IMAGE:
            if (c->u.image.src)
            {
                R2dTexture t = r2d_texture_from_framebuffer(c->u.image.src);
                UiRect d = c->u.image.dst;
                r2d_texture_draw(t, (float)d.x, (float)d.y, (float)d.w, (float)d.h,
                                 c->color ? c->color : 0xFFFFFFFFu);
                r2d_texture_free(&t);
            }
            break;
        default:
            break;
        }
    }
    r2d_end();
}
