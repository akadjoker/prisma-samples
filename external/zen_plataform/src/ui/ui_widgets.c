/*
 * ui_widgets.c - immediate-mode widgets over the layout + theme.
 *
 * Widgets take no coordinates: each pulls its cell from ui_layout_next() and
 * paints with the current UiStyle, pushing commands through ui_push_*.
 */
#include "ui_internal.h"
#include "ui_font.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define COL(ui, id) ((ui)->style.colors[id])

/* ------------------------------------------------------------------ icons -- */

void zui_icon(UiContext *ui, UiRect box, UiIcon icon, UiColor color)
{
    ui_push_icon(ui, box, icon, color);
}

/* ----------------------------------------------------- interaction helpers -- */

int ui_next_id(UiContext *ui)
{
    return ++ui->next_id;
}

bool ui_mouse_in(const UiContext *ui, UiRect r)
{
    return ui->in.mouse_x >= r.x && ui->in.mouse_y >= r.y &&
           ui->in.mouse_x < r.x + r.w && ui->in.mouse_y < r.y + r.h;
}

/* True when the mouse is over r and not swallowed by a higher popup. */
bool ui_hovered(UiContext *ui, UiRect r)
{
    if (!ui_mouse_in(ui, r))
        return false;
    if (ui->overlay_active && ui->layer < ui->overlay_layer &&
        ui_mouse_in(ui, ui->overlay_rect))
        return false; /* the click belongs to the popup floating above */
    return true;
}

int16_t ui_popup_open(UiContext *ui, UiRect rect)
{
    int16_t layer = ui->popup_layer++;
    /* Keep the largest popup as the blocker; nested popups extend the layer. */
    ui->overlay_active = true;
    ui->overlay_rect = rect;
    ui->overlay_layer = layer;
    return layer;
}

bool ui_hit(UiContext *ui, int id, UiRect r)
{
    bool hovered = ui_hovered(ui, r);
    if (hovered)
        ui->hot = id;
    if (hovered && ui->in.mouse_pressed)
        ui->active = id;
    return hovered;
}

/* Draw a UTF-8 run, vertically centered in a cell of height h at (x, cell_y). */
static void text_in_cell(UiContext *ui, int x, int cell_y, int h, const char *s, UiColor color)
{
    int top = ui_text_baseline_y(cell_y, h, ui_font_ascent(ui->font));
    ui_push_text(ui, x, top, s, color);
}

/* Visible portion of a label, honouring the "##id" / "###id" id conventions
   (everything from "##" on is identity, not shown). */
static const char *label_text(const char *s, char *tmp, int cap)
{
    const char *h = strstr(s, "##");
    int n = h ? (int)(h - s) : (int)strlen(s);
    if (n > cap - 1) n = cap - 1;
    memcpy(tmp, s, (size_t)n);
    tmp[n] = '\0';
    return tmp;
}

/* Like text_in_cell but strips the "##id" suffix first. */
static void label_in_cell(UiContext *ui, int x, int cell_y, int h, const char *s, UiColor color)
{
    char tmp[128];
    text_in_cell(ui, x, cell_y, h, label_text(s, tmp, sizeof tmp), color);
}

/* ------------------------------------------------------------------ widgets -- */

void zui_label(UiContext *ui, const char *text)
{
    UiRect r = ui_layout_next(ui);
    label_in_cell(ui, r.x, r.y, r.h, text, COL(ui, UI_COL_TEXT));
}

bool zui_button(UiContext *ui, const char *text)
{
    UiRect r = ui_layout_next(ui);
    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);
    bool active = (ui->active == id);

    UiColor bg = COL(ui, UI_COL_BUTTON);
    if (active && ui->in.mouse_down)
        bg = COL(ui, UI_COL_BUTTON_ACTIVE);
    else if (hovered)
        bg = COL(ui, UI_COL_BUTTON_HOVER);

    ui_push_round_rect(ui, r, bg, 0, ui->style.rounding);
    if (ui->style.border)
        ui_push_round_rect(ui, r, COL(ui, UI_COL_BORDER), ui->style.border, ui->style.rounding);

    char tmp[128];
    const char *disp = label_text(text, tmp, sizeof tmp);
    int tw = zui_text_width(ui->font, disp, -1);
    text_in_cell(ui, r.x + (r.w - tw) / 2, r.y, r.h, disp, COL(ui, UI_COL_TEXT));

    if (ui->in.mouse_released && active)
    {
        ui->active = 0;
        return hovered;
    }
    return false;
}

bool zui_checkbox(UiContext *ui, const char *text, bool *value)
{
    UiRect r = ui_layout_next(ui);
    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);

    int box = r.h - 6 < 14 ? r.h - 6 : 18;
    if (box < 10)
        box = 10;
    UiRect b = zui_rect(r.x, r.y + (r.h - box) / 2, box, box);

    ui_push_round_rect(ui, b, COL(ui, UI_COL_INPUT_BG), 0, ui->style.rounding / 2);
    ui_push_round_rect(ui, b, hovered ? COL(ui, UI_COL_ACCENT) : COL(ui, UI_COL_BORDER),
                       1, ui->style.rounding / 2);
    if (*value)
    {
        UiRect c = zui_rect(b.x + 4, b.y + 4, box - 8, box - 8);
        ui_push_round_rect(ui, c, COL(ui, UI_COL_ACCENT), 0, ui->style.rounding / 3);
    }
    label_in_cell(ui, b.x + box + 8, r.y, r.h, text, COL(ui, UI_COL_TEXT));

    if (ui->in.mouse_released && ui->active == id)
    {
        ui->active = 0;
        if (hovered)
        {
            *value = !*value;
            return true;
        }
    }
    return false;
}

bool zui_slider(UiContext *ui, const char *label, float *value, float min, float max)
{
    (void)label;
    UiRect r = ui_layout_next(ui);
    int id = ui_next_id(ui);
    ui_hit(ui, id, r);

    int track_h = 6;
    int knob_w = 12;
    int ty = r.y + (r.h - track_h) / 2;
    int usable = r.w - knob_w;

    if (ui->active == id && ui->in.mouse_down && max > min)
    {
        float t = (float)(ui->in.mouse_x - r.x - knob_w / 2) / (float)(usable > 0 ? usable : 1);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        *value = min + (max - min) * t;
    }

    float t = max > min ? (*value - min) / (max - min) : 0.0f;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    int knob_x = r.x + (int)(t * (float)usable);

    ui_push_round_rect(ui, zui_rect(r.x, ty, r.w, track_h), COL(ui, UI_COL_TRACK), 0, track_h / 2);
    ui_push_round_rect(ui, zui_rect(r.x, ty, knob_x - r.x + knob_w / 2, track_h),
                       COL(ui, UI_COL_ACCENT), 0, track_h / 2);
    ui_push_round_rect(ui, zui_rect(knob_x, r.y + (r.h - 18) / 2, knob_w, 18),
                       COL(ui, UI_COL_KNOB), 0, ui->style.rounding);

    bool changed = (ui->active == id && ui->in.mouse_down);
    if (ui->in.mouse_released && ui->active == id)
        ui->active = 0;
    return changed;
}

