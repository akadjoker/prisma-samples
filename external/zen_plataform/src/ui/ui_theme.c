/*
 * ui_theme.c - style presets and the scoped color stack.
 */
#include "ui_internal.h"

static void common_metrics(UiStyle *s)
{
    s->pad_x = 10;
    s->pad_y = 10;
    s->spacing_x = 6;
    s->spacing_y = 6;
    s->border = 1;
    s->rounding = 5;
    s->row_height = 28;
    s->scrollbar = 12;
    s->title_height = 26;
    s->font = NULL; /* use the context default */
}

/* Neutral dark gray (VS-Code-ish): chrome is gray, the only saturated color is
   a restrained blue accent for checks/focus/selection. */
void zui_style_dark(UiStyle *out)
{
    common_metrics(out);
    out->colors[UI_COL_WINDOW_BG]    = ZUI_RGB(45, 45, 48);
    out->colors[UI_COL_PANEL]        = ZUI_RGB(37, 37, 38);
    out->colors[UI_COL_BORDER]       = ZUI_RGB(20, 20, 22);
    out->colors[UI_COL_TITLE]        = ZUI_RGB(60, 60, 64);
    out->colors[UI_COL_TITLE_ACTIVE] = ZUI_RGB(80, 80, 86);
    out->colors[UI_COL_TEXT]         = ZUI_RGB(222, 222, 226);
    out->colors[UI_COL_TEXT_DIM]     = ZUI_RGB(140, 140, 146);
    out->colors[UI_COL_BUTTON]       = ZUI_RGB(62, 62, 66);
    out->colors[UI_COL_BUTTON_HOVER] = ZUI_RGB(82, 82, 88);
    out->colors[UI_COL_BUTTON_ACTIVE]= ZUI_RGB(48, 48, 52);
    out->colors[UI_COL_ACCENT]       = ZUI_RGB(0, 122, 204);
    out->colors[UI_COL_TRACK]        = ZUI_RGB(55, 55, 58);
    out->colors[UI_COL_KNOB]         = ZUI_RGB(150, 150, 156);
    out->colors[UI_COL_INPUT_BG]     = ZUI_RGB(30, 30, 32);
    out->colors[UI_COL_SELECT]       = ZUI_RGB(0, 122, 204);
}

/* Neutral light gray: same structure as dark, lighter values. */
void zui_style_light(UiStyle *out)
{
    common_metrics(out);
    out->colors[UI_COL_WINDOW_BG]    = ZUI_RGB(240, 240, 240);
    out->colors[UI_COL_PANEL]        = ZUI_RGB(228, 228, 228);
    out->colors[UI_COL_BORDER]       = ZUI_RGB(170, 170, 172);
    out->colors[UI_COL_TITLE]        = ZUI_RGB(212, 212, 214);
    out->colors[UI_COL_TITLE_ACTIVE] = ZUI_RGB(196, 196, 200);
    out->colors[UI_COL_TEXT]         = ZUI_RGB(28, 28, 30);
    out->colors[UI_COL_TEXT_DIM]     = ZUI_RGB(110, 110, 114);
    out->colors[UI_COL_BUTTON]       = ZUI_RGB(225, 225, 225);
    out->colors[UI_COL_BUTTON_HOVER] = ZUI_RGB(236, 236, 236);
    out->colors[UI_COL_BUTTON_ACTIVE]= ZUI_RGB(208, 208, 210);
    out->colors[UI_COL_ACCENT]       = ZUI_RGB(0, 120, 215);
    out->colors[UI_COL_TRACK]        = ZUI_RGB(200, 200, 202);
    out->colors[UI_COL_KNOB]         = ZUI_RGB(120, 120, 126);
    out->colors[UI_COL_INPUT_BG]     = ZUI_RGB(255, 255, 255);
    out->colors[UI_COL_SELECT]       = ZUI_RGB(0, 120, 215);
}

/* Classic XP/Vista "Luna" look: beige panels, blue title bars and accents. */
void zui_style_classic(UiStyle *out)
{
    common_metrics(out);
    out->rounding = 3;
    out->colors[UI_COL_WINDOW_BG]    = ZUI_RGB(236, 233, 216);
    out->colors[UI_COL_PANEL]        = ZUI_RGB(236, 233, 216);
    out->colors[UI_COL_BORDER]       = ZUI_RGB(127, 157, 185);
    out->colors[UI_COL_TITLE]        = ZUI_RGB(89, 135, 214);
    out->colors[UI_COL_TITLE_ACTIVE] = ZUI_RGB(40, 100, 206);
    out->colors[UI_COL_TEXT]         = ZUI_RGB(0, 0, 0);
    out->colors[UI_COL_TEXT_DIM]     = ZUI_RGB(90, 90, 96);
    out->colors[UI_COL_BUTTON]       = ZUI_RGB(235, 233, 220);
    out->colors[UI_COL_BUTTON_HOVER] = ZUI_RGB(220, 235, 250);
    out->colors[UI_COL_BUTTON_ACTIVE]= ZUI_RGB(198, 215, 238);
    out->colors[UI_COL_ACCENT]       = ZUI_RGB(49, 106, 197);
    out->colors[UI_COL_TRACK]        = ZUI_RGB(214, 211, 196);
    out->colors[UI_COL_KNOB]         = ZUI_RGB(125, 165, 215);
    out->colors[UI_COL_INPUT_BG]     = ZUI_RGB(255, 255, 255);
    out->colors[UI_COL_SELECT]       = ZUI_RGB(49, 106, 197);
}

void zui_set_style(UiContext *ui, const UiStyle *s)
{
    ui->style = *s;
}

UiStyle *zui_style(UiContext *ui)
{
    return &ui->style;
}

void zui_push_color(UiContext *ui, UiColorId id, UiColor c)
{
    if (id < 0 || id >= UI_COL_COUNT || ui->color_sp >= UI_MAX_COLOR_STACK)
        return;
    ui->color_stack[ui->color_sp].id = id;
    ui->color_stack[ui->color_sp].prev = ui->style.colors[id];
    ui->color_sp++;
    ui->style.colors[id] = c;
}

void zui_pop_color(UiContext *ui, int count)
{
    while (count-- > 0 && ui->color_sp > 0)
    {
        ui->color_sp--;
        UiColorSave *s = &ui->color_stack[ui->color_sp];
        ui->style.colors[s->id] = s->prev;
    }
}
