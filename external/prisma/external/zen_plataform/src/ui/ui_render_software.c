/*
 * ui_render_software.c - paint a UiDrawList into a CPU Framebuffer via draw2d.
 *
 * This is the RENDER_PIXELS path. The command list is already layer-sorted, so a
 * straight walk is correct. Most commands map one-to-one onto draw2d primitives.
 *
 * SCAFFOLD: rect/line/tri/image are wired; clipping and text await a font unit.
 */
#include "ui_internal.h"
#include "ui_font.h"
#include "ui_utf8.h"

/* ----------------------------------------------------------------- icons --- */

static void draw_icon(Framebuffer *fb, UiRect box, int icon, UiColor color)
{
    int cx = box.x + box.w / 2, cy = box.y + box.h / 2;
    int s = box.w < box.h ? box.w : box.h;
    int m = 2; /* margin */

    switch ((UiIcon)icon)
    {
    case UI_ICON_ARROW_RIGHT:
    {
        int ax = box.x + m, ay = box.y + m;
        int bx = box.x + m, by = box.y + box.h - m - 1;
        int px = box.x + box.w - m - 1, py = cy;
        draw_fill_triangle(fb, ax, ay, bx, by, px, py, color, BLEND_ALPHA);
        break;
    }
    case UI_ICON_ARROW_DOWN:
    {
        int ax = box.x + m, ay = box.y + m;
        int bx = box.x + box.w - m - 1, by = box.y + m;
        int px = cx, py = box.y + box.h - m - 1;
        draw_fill_triangle(fb, ax, ay, bx, by, px, py, color, BLEND_ALPHA);
        break;
    }
    case UI_ICON_ARROW_LEFT:
    {
        int ax = box.x + box.w - m - 1, ay = box.y + m;
        int bx = box.x + box.w - m - 1, by = box.y + box.h - m - 1;
        int px = box.x + m, py = cy;
        draw_fill_triangle(fb, ax, ay, bx, by, px, py, color, BLEND_ALPHA);
        break;
    }
    case UI_ICON_CHECK:
    {
        int thick = s > 16 ? 2 : 1;
        int x0 = box.x + m + 2, y0 = cy + s / 4;
        int x1 = cx - 2, y1 = box.y + box.h - m - 2;
        int x2 = cx + s / 3, y2 = box.y + m + 2;
        for (int t = 0; t < thick; t++)
        {
            draw_line(fb, x0 + t, y0, x1 + t, y1, color, BLEND_ALPHA);
            draw_line(fb, x1 + t, y1, x2 + t, y2, color, BLEND_ALPHA);
        }
        break;
    }
    case UI_ICON_CHEVRON:
    {
        int mid = s > 16 ? 2 : 1;
        int lx = cx - s / 4, rx = cx + s / 4;
        int ty = cy - s / 6, by = cy + s / 6;
        for (int t = 0; t < mid; t++)
        {
            draw_line(fb, lx + t, by, cx + t, ty, color, BLEND_ALPHA);
            draw_line(fb, cx + t, ty, rx + t, by, color, BLEND_ALPHA);
        }
        break;
    }
    case UI_ICON_CLOSE:
    {
        int thick = s > 16 ? 2 : 1;
        int inset = m + 2;
        for (int t = 0; t < thick; t++)
        {
            draw_line(fb, box.x + inset + t, box.y + inset, box.x + box.w - inset - 1 + t, box.y + box.h - inset - 1, color, BLEND_ALPHA);
            draw_line(fb, box.x + box.w - inset - 1 + t, box.y + inset, box.x + inset + t, box.y + box.h - inset - 1, color, BLEND_ALPHA);
        }
        break;
    }
    case UI_ICON_PLUS:
    {
        int thick = s > 16 ? 2 : 1;
        int mid = s / 2;
        for (int t = 0; t < thick; t++)
        {
            draw_line(fb, cx + t - mid / 2, box.y + m + mid / 2, cx + t + mid / 2, box.y + m + mid / 2, color, BLEND_ALPHA);
            draw_line(fb, cx + t - mid / 2, box.y + box.h - m - mid / 2, cx + t + mid / 2, box.y + box.h - m - mid / 2, color, BLEND_ALPHA);
        }
        /* vertical */
        for (int t = 0; t < thick; t++)
        {
            draw_line(fb, box.x + m + s / 4 + t, box.y + m + 1, box.x + m + s / 4 + t, box.y + box.h - m - 1, color, BLEND_ALPHA);
            draw_line(fb, box.x + box.w - m - s / 4 - 1 + t, box.y + m + 1, box.x + box.w - m - s / 4 - 1 + t, box.y + box.h - m - 1, color, BLEND_ALPHA);
        }
        break;
    }
    case UI_ICON_MINUS:
    {
        int thick = s > 16 ? 2 : 1;
        for (int t = 0; t < thick; t++)
            draw_line(fb, box.x + m + 3 + t, cy, box.x + box.w - m - 3 + t, cy, color, BLEND_ALPHA);
        break;
    }
    case UI_ICON_FOLDER:
    {
        int h = box.h / 2;
        draw_fill_rect(fb, box.x + m, box.y + m + h / 2, h, h, color, BLEND_ALPHA);
        draw_fill_rect(fb, box.x + m, box.y + m + h / 2, box.w - 2 * m, box.h - m - h / 2, color, BLEND_ALPHA);
        break;
    }
    case UI_ICON_FILE:
    {
        draw_fill_rect(fb, box.x + m, box.y + m, box.w - 2 * m, box.h - 2 * m, color, BLEND_ALPHA);
        draw_line(fb, box.x + box.w - m - s / 3, box.y + m, box.x + box.w - m - 1, box.y + m + s / 3, color, BLEND_ALPHA);
        break;
    }
    case UI_ICON_DOT:
    {
        int r = s > 12 ? 3 : 2;
        draw_fill_circle(fb, cx, cy, r, color, BLEND_ALPHA);
        break;
    }
    default:
        break;
    }
}