bool zui_slider_v(UiContext *ui, const char *label, float *value, float min, float max)
{
    (void)label;
    UiRect r = ui_layout_next(ui);
    int id = ui_next_id(ui);
    ui_hit(ui, id, r);

    int track_w = 6;
    int knob_h = 12;
    int tx = r.x + (r.w - track_w) / 2;
    int usable = r.h - knob_h;

    if (ui->active == id && ui->in.mouse_down && max > min)
    {
        float t = (float)(ui->in.mouse_y - r.y - knob_h / 2) / (float)(usable > 0 ? usable : 1);
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        *value = max - t * (max - min); /* invert: top = max */
    }

    float t = max > min ? (max - *value) / (max - min) : 0.0f;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    int knob_y = r.y + (int)(t * (float)usable);

    ui_push_round_rect(ui, zui_rect(tx, r.y, track_w, r.h), COL(ui, UI_COL_TRACK), 0, track_w / 2);
    ui_push_round_rect(ui, zui_rect(tx, knob_y + knob_h / 2, track_w, r.y + r.h - knob_y - knob_h / 2),
                       COL(ui, UI_COL_ACCENT), 0, track_w / 2);
    ui_push_round_rect(ui, zui_rect(r.x + (r.w - 18) / 2, knob_y, 18, knob_h),
                       COL(ui, UI_COL_KNOB), 0, ui->style.rounding);

    bool changed = (ui->active == id && ui->in.mouse_down);
    if (ui->in.mouse_released && ui->active == id)
        ui->active = 0;
    return changed;
}

bool zui_knob(UiContext *ui, const char *label, float *value, float min, float max)
{
    UiRect r = ui_layout_next(ui);
    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);
    bool changed = false;

    int label_h = (label && label[0]) ? 16 : 0;
    int cx = r.x + r.w / 2;
    int cy = r.y + (r.h - label_h) / 2;
    int radius = (r.w < (r.h - label_h) ? r.w : (r.h - label_h)) / 2 - 4;
    if (radius < 8) radius = 8;

    /* rotary: a vertical drag changes the value. Drag a full knob height for the
       full range, so it feels the same regardless of the value span. */
    if (ui->active == id && ui->in.mouse_down && max > min)
    {
        float per_px = (max - min) / (float)(radius * 4);
        *value -= (float)ui->in.mouse_dy * per_px;
        if (*value < min) *value = min;
        if (*value > max) *value = max;
        changed = ui->in.mouse_dy != 0;
        zui_request_cursor(ui, CURSOR_RESIZE_NS);
    }
    else if (hovered)
        zui_request_cursor(ui, CURSOR_HAND);

    float frac = max > min ? (*value - min) / (max - min) : 0.0f;
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;

    /* body: filled circle (round-rect with radius = half) + ring */
    UiRect body = zui_rect(cx - radius, cy - radius, radius * 2, radius * 2);
    ui_push_round_rect(ui, body, COL(ui, UI_COL_KNOB), 0, radius);
    ui_push_round_rect(ui, body, hovered ? COL(ui, UI_COL_ACCENT) : COL(ui, UI_COL_BORDER),
                       2, radius);

    /* pointer line: 270-degree sweep, gap at the bottom (135 deg .. 405 deg). */
    {
        float a0 = 2.356194f;            /* 135 deg */
        float sweep = 4.712389f;         /* 270 deg */
        float ang = a0 + sweep * frac;
        float ca = (float)cos(ang), sa = (float)sin(ang);
        int ix = cx + (int)(ca * (radius - 3));
        int iy = cy + (int)(sa * (radius - 3));
        ui_push_line(ui, cx, cy, ix, iy, COL(ui, UI_COL_ACCENT));
    }

    if (label_h)
        text_in_cell(ui, r.x, r.y + r.h - label_h, r.w, label, COL(ui, UI_COL_TEXT_DIM));

    if (ui->in.mouse_released && ui->active == id)
        ui->active = 0;
    return changed;
}

bool zui_progress(UiContext *ui, float fraction)
{
    UiRect r = ui_layout_next(ui);
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;

    ui_push_round_rect(ui, r, COL(ui, UI_COL_TRACK), 0, ui->style.rounding);
    if (fraction > 0)
    {
        int fill = (int)((float)r.w * fraction);
        if (fill < 4) fill = 4;
        ui_push_round_rect(ui, zui_rect(r.x, r.y, fill, r.h), COL(ui, UI_COL_ACCENT), 0, ui->style.rounding);
    }
    if (ui->style.border)
        ui_push_round_rect(ui, r, COL(ui, UI_COL_BORDER), ui->style.border, ui->style.rounding);
    return false;
}

