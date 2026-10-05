/*
 * ui_core.c - context lifecycle and the draw-command buffer.
 *
 * This is the backbone: the buffer that widgets push into and the renderer reads
 * back. It is fully implemented; the widgets and renderers that use it are still
 * stubs (ui_widgets.c, ui_render_*.c).
 */
#include "ui_internal.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ list -- */

void ui_dl_reset(UiDrawList *dl)
{
    dl->count = 0;
    dl->text_len = 0;
}

void ui_dl_free(UiDrawList *dl)
{
    free(dl->cmds);
    free(dl->text_arena);
    memset(dl, 0, sizeof(*dl));
}

UiCmd *ui_dl_push(UiContext *ui, UiCmdType type)
{
    UiDrawList *dl = &ui->dl;
    if (dl->count == dl->cap)
    {
        int ncap = dl->cap ? dl->cap * 2 : 256;
        UiCmd *n = (UiCmd *)realloc(dl->cmds, (size_t)ncap * sizeof(UiCmd));
        if (!n)
            return NULL;
        dl->cmds = n;
        dl->cap = ncap;
    }
    UiCmd *c = &dl->cmds[dl->count++];
    memset(c, 0, sizeof(*c));
    c->type = type;
    c->layer = ui->layer;
    c->clip = ui->clip;
    return c;
}

const char *ui_dl_intern(UiContext *ui, const char *str, int len)
{
    UiDrawList *dl = &ui->dl;
    if (len < 0)
        len = (int)strlen(str);
    if (dl->text_len + len + 1 > dl->text_cap)
    {
        int ncap = dl->text_cap ? dl->text_cap * 2 : 1024;
        while (ncap < dl->text_len + len + 1)
            ncap *= 2;
        char *n = (char *)realloc(dl->text_arena, (size_t)ncap);
        if (!n)
            return "";
        dl->text_arena = n;
        dl->text_cap = ncap;
    }
    char *dst = dl->text_arena + dl->text_len;
    memcpy(dst, str, (size_t)len);
    dst[len] = '\0';
    dl->text_len += len + 1;
    return dst;
}

/* Stable insertion sort keyed on layer. Command counts per frame are small
   (hundreds), and stability preserves issue order within a layer, which is the
   painter's-algorithm semantics widgets rely on. */
void ui_dl_sort(UiDrawList *dl)
{
    for (int i = 1; i < dl->count; ++i)
    {
        UiCmd key = dl->cmds[i];
        int j = i - 1;
        while (j >= 0 && dl->cmds[j].layer > key.layer)
        {
            dl->cmds[j + 1] = dl->cmds[j];
            --j;
        }
        dl->cmds[j + 1] = key;
    }
}

/* --------------------------------------------------------------- emitters -- */

void ui_push_rect(UiContext *ui, UiRect r, UiColor color, int thickness)
{
    ui_push_round_rect(ui, r, color, thickness, 0);
}

void ui_push_round_rect(UiContext *ui, UiRect r, UiColor color, int thickness, int rounding)
{
    UiCmd *c = ui_dl_push(ui, UI_CMD_RECT);
    if (!c)
        return;
    c->color = color;
    c->u.rect.rect = r;
    c->u.rect.thickness = thickness;
    c->u.rect.rounding = rounding;
}

void ui_push_text(UiContext *ui, int x, int y, const char *str, UiColor color)
{
    const char *s = ui_dl_intern(ui, str, -1);
    UiCmd *c = ui_dl_push(ui, UI_CMD_TEXT);
    if (!c)
        return;
    c->color = color;
    c->u.text.x = x;
    c->u.text.y = y;
    c->u.text.str = s;
    c->u.text.len = (int)strlen(s);
    c->u.text.font = ui->font;
}

void ui_push_line(UiContext *ui, int x0, int y0, int x1, int y1, UiColor color)
{
    UiCmd *c = ui_dl_push(ui, UI_CMD_LINE);
    if (!c)
        return;
    c->color = color;
    c->u.line.x0 = x0;
    c->u.line.y0 = y0;
    c->u.line.x1 = x1;
    c->u.line.y1 = y1;
}

void ui_push_icon(UiContext *ui, UiRect box, UiIcon icon, UiColor color)
{
    UiCmd *c = ui_dl_push(ui, UI_CMD_ICON);
    if (!c)
        return;
    c->color = color;
    c->u.icon.box = box;
    c->u.icon.icon = (int)icon;
}

void ui_push_gradient(UiContext *ui, UiRect r, UiColor top, UiColor bottom)
{
    UiCmd *c = ui_dl_push(ui, UI_CMD_GRADIENT);
    if (!c)
        return;
    c->u.gradient.rect = r;
    c->u.gradient.top = top;
    c->u.gradient.bottom = bottom;
}