/* Vertical two-color gradient: top-to-bottom linear interpolation per row. */
static void draw_gradient(Framebuffer *fb, UiRect r, UiColor top, UiColor bottom)
{
    if (r.h <= 1)
    {
        draw_fill_rect(fb, r.x, r.y, r.w, r.h, top, BLEND_ALPHA);
        return;
    }
    int tr = (top >> 16) & 0xFF, tg = (top >> 8) & 0xFF, tb = top & 0xFF, ta = (top >> 24) & 0xFF;
    int br = (bottom >> 16) & 0xFF, bg = (bottom >> 8) & 0xFF, bb = bottom & 0xFF, ba = (bottom >> 24) & 0xFF;
    for (int y = 0; y < r.h; y++)
    {
        int t = y * 256 / (r.h - 1);
        int rr = tr + ((br - tr) * t >> 8);
        int gg = tg + ((bg - tg) * t >> 8);
        int bb2 = tb + ((bb - tb) * t >> 8);
        int aa = ta + ((ba - ta) * t >> 8);
        uint32_t c = ((uint32_t)aa << 24) | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb2;
        draw_fill_rect(fb, r.x, r.y + y, r.w, 1, c, BLEND_ALPHA);
    }
}

/* Tint a glyph's 8-bit coverage by color and source-over it onto the fb,
   clipped to `clip` (the scissor in force when the text command was emitted). */
static void blit_glyph(Framebuffer *fb, const UiGlyph *g, int px, int py, UiColor color, UiRect clip)
{
    if (!g->cov)
        return;
    int cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF, cb = color & 0xFF;
    int ca = (color >> 24) & 0xFF;
    for (int y = 0; y < g->h; ++y)
    {
        int dy = py + y;
        if (dy < 0 || dy >= fb->height)
            continue;
        if (dy < clip.y || dy >= clip.y + clip.h)
            continue;
        for (int x = 0; x < g->w; ++x)
        {
            int dx = px + x;
            if (dx < 0 || dx >= fb->width)
                continue;
            if (dx < clip.x || dx >= clip.x + clip.w)
                continue;
            int a = g->cov[y * g->w + x] * ca / 255;
            if (!a)
                continue;
            uint32_t d = fb->pixels[dy * fb->stride + dx];
            int dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
            dr += (cr - dr) * a / 255;
            dg += (cg - dg) * a / 255;
            db += (cb - db) * a / 255;
            fb->pixels[dy * fb->stride + dx] =
                0xFF000000u | ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db;
        }
    }
}

static void draw_text_run(Framebuffer *fb, const UiCmd *c)
{
    UiFont *font = (UiFont *)c->u.text.font;
    if (!font)
        return;
    int pen = c->u.text.x;
    int baseline = c->u.text.y + ui_font_ascent(font);
    int i = 0, len = c->u.text.len;
    while (i < len)
    {
        uint32_t cp;
        int adv = ui_utf8_decode(c->u.text.str + i, len - i, &cp);
        if (adv <= 0)
            break;
        i += adv;
        const UiGlyph *g = ui_font_glyph(font, cp);
        blit_glyph(fb, g, pen + g->xoff, baseline + g->yoff, c->color, c->clip);
        pen += g->advance;
    }
}

void zui_render_software(Framebuffer *fb, const UiDrawList *list)
{
    for (int i = 0; i < list->count; ++i)
    {
        const UiCmd *c = &list->cmds[i];
        /* Each command carries the clip rect that was in force when it was
           emitted. Set the scissor for this command's primitive set. */
        draw_set_clip(c->clip.x, c->clip.y, c->clip.w, c->clip.h);

        switch (c->type)
        {
        case UI_CMD_CLIP:
            /* Handled above per-command; UI_CMD_CLIP is a no-op at render time. */
            break;
        case UI_CMD_RECT:
        {
            UiRect r = c->u.rect.rect;
            int rad = c->u.rect.rounding;
            if (c->u.rect.thickness > 0)
                draw_round_rect(fb, r.x, r.y, r.w, r.h, rad, c->color, BLEND_ALPHA);
            else
                draw_fill_round_rect(fb, r.x, r.y, r.w, r.h, rad, c->color, BLEND_ALPHA);
            break;
        }
        case UI_CMD_LINE:
            draw_line(fb, c->u.line.x0, c->u.line.y0, c->u.line.x1, c->u.line.y1,
                      c->color, BLEND_ALPHA);
            break;
        case UI_CMD_TRI:
            draw_fill_triangle(fb, c->u.tri.x0, c->u.tri.y0, c->u.tri.x1, c->u.tri.y1,
                               c->u.tri.x2, c->u.tri.y2, c->color, BLEND_ALPHA);
            break;
        case UI_CMD_IMAGE:
            if (c->u.image.src)
                draw_blit(fb, c->u.image.src, 0, 0, c->u.image.src->width,
                          c->u.image.src->height, c->u.image.dst.x, c->u.image.dst.y,
                          c->u.image.dst.w, c->u.image.dst.h, BLEND_ALPHA, SCALE_BILINEAR);
            break;
        case UI_CMD_TEXT:
            draw_text_run(fb, c);
            break;
        case UI_CMD_ICON:
            draw_icon(fb, c->u.icon.box, c->u.icon.icon, c->color);
            break;
        case UI_CMD_GRADIENT:
            draw_gradient(fb, c->u.gradient.rect, c->u.gradient.top, c->u.gradient.bottom);
            break;
        }
    }
    draw_reset_clip();
}