/* Length in bytes of the UTF-8 char that ends just before byte offset `at`. */
static int utf8_prev_len(const char *s, int at)
{
    int n = 1;
    while (at - n > 0 && ((uint8_t)s[at - n] & 0xC0) == 0x80)
        n++;
    return n;
}
/* Length in bytes of the UTF-8 char starting at byte offset `at`. */
static int utf8_at_len(const char *s, int at)
{
    uint8_t c = (uint8_t)s[at];
    if (c >= 0xF0) return 4;
    if (c >= 0xE0) return 3;
    if (c >= 0xC0) return 2;
    return 1;
}
/* Encode codepoint to UTF-8 in out (<=4 bytes); returns the byte count. */
static int utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800)
    {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

bool zui_text_input(UiContext *ui, const char *label, char *buf, int cap)
{
    (void)label;
    UiRect r = ui_layout_next(ui);
    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);
    int len = (int)strlen(buf);
    bool changed = false;

    /* Focus management: click to focus / place caret; click away unfocuses. */
    if (ui->in.mouse_pressed)
    {
        if (hovered)
        {
            ui->focus = id;
            /* caret to the nearest gap for the click x */
            int best = len, bestd = 1 << 30;
            for (int i = 0; i <= len;)
            {
                int gx = r.x + 8 + zui_text_width(ui->font, buf, i);
                int d = ui->in.mouse_x - gx; if (d < 0) d = -d;
                if (d < bestd) { bestd = d; best = i; }
                if (i == len) break;
                i += utf8_at_len(buf, i);
            }
            ui->caret = best;
        }
        else if (ui->focus == id)
            ui->focus = 0;
    }

    bool focused = (ui->focus == id);
    if (focused)
    {
        if (ui->caret > len) ui->caret = len;
        if (ui->caret < 0) ui->caret = 0;

        /* Editing keys (consume the whole queue this frame). */
        for (int k = 0; k < ui->in.key_count; ++k)
        {
            int key = ui->in.keys[k];
            if (key == KEY_BACKSPACE && ui->caret > 0)
            {
                int n = utf8_prev_len(buf, ui->caret);
                memmove(buf + ui->caret - n, buf + ui->caret, (size_t)(len - ui->caret + 1));
                ui->caret -= n; len -= n; changed = true;
            }
            else if (key == KEY_DELETE && ui->caret < len)
            {
                int n = utf8_at_len(buf, ui->caret);
                memmove(buf + ui->caret, buf + ui->caret + n, (size_t)(len - ui->caret - n + 1));
                len -= n; changed = true;
            }
            else if (key == KEY_LEFT && ui->caret > 0)
                ui->caret -= utf8_prev_len(buf, ui->caret);
            else if (key == KEY_RIGHT && ui->caret < len)
                ui->caret += utf8_at_len(buf, ui->caret);
            else if (key == KEY_HOME)
                ui->caret = 0;
            else if (key == KEY_END)
                ui->caret = len;
            else if (key == KEY_ENTER || key == KEY_TAB)
                ui->focus = 0;
        }

        /* Printable characters. */
        for (int c = 0; c < ui->in.char_count; ++c)
        {
            uint32_t cp = ui->in.chars[c];
            if (cp < 32 || cp == 127)
                continue;
            char enc[4];
            int n = utf8_encode(cp, enc);
            if (len + n >= cap)
                continue;
            memmove(buf + ui->caret + n, buf + ui->caret, (size_t)(len - ui->caret + 1));
            memcpy(buf + ui->caret, enc, (size_t)n);
            ui->caret += n; len += n; changed = true;
        }
        focused = (ui->focus == id); /* Enter may have dropped focus */
    }

    UiColor border = focused ? COL(ui, UI_COL_ACCENT)
                             : (hovered ? COL(ui, UI_COL_ACCENT) : COL(ui, UI_COL_BORDER));
    ui_push_round_rect(ui, r, COL(ui, UI_COL_INPUT_BG), 0, ui->style.rounding);
    ui_push_round_rect(ui, r, border, focused ? 2 : 1, ui->style.rounding);
    if (buf[0])
        text_in_cell(ui, r.x + 8, r.y, r.h, buf, COL(ui, UI_COL_TEXT));

    if (focused)
    {
        zui_request_cursor(ui, CURSOR_IBEAM);
        int cx = r.x + 8 + zui_text_width(ui->font, buf, ui->caret);
        int ch = ui->font ? zui_font_height(ui->font) : 14;
        int cy = r.y + (r.h - ch) / 2;

        /* An input method composing text: show it at the caret, underlined, and keep its
           candidate list beside the caret. */
        const char *comp = ui->win ? window_text_composition(ui->win) : "";
        int comp_w = 0;
        if (ui->win && (cx != ui->ime_x || cy != ui->ime_y))
        {
            ui->ime_x = cx;
            ui->ime_y = cy;
            window_set_text_input_rect(ui->win, cx, cy, 1, ch);
        }
        if (comp[0])
        {
            comp_w = zui_text_width(ui->font, comp, -1);
            ui_push_rect(ui, zui_rect(cx, cy, comp_w, ch), COL(ui, UI_COL_INPUT_BG), 0);
            text_in_cell(ui, cx, r.y, r.h, comp, COL(ui, UI_COL_TEXT));
            ui_push_rect(ui, zui_rect(cx, cy + ch - 1, comp_w, 1), COL(ui, UI_COL_TEXT), 0);
        }
        ui_push_rect(ui, zui_rect(cx + comp_w, cy, 1, ch), COL(ui, UI_COL_TEXT), 0);
    }
    else if (hovered)
        zui_request_cursor(ui, CURSOR_IBEAM);

    return changed;
}

/* ------------------------------------------------------------ selection -- */

bool zui_selectable(UiContext *ui, const char *label, bool selected)
{
    UiRect r = ui_layout_next(ui);
    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);

    if (selected)
        ui_push_round_rect(ui, r, COL(ui, UI_COL_SELECT), 0, ui->style.rounding);
    else if (hovered)
        ui_push_round_rect(ui, r, COL(ui, UI_COL_BUTTON_HOVER), 0, ui->style.rounding);

    label_in_cell(ui, r.x + ui->style.pad_x, r.y, r.h, label, COL(ui, UI_COL_TEXT));

    if (ui->in.mouse_released && ui->active == id && hovered)
    {
        ui->active = 0;
        return true;
    }
    if (ui->in.mouse_released && ui->active == id)
        ui->active = 0;
    return false;
}

int zui_listbox(UiContext *ui, const char *id_str, const char **items, int count, int *selected, int rows)
{
    UiRect r = ui_layout_next(ui);
    int row_h = ui->style.row_height;
    /* The visible height is the cell we were given; `rows` is only a hint used
       when the cell has no height of its own. */
    int total_h = r.h > row_h ? r.h : (rows < 1 ? 5 : rows) * row_h;
    r.h = total_h;
    int content_h = row_h * count;

    ZuiId gid = zui_id(ui, id_str);
    float *scroll = zui_state_float(ui, gid, 0.0f);
    int max_scroll = content_h > total_h ? content_h - total_h : 0;

    /* mouse wheel scrolls when the pointer is over the list */
    if (max_scroll > 0 && ui->in.wheel_y != 0.0f && ui_hovered(ui, r))
        *scroll -= ui->in.wheel_y * row_h * 3.0f;
    if (*scroll > max_scroll) *scroll = (float)max_scroll;
    if (*scroll < 0) *scroll = 0;

    /* background */
    ui_push_round_rect(ui, r, COL(ui, UI_COL_INPUT_BG), 0, ui->style.rounding);
    if (ui->style.border)
        ui_push_round_rect(ui, r, COL(ui, UI_COL_BORDER), ui->style.border, ui->style.rounding);

    /* clip to listbox bounds, subtract scrollbar width if needed */
    UiRect clip = r;
    if (content_h > total_h)
        clip.w -= ui->style.scrollbar;
    UiRect saved_clip = ui->clip;
    ui->clip = clip;

    int start_y = r.y - (int)*scroll;
    int changed_idx = -1;

    for (int i = 0; i < count; i++)
    {
        int iy = start_y + i * row_h;
        if (iy + row_h < r.y || iy > r.y + total_h)
            continue; /* outside visible area */
        UiRect row_rect = zui_rect(r.x, iy, clip.w, row_h);
        int id = ui_next_id(ui);
        /* clamp the hover test to the visible viewport and respect popups above */
        bool hovered = ui_mouse_in(ui, clip) && ui_hovered(ui, row_rect);
        if (hovered) ui->hot = id;
        if (hovered && ui->in.mouse_pressed) ui->active = id;

        if (*selected == i)
            ui_push_round_rect(ui, row_rect, COL(ui, UI_COL_SELECT), 0, 2);
        else if (hovered)
            ui_push_round_rect(ui, row_rect, COL(ui, UI_COL_BUTTON_HOVER), 0, 2);

        text_in_cell(ui, r.x + 4, iy, row_h, items[i], COL(ui, UI_COL_TEXT));

        if (ui->in.mouse_released && ui->active == id && hovered)
        {
            *selected = i;
            changed_idx = i;
            ui->active = 0;
        }
    }

    ui->clip = saved_clip;

    /* scrollbar */
    if (content_h > total_h)
    {
        UiRect sb = zui_rect(r.x + r.w - ui->style.scrollbar, r.y, ui->style.scrollbar, total_h);
        zui_scrollbar(ui, sb, scroll, (float)content_h, (float)total_h, true);
    }

    return changed_idx;
}