/* -------------------------------------------------------------- lifecycle -- */

UiContext *zui_create(void)
{
    UiContext *ui = (UiContext *)calloc(1, sizeof(UiContext));
    return ui;
}

void zui_destroy(UiContext *ui)
{
    if (!ui)
        return;
    ui_dialog_free(ui);
    ui_dl_free(&ui->dl);
    free(ui);
}

void zui_begin(UiContext *ui, PlatformWindow *w)
{
    /* Carry interaction state across frames; reset per-frame id and draw list. */
    int saved_active = ui->active;
    int saved_hot = ui->hot;

    ui->win = w;
    ui->next_id = 0;
    ui->hot = saved_hot;
    ui->active = saved_active;
    ui->layer = 0;

    ui->in.mouse_x = mouse_x(w);
    ui->in.mouse_y = mouse_y(w);
    mouse_delta(w, &ui->in.mouse_dx, &ui->in.mouse_dy);
    mouse_wheel_v(w, &ui->in.wheel_x, &ui->in.wheel_y);
    ui->in.mouse_down = mouse_button_down(w, MOUSE_LEFT);
    ui->in.mouse_pressed = mouse_button_pressed(w, MOUSE_LEFT);
    ui->in.mouse_released = mouse_button_released(w, MOUSE_LEFT);
    ui->in.mouse_down_r = mouse_button_down(w, MOUSE_RIGHT);
    ui->in.mouse_pressed_r = mouse_button_pressed(w, MOUSE_RIGHT);
    ui->in.mouse_released_r = mouse_button_released(w, MOUSE_RIGHT);

    /* Drain the whole key/char queue for this frame so fast typing isn't lost. */
    ui->in.key_count = 0;
    for (int k; (k = key_get_pressed(w)) != 0 && ui->in.key_count < 16;)
        ui->in.keys[ui->in.key_count++] = k;
    ui->in.char_count = 0;
    for (uint32_t cp; (cp = char_get_pressed(w)) != 0 && ui->in.char_count < 16;)
        ui->in.chars[ui->in.char_count++] = cp;
    ui->in.last_key = ui->in.key_count ? ui->in.keys[0] : 0;
    ui->in.last_char = ui->in.char_count ? ui->in.chars[0] : 0;

    int ww, wh;
    window_get_framebuffer_size(w, &ww, &wh);
    ui->clip = zui_rect(0, 0, ww, wh);
    ui->screen_w = ww;
    ui->screen_h = wh;

    if (!ui->font)
        ui->font = zui_font_default(16);
    if (ui->style.colors[UI_COL_TEXT] == 0 && ui->style.row_height == 0)
        zui_style_dark(&ui->style); /* first frame: a usable default theme */

    /* Reset the container stack, id stack, and seed a root region over the
       whole surface, inset by the style padding, so widgets work without a window. */
    ui->container_sp = 0;
    ui->window_count = 0;
    ui->color_sp = 0;
    ui->id_sp = 0;
    ui->layer = 0;
    ui->popup_layer = 2000; /* well above any window layer (window_count*16) */
    ui->group_sp = 0;
    ui->in_menubar = false;
    ui->overlay_active = false;
    /* An open modal dialog blocks all widgets below the popup layer for the whole
       frame, regardless of where zui_dialog is called in the frame. */
    if (ui->dlg.open)
    {
        ui->overlay_active = true;
        ui->overlay_rect = zui_rect(0, 0, ww, wh);
        ui->overlay_layer = 2000;
    }
    ui->want_cursor = -1;
    ui->hot = 0; /* recomputed each frame from hover; active/focus persist */
    UiRect root = zui_rect(ui->style.pad_x, ui->style.pad_y,
                           ww - 2 * ui->style.pad_x, wh - 2 * ui->style.pad_y);
    ui_push_container(ui, root, 0);

    ui_dl_reset(&ui->dl);
}

void zui_set_font(UiContext *ui, UiFont *f)
{
    ui->font = f;
}

void zui_end(UiContext *ui)
{
    ui_dl_sort(&ui->dl);
    if (ui->win)
        mouse_set_cursor(ui->win, ui->want_cursor >= 0 ? ui->want_cursor : CURSOR_DEFAULT);
}

const UiDrawList *zui_draw_list(UiContext *ui)
{
    return &ui->dl;
}

void zui_request_cursor(UiContext *ui, int cursor)
{
    ui->want_cursor = cursor; /* applied against the window in zui_end */
}