bool zui_combo(UiContext *ui, const char *id_str, const char **items, int count, int *selected)
{
    UiRect r = ui_layout_next(ui);
    ZuiId cid = zui_id(ui, id_str);
    int *open = zui_state_int(ui, cid, 0);

    /* closed button with chevron */
    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);

    UiColor bg = (*open || hovered) ? COL(ui, UI_COL_BUTTON_HOVER) : COL(ui, UI_COL_BUTTON);
    ui_push_round_rect(ui, r, bg, 0, ui->style.rounding);
    if (ui->style.border)
        ui_push_round_rect(ui, r, COL(ui, UI_COL_BORDER), ui->style.border, ui->style.rounding);

    if (*selected >= 0 && *selected < count)
        text_in_cell(ui, r.x + 8, r.y, r.h, items[*selected], COL(ui, UI_COL_TEXT));

    int chev_s = r.h - 8;
    UiRect chev = zui_rect(r.x + r.w - chev_s - 4, r.y + (r.h - chev_s) / 2, chev_s, chev_s);
    ui_push_icon(ui, chev, *open ? UI_ICON_ARROW_DOWN : UI_ICON_CHEVRON, COL(ui, UI_COL_TEXT_DIM));

    bool result = false;

    /* toggle on click */
    if (ui->in.mouse_released && ui->active == id && hovered)
    {
        *open = !*open;
        ui->active = 0;
    }

    if (*open)
    {
        int row_h = ui->style.row_height;
        int rows = count < 8 ? count : 8;
        int popup_h = rows * row_h + 4;

        /* open downward, but flip above the field if it would overflow the screen */
        int py = r.y + r.h;
        if (py + popup_h > ui->screen_h && r.y - popup_h >= 0)
            py = r.y - popup_h;
        UiRect popup = zui_rect(r.x, py, r.w, popup_h);

        /* float the popup above everything and block input behind it */
        int16_t saved_layer = ui->layer;
        UiRect  saved_clip = ui->clip;
        ui->layer = ui_popup_open(ui, popup);
        ui->clip = popup; /* escape the parent window's content clip */

        ui_push_round_rect(ui, popup, COL(ui, UI_COL_WINDOW_BG), 0, ui->style.rounding);
        ui_push_round_rect(ui, popup, COL(ui, UI_COL_BORDER), 1, ui->style.rounding);

        for (int i = 0; i < count && i < rows; i++)
        {
            UiRect item_r = zui_rect(popup.x + 2, popup.y + 2 + i * row_h,
                                     popup.w - 4, row_h);
            int iid = ui_next_id(ui);
            bool ihover = ui_mouse_in(ui, item_r); /* already on the top layer */
            if (ihover) ui->hot = iid;
            if (ihover && ui->in.mouse_pressed) ui->active = iid;

            if (*selected == i)
                ui_push_round_rect(ui, item_r, COL(ui, UI_COL_SELECT), 0, 2);
            else if (ihover)
                ui_push_round_rect(ui, item_r, COL(ui, UI_COL_BUTTON_HOVER), 0, 2);

            text_in_cell(ui, item_r.x + 4, item_r.y, row_h, items[i], COL(ui, UI_COL_TEXT));

            if (ui->in.mouse_released && ui->active == iid && ihover)
            {
                *selected = i;
                *open = 0;
                ui->active = 0;
                result = true;
            }
        }

        /* click anywhere outside the field and the popup closes it */
        if (ui->in.mouse_pressed && !ui_mouse_in(ui, popup) && !ui_mouse_in(ui, r))
            *open = 0;

        ui->layer = saved_layer;
        ui->clip = saved_clip;
    }

    if (ui->in.mouse_released && ui->active == id)
        ui->active = 0;
    return result;
}

/* --------------------------------------------------------------- menus -- */

void zui_menubar_begin(UiContext *ui)
{
    /* Flush to the top-left of the window, full width (like a real menu bar). */
    UiRect bar = zui_rect(0, 0, ui->screen_w, ui->style.title_height);
    ui_push_rect(ui, bar, COL(ui, UI_COL_TITLE), 0);
    ui_push_rect(ui, zui_rect(bar.x, bar.y + bar.h - 1, bar.w, 1), COL(ui, UI_COL_BORDER), 0);

    ui->in_menubar = true;
    ui->menubar_rect = bar;
    ui->menubar_x = bar.x + ui->style.spacing_x;

    /* Push the root layout's cursor below the bar so root widgets don't overlap. */
    UiLayout *L = ui_top(ui);
    if (L->cursor_y < bar.y + bar.h + ui->style.spacing_y)
        L->cursor_y = bar.y + bar.h + ui->style.spacing_y;
    L->col = 0;
}

void zui_menubar_end(UiContext *ui)
{
    ui->in_menubar = false;
}

bool zui_menu_begin(UiContext *ui, const char *label, int width)
{
    UiRect r;
    if (ui->in_menubar)
    {
        int tw = zui_text_width(ui->font, label, -1) + 2 * ui->style.pad_x + 8;
        r = zui_rect(ui->menubar_x, ui->menubar_rect.y, tw, ui->menubar_rect.h);
        ui->menubar_x += tw;
    }
    else
        r = ui_layout_next(ui);
    ZuiId mid = zui_id(ui, label);
    int *open = zui_state_int(ui, mid, 0);
    /* Content height in px, measured in menu_end last frame (covers any widget,
       not just menu_items: checkboxes, labels, ...). */
    int *boxh = zui_state_int(ui, mid + 1, 4 * ui->style.row_height);

    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);

    /* Toggle on the label; closing on an outside click is handled below once the
       popup rect is known, so clicks on items/checkboxes don't close the menu. */
    if (hovered && ui->in.mouse_pressed)
        *open = !*open;

    UiColor mtxt = COL(ui, UI_COL_TEXT);
    if (*open)
    {
        ui_push_rect(ui, r, COL(ui, UI_COL_ACCENT), 0);
        mtxt = ZUI_RGB(255, 255, 255);
    }
    else if (hovered)
        ui_push_rect(ui, r, COL(ui, UI_COL_BUTTON_HOVER), 0);

    int mlw = zui_text_width(ui->font, label, -1);
    text_in_cell(ui, r.x + (r.w - mlw) / 2, r.y, r.h, label, mtxt);

    if (*open)
    {
        int vpad = 4; /* tight vertical padding so there's no gap at the top */
        int menu_h = (*boxh > 0 ? *boxh : ui->style.row_height) + 2 * vpad;
        if (width <= 0) width = 160;
        UiRect popup = zui_rect(r.x, r.y + r.h, width, menu_h);
        if (popup.x + popup.w > ui->screen_w) popup.x = ui->screen_w - popup.w;
        if (popup.x < 0) popup.x = 0;
        if (popup.y + popup.h > ui->screen_h) popup.y = ui->screen_h - popup.h;
        if (popup.y < 0) popup.y = 0;

        int16_t layer = ui_popup_open(ui, popup);
        int16_t saved = ui->layer;
        ui->layer = layer;
        ui_push_round_rect(ui, popup, COL(ui, UI_COL_WINDOW_BG), 0, ui->style.rounding);
        ui_push_round_rect(ui, popup, COL(ui, UI_COL_BORDER), 1, ui->style.rounding);

        UiRect content = zui_rect(popup.x + ui->style.pad_x, popup.y + vpad,
                                  popup.w - 2 * ui->style.pad_x, popup.h);
        ui_push_container(ui, content, layer);
        ui->clip = popup; /* clip items to the popup (push saved the outer clip) */
        (void)saved;
        ui->menu_id = mid;
        ui->menu_item_n = 0;

        /* click outside the popup (and not on the label) closes the menu */
        if (ui->in.mouse_pressed && !ui_mouse_in(ui, popup) && !ui_mouse_in(ui, r))
            *open = 0;
        return true;
    }
    return false;
}

/* Pixel height the current popup container has consumed so far. */
static int ui_menu_used_h(UiContext *ui)
{
    UiLayout *L = ui_top(ui);
    int used = L->cursor_y - L->content.y;
    if (L->col != 0) /* a row in progress hasn't advanced the cursor yet */
        used += L->row_height + ui->style.spacing_y;
    return used > 0 ? used : ui->style.row_height;
}

void zui_menu_end(UiContext *ui)
{
    /* store the measured content height so next frame's box fits exactly */
    int used = ui_menu_used_h(ui);
    ui_pop_container(ui);
    int *boxh = zui_state_int(ui, ui->menu_id + 1, used);
    *boxh = used;
}

/* One dropdown row (Qt-style): full-width accent highlight on hover with bright
   text, a fixed left gutter for a check/radio/icon, optional right shortcut. */
static bool menu_row(UiContext *ui, const char *label, bool has_mark, UiIcon mark,
                     bool on, const char *shortcut)
{
    ui->menu_item_n++;
    UiRect r = ui_layout_next(ui);
    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);

    int gutter = r.h; /* square slot for the mark, text aligns past it */
    if (hovered)
        ui_push_rect(ui, r, COL(ui, UI_COL_ACCENT), 0);

    UiColor txt = hovered ? ZUI_RGB(255, 255, 255) : COL(ui, UI_COL_TEXT);
    UiColor dim = hovered ? ZUI_RGB(235, 235, 240) : COL(ui, UI_COL_TEXT_DIM);

    if (has_mark && on)
    {
        int ms = r.h - 10; if (ms < 10) ms = 10;
        UiRect mb = zui_rect(r.x + (gutter - ms) / 2, r.y + (r.h - ms) / 2, ms, ms);
        ui_push_icon(ui, mb, mark, txt);
    }
    label_in_cell(ui, r.x + gutter, r.y, r.h, label, txt);
    if (shortcut && shortcut[0])
    {
        int sw = zui_text_width(ui->font, shortcut, -1);
        text_in_cell(ui, r.x + r.w - sw - ui->style.pad_x, r.y, r.h, shortcut, dim);
    }

    if (ui->in.mouse_released && ui->active == id && hovered)
    {
        ui->active = 0;
        int *open = zui_state_int(ui, ui->menu_id, 0); /* selecting closes the menu */
        *open = 0;
        return true;
    }
    return false;
}

bool zui_menu_item(UiContext *ui, const char *label)
{
    return menu_row(ui, label, false, UI_ICON_NONE, false, NULL);
}

void zui_menu_separator(UiContext *ui)
{
    ui->menu_item_n++;
    UiRect r = ui_layout_next(ui);
    int y = r.y + r.h / 2;
    ui_push_rect(ui, zui_rect(r.x + 4, y, r.w - 8, 1), COL(ui, UI_COL_BORDER), 0);
}

bool zui_menu_item_check(UiContext *ui, const char *label, bool *checked)
{
    bool hit = menu_row(ui, label, true, UI_ICON_CHECK, checked && *checked, NULL);
    if (hit && checked)
        *checked = !*checked;
    return hit;
}

bool zui_menu_item_radio(UiContext *ui, const char *label, int *selected, int value)
{
    bool hit = menu_row(ui, label, true, UI_ICON_DOT, selected && *selected == value, NULL);
    if (hit && selected)
        *selected = value;
    return hit;
}

bool zui_context_menu_begin(UiContext *ui, const char *id_str, int width)
{
    ZuiId cid = zui_id(ui, id_str);
    int *open  = zui_state_int(ui, cid, 0);
    int *ax    = zui_state_int(ui, cid + 1, 0); /* anchor, set when it opens */
    int *ay    = zui_state_int(ui, cid + 2, 0);
    int *boxh = zui_state_int(ui, cid + 3, 4 * ui->style.row_height);

    /* right-click anywhere opens it at the cursor */
    if (ui->in.mouse_pressed_r)
    {
        *open = 1;
        *ax = ui->in.mouse_x;
        *ay = ui->in.mouse_y;
    }
    if (!*open)
        return false;

    if (width <= 0) width = 180;
    int vpad = 4;
    int h = (*boxh > 0 ? *boxh : ui->style.row_height) + 2 * vpad;
    int px = *ax, py = *ay;
    if (px + width > ui->screen_w)  px = ui->screen_w - width;
    if (py + h > ui->screen_h)      py = ui->screen_h - h;
    UiRect popup = zui_rect(px, py, width, h);

    int16_t layer = ui_popup_open(ui, popup);
    ui->layer = layer;
    ui_push_round_rect(ui, popup, COL(ui, UI_COL_WINDOW_BG), 0, ui->style.rounding);
    ui_push_round_rect(ui, popup, COL(ui, UI_COL_BORDER), 1, ui->style.rounding);

    /* a left-click outside dismisses it */
    if (ui->in.mouse_pressed && !ui_mouse_in(ui, popup))
        *open = 0;

    UiRect content = zui_rect(popup.x + ui->style.pad_x, popup.y + vpad,
                              popup.w - 2 * ui->style.pad_x, popup.h);
    ui_push_container(ui, content, layer);
    ui->clip = popup;
    ui->menu_id = cid; /* so zui_menu_item closes this popup on selection */
    ui->menu_item_n = 0;
    return true;
}

void zui_context_menu_end(UiContext *ui)
{
    int used = ui_menu_used_h(ui);
    ui_pop_container(ui);
    int *boxh = zui_state_int(ui, ui->menu_id + 3, used);
    *boxh = used;
}

/* --------------------------------------------------------- tree / toggle -- */

bool zui_tree_node(UiContext *ui, const char *label)
{
    UiRect r = ui_layout_next(ui);
    ZuiId tid = zui_id(ui, label);
    int *expanded = zui_state_int(ui, tid, 0);

    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);

    /* arrow icon */
    int arrow_s = r.h - 6;
    if (arrow_s < 8) arrow_s = 8;
    UiRect arrow = zui_rect(r.x + 2, r.y + (r.h - arrow_s) / 2, arrow_s, arrow_s);
    ui_push_icon(ui, arrow, *expanded ? UI_ICON_ARROW_DOWN : UI_ICON_ARROW_RIGHT,
                 COL(ui, UI_COL_TEXT));

    label_in_cell(ui, r.x + arrow_s + 4, r.y, r.h, label, COL(ui, UI_COL_TEXT));

    if (hovered && ui->in.mouse_released && ui->active == id)
    {
        *expanded = !*expanded;
        ui->active = 0;
        return *expanded;
    }
    if (ui->in.mouse_released && ui->active == id)
        ui->active = 0;

    return *expanded != 0;
}

void zui_tree_pop(UiContext *ui)
{
    zui_pop_id(ui);
}

int zui_toggle_group(UiContext *ui, const char **labels, int count, int *selected)
{
    zui_row(ui, 0, count);
    int changed = -1;
    for (int i = 0; i < count; i++)
    {
        UiRect r = ui_layout_next(ui);
        int id = ui_next_id(ui);
        bool hovered = ui_hit(ui, id, r);
        bool active = (ui->active == id && ui->in.mouse_down);

        UiColor bg;
        if (*selected == i)
            bg = COL(ui, UI_COL_ACCENT);
        else if (active)
            bg = COL(ui, UI_COL_BUTTON_ACTIVE);
        else if (hovered)
            bg = COL(ui, UI_COL_BUTTON_HOVER);
        else
            bg = COL(ui, UI_COL_BUTTON);

        ui_push_round_rect(ui, r, bg, 0, ui->style.rounding);
        text_in_cell(ui, r.x + 4, r.y, r.h, labels[i], COL(ui, UI_COL_TEXT));

        if (ui->in.mouse_released && ui->active == id && hovered)
        {
            *selected = i;
            changed = i;
            ui->active = 0;
        }
    }
    return changed;
}

/* ------------------------------------------------------------- tabs / split - */

int zui_tabs(UiContext *ui, const char *id, const char **labels, int count, int *active)
{
    zui_push_id(ui, id);
    zui_row(ui, ui->style.row_height + 4, 1);
    UiRect row = ui_layout_next(ui); /* a full-width strip */
    int changed = -1;

    /* Editor-style strip: darker header bar with a baseline; the active tab is
       filled with the body colour so it reads as part of the content below, with
       an accent bar on top. No rounding, tabs sit flush. */
    UiColor body   = COL(ui, UI_COL_PANEL);
    UiColor strip  = COL(ui, UI_COL_INPUT_BG);
    UiColor border = COL(ui, UI_COL_BORDER);

    ui_push_rect(ui, row, strip, 0);
    ui_push_rect(ui, zui_rect(row.x, row.y + row.h - 1, row.w, 1), border, 0); /* baseline */

    int x = row.x;
    for (int i = 0; i < count; i++)
    {
        int tw = zui_text_width(ui->font, labels[i], -1) + 2 * ui->style.pad_x + 10;
        UiRect t = zui_rect(x, row.y, tw, row.h);
        x += tw;

        int wid = ui_next_id(ui);
        bool hovered = ui_hit(ui, wid, t);
        bool act = (*active == i);

        if (act)
        {
            /* fill with body colour, covering the baseline -> "open" tab */
            ui_push_rect(ui, t, body, 0);
            ui_push_rect(ui, zui_rect(t.x, t.y, t.w, 2), COL(ui, UI_COL_ACCENT), 0);
            ui_push_rect(ui, zui_rect(t.x, t.y, 1, t.h), border, 0);
            ui_push_rect(ui, zui_rect(t.x + t.w - 1, t.y, 1, t.h), border, 0);
        }
        else if (hovered)
            ui_push_rect(ui, zui_rect(t.x, t.y + 2, t.w, t.h - 3), COL(ui, UI_COL_BUTTON_HOVER), 0);

        int lw = zui_text_width(ui->font, labels[i], -1);
        text_in_cell(ui, t.x + (t.w - lw) / 2, t.y, t.h, labels[i],
                     COL(ui, act ? UI_COL_TEXT : UI_COL_TEXT_DIM));

        if (ui->in.mouse_released && ui->active == wid && hovered)
        {
            *active = i;
            changed = i;
            ui->active = 0;
        }
    }
    zui_pop_id(ui);
    return changed;
}

int zui_splitter(UiContext *ui, const char *id, UiRect bounds, bool vertical)
{
    zui_push_id(ui, id);
    int wid = ui_next_id(ui);
    bool hovered = ui_hit(ui, wid, bounds);
    bool dragging = (ui->active == wid && ui->in.mouse_down);
    int delta = 0;

    if (dragging)
        delta = vertical ? ui->in.mouse_dx : ui->in.mouse_dy;
    if (hovered || dragging)
        zui_request_cursor(ui, vertical ? CURSOR_RESIZE_EW : CURSOR_RESIZE_NS);

    ui_push_rect(ui, bounds, (hovered || dragging) ? COL(ui, UI_COL_ACCENT)
                                                   : COL(ui, UI_COL_BORDER), 0);
    if (ui->in.mouse_released && ui->active == wid)
        ui->active = 0;

    zui_pop_id(ui);
    return delta;
}

/* ----------------------------------------------------- inspector widgets --- */

/* One numeric field drawn into a given rect: drag horizontally to scrub the
   value; optional tinted tag (X/Y/Z). The building block of drag_float[3]. */
static bool drag_float_rect(UiContext *ui, UiRect r, int id, float *v, float speed,
                            float lo, float hi, const char *tag, UiColor tagcol)
{
    bool hovered = ui_hit(ui, id, r);
    bool changed = false;
    bool editing = (ui->focus == id);

    if (!editing)
    {
        if (ui->in.mouse_pressed && hovered)
            ui->press_move = 0;
        if (ui->active == id && ui->in.mouse_down)
        {
            ui->press_move += ui->in.mouse_dx < 0 ? -ui->in.mouse_dx : ui->in.mouse_dx;
            if (ui->in.mouse_dx != 0)
            {
                *v += (float)ui->in.mouse_dx * speed;
                if (hi > lo) { if (*v < lo) *v = lo; if (*v > hi) *v = hi; }
                changed = true;
            }
            zui_request_cursor(ui, CURSOR_RESIZE_EW);
        }
        else if (hovered)
            zui_request_cursor(ui, CURSOR_RESIZE_EW);

        /* a click without a scrub focuses the field for keyboard entry */
        if (ui->in.mouse_released && ui->active == id && hovered && ui->press_move < 3)
        {
            ui->focus = id;
            snprintf(ui->edit_buf, sizeof ui->edit_buf, "%g", *v);
            ui->caret = (int)strlen(ui->edit_buf);
            editing = true;
        }
        if (ui->in.mouse_released && ui->active == id)
            ui->active = 0;
    }

    if (editing)
    {
        char *b = ui->edit_buf;
        int cap = (int)sizeof ui->edit_buf;
        int len = (int)strlen(b);
        if (ui->caret > len) ui->caret = len;
        for (int k = 0; k < ui->in.key_count; k++)
        {
            int key = ui->in.keys[k];
            if (key == KEY_BACKSPACE && ui->caret > 0)
            { memmove(b + ui->caret - 1, b + ui->caret, (size_t)(len - ui->caret + 1)); ui->caret--; len--; }
            else if (key == KEY_DELETE && ui->caret < len)
            { memmove(b + ui->caret, b + ui->caret + 1, (size_t)(len - ui->caret)); len--; }
            else if (key == KEY_LEFT && ui->caret > 0) ui->caret--;
            else if (key == KEY_RIGHT && ui->caret < len) ui->caret++;
            else if (key == KEY_HOME) ui->caret = 0;
            else if (key == KEY_END) ui->caret = len;
            else if (key == KEY_ENTER || key == KEY_TAB) ui->focus = 0;
        }
        for (int c = 0; c < ui->in.char_count; c++)
        {
            uint32_t cp = ui->in.chars[c];
            if (!((cp >= '0' && cp <= '9') || cp == '.' || cp == '-' || cp == '+'))
                continue;
            if (len + 1 >= cap) continue;
            memmove(b + ui->caret + 1, b + ui->caret, (size_t)(len - ui->caret + 1));
            b[ui->caret] = (char)cp; ui->caret++; len++;
        }
        if (ui->in.mouse_pressed && !hovered)
            ui->focus = 0;
        if (ui->focus != id) /* committed (Enter / Tab / clicked away) */
        {
            *v = (float)atof(b);
            if (hi > lo) { if (*v < lo) *v = lo; if (*v > hi) *v = hi; }
            changed = true;
            editing = false;
        }
    }

    ui_push_round_rect(ui, r, COL(ui, UI_COL_INPUT_BG), 0, ui->style.rounding);
    ui_push_round_rect(ui, r, (hovered || ui->active == id || editing) ? COL(ui, UI_COL_ACCENT)
                                                                       : COL(ui, UI_COL_BORDER),
                       editing ? 2 : 1, ui->style.rounding);

    int left = r.x;
    if (tag && tag[0])
    {
        UiRect tg = zui_rect(r.x, r.y, 16, r.h);
        ui_push_round_rect(ui, tg, tagcol, 0, ui->style.rounding);
        ui_push_rect(ui, zui_rect(tg.x + tg.w - 2, tg.y, 2, tg.h), tagcol, 0);
        text_in_cell(ui, tg.x + 5, r.y, r.h, tag, ZUI_RGB(255, 255, 255));
        left = tg.x + tg.w;
    }

    if (editing)
    {
        text_in_cell(ui, left + 6, r.y, r.h, ui->edit_buf, COL(ui, UI_COL_TEXT));
        int cx = left + 6 + zui_text_width(ui->font, ui->edit_buf, ui->caret);
        int chh = zui_font_height(ui->font);
        ui_push_rect(ui, zui_rect(cx, r.y + (r.h - chh) / 2, 1, chh), COL(ui, UI_COL_TEXT), 0);
        zui_request_cursor(ui, CURSOR_IBEAM);
    }
    else
    {
        char buf[32];
        snprintf(buf, sizeof buf, "%.2f", *v);
        int tw = zui_text_width(ui->font, buf, -1);
        int avail = r.x + r.w - left;
        text_in_cell(ui, left + (avail - tw) / 2, r.y, r.h, buf, COL(ui, UI_COL_TEXT));
    }
    return changed;
}

bool zui_drag_float(UiContext *ui, const char *label, float *v, float speed, float min, float max)
{
    (void)label;
    UiRect r = ui_layout_next(ui);
    return drag_float_rect(ui, r, ui_next_id(ui), v, speed, min, max, NULL, 0);
}

bool zui_drag_float3(UiContext *ui, const char *label, float v[3], float speed)
{
    (void)label;
    UiRect cell = ui_layout_next(ui);
    const char *tags[3] = {"X", "Y", "Z"};
    UiColor cols[3] = {ZUI_RGB(196, 78, 82), ZUI_RGB(106, 168, 106), ZUI_RGB(78, 124, 196)};
    int gap = 4;
    int w = (cell.w - 2 * gap) / 3;
    bool changed = false;
    for (int i = 0; i < 3; i++)
    {
        UiRect r = zui_rect(cell.x + i * (w + gap), cell.y, w, cell.h);
        if (drag_float_rect(ui, r, ui_next_id(ui), &v[i], speed, 0, 0, tags[i], cols[i]))
            changed = true;
    }
    return changed;
}

bool zui_collapsing_header(UiContext *ui, const char *label)
{
    zui_row(ui, ui->style.row_height, 1); /* always a full-width block */
    UiRect r = ui_layout_next(ui);
    ZuiId hid = zui_id(ui, label);
    int *open = zui_state_int(ui, hid, 1);

    int id = ui_next_id(ui);
    bool hovered = ui_hit(ui, id, r);
    ui_push_rect(ui, r, hovered ? COL(ui, UI_COL_BUTTON_HOVER) : COL(ui, UI_COL_TITLE), 0);

    int as = r.h - 12; if (as < 8) as = 8;
    UiRect ar = zui_rect(r.x + 6, r.y + (r.h - as) / 2, as, as);
    ui_push_icon(ui, ar, *open ? UI_ICON_ARROW_DOWN : UI_ICON_ARROW_RIGHT, COL(ui, UI_COL_TEXT));
    text_in_cell(ui, r.x + as + 12, r.y, r.h, label, COL(ui, UI_COL_TEXT));

    if (ui->in.mouse_released && ui->active == id && hovered)
    {
        *open = !*open;
        ui->active = 0;
    }
    return *open != 0;
}

void zui_prop_row(UiContext *ui, const char *label, float label_frac)
{
    if (label_frac <= 0.0f) label_frac = 0.40f;
    if (label_frac >= 1.0f) label_frac = 0.40f;
    zui_row_begin(ui, ui->style.row_height - 4, 2);
    zui_row_push(ui, label_frac);
    zui_label(ui, label);
    zui_row_push(ui, 1.0f - label_frac);
}

int zui_statusbar_height(UiContext *ui)
{
    return ui->style.row_height;
}

void zui_statusbar_begin(UiContext *ui)
{
    int h = ui->style.row_height;
    UiRect bar = zui_rect(0, ui->screen_h - h, ui->screen_w, h);
    ui_push_rect(ui, bar, COL(ui, UI_COL_TITLE), 0);
    ui_push_rect(ui, zui_rect(bar.x, bar.y, bar.w, 1), COL(ui, UI_COL_BORDER), 0); /* top edge */

    UiRect content = zui_rect(bar.x + ui->style.pad_x, bar.y,
                              bar.w - 2 * ui->style.pad_x, h);
    ui_push_container(ui, content, ui->layer);
    ui->clip = bar;
    ui_top(ui)->row_height = h; /* rows fill the bar so text centres vertically */
}

void zui_statusbar_end(UiContext *ui)
{
    ui_pop_container(ui);
}

/* --------------------------------------------------------------- tooltip -- */

void zui_tooltip(UiContext *ui, const char *text)
{
    /* tooltip appears when the mouse is hovering over a widget */
    /* In a real impl, we'd track hover time. For now, just draw near mouse. */
    int tw = zui_text_width(ui->font, text, -1) + 12;
    int th = ui->font ? zui_font_height(ui->font) + 8 : 28;
    UiRect r = zui_rect(ui->in.mouse_x + 16, ui->in.mouse_y + 16, tw, th);
    ui_push_round_rect(ui, r, COL(ui, UI_COL_INPUT_BG), 0, ui->style.rounding);
    ui_push_round_rect(ui, r, COL(ui, UI_COL_BORDER), 1, ui->style.rounding);
    ui_push_text(ui, r.x + 6, r.y + 4, text, COL(ui, UI_COL_TEXT));
}

/* ------------------------------------------------------------ scrollbar --- */

bool zui_scrollbar(UiContext *ui, UiRect bounds, float *offset, float content, float view, bool vertical)
{
    if (content <= view)
        return false;
    int id = ui_next_id(ui);
    ui_hit(ui, id, bounds);

    float frac = view / content;
    float max_off = content - view;

    /* track */
    ui_push_round_rect(ui, bounds, COL(ui, UI_COL_TRACK), 0, ui->style.rounding);

    /* knob */
    float knob_len = vertical
        ? (float)bounds.h * frac
        : (float)bounds.w * frac;
    if (knob_len < 8.0f)
        knob_len = 8.0f;
    float usable = vertical
        ? (float)bounds.h - knob_len
        : (float)bounds.w - knob_len;
    float knob_pos = max_off > 0 ? (*offset / max_off) * usable : 0;
    UiRect knob;
    if (vertical)
    {
        knob = zui_rect(bounds.x + 1, bounds.y + (int)knob_pos,
                         bounds.w - 2, (int)knob_len);
    }
    else
    {
        knob = zui_rect(bounds.x + (int)knob_pos, bounds.y + 1,
                         (int)knob_len, bounds.h - 2);
    }
    ui_push_round_rect(ui, knob, COL(ui, UI_COL_KNOB), 0, ui->style.rounding);

    /* drag */
    bool dragging = false;
    if (ui->active == id && ui->in.mouse_down)
    {
        float t = vertical
            ? (float)(ui->in.mouse_y - bounds.y - (int)(knob_len * 0.5f)) / usable
            : (float)(ui->in.mouse_x - bounds.x - (int)(knob_len * 0.5f)) / usable;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        *offset = t * max_off;
        dragging = true;
    }
    if (ui->in.mouse_released && ui->active == id)
        ui->active = 0;
    return dragging;
}

/* ------------------------------------------------------------- scroll group - */

bool zui_group_begin(UiContext *ui, const char *id_str, UiRect bounds, int flags)
{
    if (bounds.w <= 0 || bounds.h <= 0)
        return false;

    ZuiId gid = zui_id(ui, id_str);
    float *scroll_y = zui_state_float(ui, gid, 0.0f);
    int   *had_sb   = zui_state_int(ui, gid + 2, 0); /* did it scroll last frame? */

    if (flags & ZUI_GROUP_BORDER)
    {
        ui_push_round_rect(ui, bounds, COL(ui, UI_COL_PANEL), 0, ui->style.rounding);
        if (ui->style.border)
            ui_push_round_rect(ui, bounds, COL(ui, UI_COL_BORDER), ui->style.border, ui->style.rounding);
    }

    /* mouse wheel scrolls when the pointer is over the group */
    if (!(flags & ZUI_GROUP_NO_SCROLLBAR) && ui->in.wheel_y != 0.0f && ui_hovered(ui, bounds))
        *scroll_y -= ui->in.wheel_y * ui->style.row_height * 3.0f;
    if (*scroll_y < 0) *scroll_y = 0;

    /* Reserve room on the right for the scrollbar if it scrolled last frame. */
    int inner_w = bounds.w - 2 * ui->style.pad_x;
    if (*had_sb)
        inner_w -= ui->style.scrollbar;

    UiRect content = zui_rect(bounds.x + ui->style.pad_x,
                              bounds.y + ui->style.pad_y - (int)*scroll_y,
                              inner_w,
                              bounds.h - 2 * ui->style.pad_y);
    ui_push_container(ui, content, ui->layer);
    ui->clip = bounds; /* push saved the outer clip; group_end restores it */

    if (ui->group_sp < 4)
    {
        ui->group_stk[ui->group_sp].id = gid;
        ui->group_stk[ui->group_sp].bounds = bounds;
        ui->group_stk[ui->group_sp].scroll = scroll_y;
        ui->group_stk[ui->group_sp].flags = flags;
        ui->group_sp++;
    }
    return true;
}

void zui_group_end(UiContext *ui)
{
    UiRect bounds = {0,0,0,0};
    float *scroll = NULL;
    ZuiId gid = 0;
    int content_h = 0, flags = 0;

    if (ui->group_sp > 0)
    {
        UiLayout *L = ui_top(ui);
        /* content height = how far the cursor walked below the content top */
        content_h = L->cursor_y - L->content.y;
        ui->group_sp--;
        bounds = ui->group_stk[ui->group_sp].bounds;
        scroll = ui->group_stk[ui->group_sp].scroll;
        gid    = ui->group_stk[ui->group_sp].id;
        flags  = ui->group_stk[ui->group_sp].flags;
    }

    if (scroll && !(flags & ZUI_GROUP_NO_SCROLLBAR))
    {
        int view_h = bounds.h - 2 * ui->style.pad_y;
        int max_scroll = content_h > view_h ? content_h - view_h : 0;
        if (*scroll > max_scroll) *scroll = (float)max_scroll;
        if (*scroll < 0) *scroll = 0;

        int *had_sb = zui_state_int(ui, gid + 2, 0);
        *had_sb = max_scroll > 0 ? 1 : 0;

        /* Drawn before the pop so it keeps the group's layer (popping would reset
           the layer to the parent / 0 and hide the bar under later content). */
        if (max_scroll > 0)
        {
            UiRect sb = zui_rect(bounds.x + bounds.w - ui->style.scrollbar - 1,
                                 bounds.y + 1, ui->style.scrollbar, bounds.h - 2);
            zui_scrollbar(ui, sb, scroll, (float)content_h, (float)view_h, true);
        }
    }

    ui_pop_container(ui); /* restores the outer clip */
}

/* --------------------------------------------------------------- windows --- */

bool zui_window_begin(UiContext *ui, const char *title, UiRect *bounds)
{
    int th = ui->style.title_height;
    int id = ui_next_id(ui);
    int16_t wl = (int16_t)(++ui->window_count * 16); /* each window above the last */
    ui->layer = wl;

    /* title-bar drag (state persists across frames in the context) */
    UiRect tbar = zui_rect(bounds->x, bounds->y, bounds->w, th);
    bool on_title = ui_hovered(ui, tbar); /* not when a popup floats over it */
    if (on_title && ui->in.mouse_pressed)
    {
        ui->active = id;
        ui->drag_id = id;
        ui->drag_off_x = ui->in.mouse_x - bounds->x;
        ui->drag_off_y = ui->in.mouse_y - bounds->y;
    }
    if (ui->drag_id == id)
    {
        if (ui->in.mouse_down)
        {
            bounds->x = ui->in.mouse_x - ui->drag_off_x;
            bounds->y = ui->in.mouse_y - ui->drag_off_y;
            tbar.x = bounds->x;
            tbar.y = bounds->y;
        }
        else
        {
            ui->drag_id = 0;
            if (ui->active == id)
                ui->active = 0;
        }
    }

    /* chrome */
    ui_push_round_rect(ui, *bounds, COL(ui, UI_COL_WINDOW_BG), 0, ui->style.rounding);
    ui_push_rect(ui, tbar, on_title ? COL(ui, UI_COL_TITLE_ACTIVE) : COL(ui, UI_COL_TITLE), 0);
    if (ui->style.border)
        ui_push_round_rect(ui, *bounds, COL(ui, UI_COL_BORDER), ui->style.border, ui->style.rounding);
    text_in_cell(ui, bounds->x + 10, bounds->y, th, title, COL(ui, UI_COL_TEXT)); /* title text */

    UiRect content = zui_rect(bounds->x + ui->style.pad_x,
                              bounds->y + th + ui->style.pad_y,
                              bounds->w - 2 * ui->style.pad_x,
                              bounds->h - th - 2 * ui->style.pad_y);
    ui_push_container(ui, content, wl);
    /* Clip content to the window body so widgets (e.g. an expanded tree) can't
       spill outside. The push saved the previous clip; window_end restores it. */
    ui->clip = zui_rect(bounds->x, bounds->y + th,
                        bounds->w, bounds->h - th);
    return true;
}

void zui_window_end(UiContext *ui)
{
    ui_pop_container(ui);
}
