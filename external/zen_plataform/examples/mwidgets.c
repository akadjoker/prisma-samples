#include "platform.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define COLOR(r, g, b) (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define COLORA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define FONT_W 5
#define FONT_H 7

static const uint8_t glyph_digits[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};

static const uint8_t glyph_upper[26][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}, {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};

static const uint8_t glyph_colon[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
static const uint8_t glyph_dot[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
static const uint8_t glyph_dash[7] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
static const uint8_t glyph_plus[7] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};

static bool pt_in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void draw_glyph(Framebuffer *fb, int x, int y, const uint8_t glyph[7], uint32_t color, int scale)
{
    for (int row = 0; row < FONT_H; ++row)
    {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; ++col)
        {
            if (bits & (1 << (FONT_W - 1 - col)))
            {
                draw_fill_rect(fb, x + col * scale, y + row * scale, scale, scale, color, BLEND_NONE);
            }
        }
    }
}

static void draw_char5x7(Framebuffer *fb, int x, int y, char c, uint32_t color, int scale)
{
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
    {
        draw_glyph(fb, x, y, glyph_upper[c - 'A'], color, scale);
        return;
    }
    if (c >= '0' && c <= '9')
    {
        draw_glyph(fb, x, y, glyph_digits[c - '0'], color, scale);
        return;
    }
    switch (c)
    {
    case ':':
        draw_glyph(fb, x, y, glyph_colon, color, scale);
        break;
    case '.':
        draw_glyph(fb, x, y, glyph_dot, color, scale);
        break;
    case '-':
        draw_glyph(fb, x, y, glyph_dash, color, scale);
        break;
    case '+':
        draw_glyph(fb, x, y, glyph_plus, color, scale);
        break;
    default:
        break;
    }
}

static void draw_text5x7(Framebuffer *fb, int x, int y, const char *text, uint32_t color, int scale)
{
    for (const char *p = text; *p; ++p, x += (FONT_W + 1) * scale)
        draw_char5x7(fb, x, y, *p, color, scale);
}

static void draw_label_i(Framebuffer *fb, int x, int y, const char *label, int value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %d", label, value);
    draw_text5x7(fb, x, y, buf, COLOR(220, 224, 232), 2);
}

typedef struct
{
    int mouse_x, mouse_y;
    int mouse_dx, mouse_dy;
    float wheel_x, wheel_y;
    bool mouse_down;
    bool mouse_pressed;
    bool mouse_released;
    int last_key;
    uint32_t last_char;
} UiInput;

typedef struct
{
    int hot;
    int active;
    int next_id;
    UiInput in;
} UiContext;

typedef struct
{
    int x, y, w, h;
    bool moving;
    int drag_off_x, drag_off_y;
    const char *title;
} DemoWindow;

typedef struct
{
    bool checked;
    bool hide_cursor;
    bool always_on_top;
    float value;
    float opacity;
    int clicks;
    int counter;
    int fps;
    double fps_timer;
    int fps_frames;
    DemoWindow tools;
    DemoWindow stats;
} DemoState;

static int ui_id(UiContext *ui)
{
    return ++ui->next_id;
}

static void ui_begin(UiContext *ui, PlatformWindow *w)
{
    /* Preserve active/hot across frames (carry widget state), but reset
       next_id so widget IDs stay stable frame-to-frame. */
    int saved_active = ui->active;
    int saved_hot    = ui->hot;
    memset(ui, 0, sizeof(*ui));
    ui->active = saved_active;
    ui->hot    = saved_hot;
    /* next_id stays 0 (from memset) – widgets get IDs 1,2,3... each frame */

    ui->in.mouse_x = mouse_x(w);
    ui->in.mouse_y = mouse_y(w);
    mouse_delta(w, &ui->in.mouse_dx, &ui->in.mouse_dy);
    mouse_wheel_v(w, &ui->in.wheel_x, &ui->in.wheel_y);
    ui->in.mouse_down = mouse_button_down(w, MOUSE_LEFT);
    ui->in.mouse_pressed = mouse_button_pressed(w, MOUSE_LEFT);
    ui->in.mouse_released = mouse_button_released(w, MOUSE_LEFT);
    ui->in.last_key = key_get_pressed(w);
    ui->in.last_char = char_get_pressed(w);
}

static bool ui_hit(UiContext *ui, int id, int x, int y, int w, int h)
{
    bool hovered = pt_in(ui->in.mouse_x, ui->in.mouse_y, x, y, w, h);
    if (hovered)
        ui->hot = id;
    if (hovered && ui->in.mouse_pressed)
        ui->active = id;
    return hovered;
}

static void ui_panel(Framebuffer *fb, int x, int y, int w, int h, uint32_t bg, uint32_t border)
{
    draw_fill_rect(fb, x, y, w, h, bg, BLEND_NONE);
    draw_rect(fb, x, y, w, h, border, BLEND_NONE);
}

static bool ui_button(UiContext *ui, Framebuffer *fb, int x, int y, int w, int h, const char *text)
{
    int id = ui_id(ui);
    bool hovered = ui_hit(ui, id, x, y, w, h);
    bool active = (ui->active == id);
    uint32_t bg = COLOR(56, 98, 162);
    if (active && ui->in.mouse_down)
        bg = COLOR(42, 76, 126);
    else if (hovered)
        bg = COLOR(70, 115, 188);
    draw_fill_rect(fb, x, y, w, h, bg, BLEND_NONE);
    draw_rect(fb, x, y, w, h, COLOR(10, 18, 28), BLEND_NONE);
    draw_text5x7(fb, x + 10, y + 8, text, COLOR(245, 248, 252), 2);
    if (ui->in.mouse_released && active)
    {
        bool clicked = hovered;
        ui->active = 0;
        return clicked;
    }
    return false;
}

static bool ui_checkbox(UiContext *ui, Framebuffer *fb, int x, int y, bool *value, const char *text)
{
    int id = ui_id(ui);
    bool hovered = ui_hit(ui, id, x, y, 22 + (int)strlen(text) * 12, 20);
    draw_fill_rect(fb, x, y, 18, 18, COLOR(42, 46, 56), BLEND_NONE);
    draw_rect(fb, x, y, 18, 18, hovered ? COLOR(110, 180, 255) : COLOR(120, 126, 140), BLEND_NONE);
    if (*value)
        draw_fill_rect(fb, x + 4, y + 4, 10, 10, COLOR(110, 220, 130), BLEND_NONE);
    draw_text5x7(fb, x + 26, y + 2, text, COLOR(220, 224, 232), 2);
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

static bool ui_slider(UiContext *ui, Framebuffer *fb, int x, int y, int w, float *value, float minv, float maxv, const char *label)
{
    int id = ui_id(ui);
    bool hovered = ui_hit(ui, id, x, y, w, 22);
    if (ui->active == id && ui->in.mouse_down)
    {
        float t = (float)(ui->in.mouse_x - x) / (float)(w - 12);
        t = clampf(t, 0.0f, 1.0f);
        *value = minv + (maxv - minv) * t;
    }
    draw_text5x7(fb, x, y - 18, label, COLOR(220, 224, 232), 2);
    draw_fill_rect(fb, x, y + 8, w, 6, COLOR(44, 48, 60), BLEND_NONE);
    draw_rect(fb, x, y + 8, w, 6, COLOR(22, 26, 34), BLEND_NONE);
    float t = (*value - minv) / (maxv - minv);
    t = clampf(t, 0.0f, 1.0f);
    int knob_x = x + (int)(t * (float)(w - 12));
    draw_fill_rect(fb, x, y + 8, knob_x - x + 6, 6, COLOR(72, 140, 235), BLEND_NONE);
    draw_fill_rect(fb, knob_x, y, 12, 22, hovered || ui->active == id ? COLOR(190, 220, 255) : COLOR(160, 176, 196), BLEND_NONE);
    draw_rect(fb, knob_x, y, 12, 22, COLOR(18, 24, 36), BLEND_NONE);
    bool changed = (ui->active == id && ui->in.mouse_down);
    if (ui->in.mouse_released && ui->active == id)
        ui->active = 0;
    return changed;
}

static void ui_begin_window(UiContext *ui, Framebuffer *fb, DemoWindow *win)
{
    int title_h = 26;
    int id = ui_id(ui);
    bool on_title = ui_hit(ui, id, win->x, win->y, win->w, title_h);
    if (on_title && ui->in.mouse_pressed)
    {
        win->moving = true;
        win->drag_off_x = ui->in.mouse_x - win->x;
        win->drag_off_y = ui->in.mouse_y - win->y;
        ui->active = id;
    }
    if (win->moving)
    {
        if (ui->in.mouse_down && ui->active == id)
        {
            win->x = ui->in.mouse_x - win->drag_off_x;
            win->y = ui->in.mouse_y - win->drag_off_y;
        }
        else
        {
            win->moving = false;
            if (ui->active == id)
                ui->active = 0;
        }
    }
    ui_panel(fb, win->x, win->y, win->w, win->h, COLOR(28, 31, 39), COLOR(8, 10, 14));
    draw_fill_rect(fb, win->x, win->y, win->w, title_h, on_title ? COLOR(64, 82, 118) : COLOR(48, 60, 84), BLEND_NONE);
    draw_rect(fb, win->x, win->y, win->w, title_h, COLOR(8, 10, 14), BLEND_NONE);
    draw_text5x7(fb, win->x + 8, win->y + 6, win->title, COLOR(244, 247, 252), 2);
}

static void draw_background(Framebuffer *fb)
{
    for (int y = 0; y < fb->height; ++y)
    {
        int c = 20 + (y * 18) / (fb->height > 1 ? fb->height - 1 : 1);
        draw_fill_rect(fb, 0, y, fb->width, 1, COLOR(c, c + 4, c + 10), BLEND_NONE);
    }
    for (int x = 0; x < fb->width; x += 32)
        draw_line(fb, x, 0, x, fb->height - 1, COLORA(255, 255, 255, 18), BLEND_ALPHA);
    for (int y = 0; y < fb->height; y += 32)
        draw_line(fb, 0, y, fb->width - 1, y, COLORA(255, 255, 255, 18), BLEND_ALPHA);
}

static void frame(PlatformWindow *w, void *user)
{
    DemoState *st = (DemoState *)user;
    /* window_begin_frame is already called by backend_run; calling it again
       would copy mouse_prev=mouse_down and destroy edge detection. */

    /* FPS counter – update every 0.5 s */
    st->fps_frames++;
    double now = time_seconds();
    if (now - st->fps_timer >= 0.5)
    {
        st->fps = (int)((double)st->fps_frames / (now - st->fps_timer) + 0.5);
        st->fps_timer = now;
        st->fps_frames = 0;
    }

    static UiContext ui;
    ui_begin(&ui, w);

    if (key_pressed(w, KEY_SPACE))
        st->counter++;
    if (key_pressed(w, KEY_C))
        clipboard_set("mwidgets clipboard test");
    if (key_pressed(w, KEY_V))
    {
        const char *clip = clipboard_get();
        if (clip && *clip)
            st->counter += (int)strlen(clip);
    }

    mouse_set_mode(w, st->hide_cursor ? MOUSE_MODE_HIDDEN : MOUSE_MODE_NORMAL);
    window_set_opacity(w, st->opacity);
    window_set_always_on_top(w, st->always_on_top);

    Framebuffer fb;
    if (!window_lock_pixels(w, &fb))
        return;

    draw_background(&fb);

    ui_panel(&fb, 16, 16, 300, 188, COLORA(12, 16, 24, 230), COLOR(40, 56, 84));
    draw_text5x7(&fb, 26, 26, "INPUT DEBUG", COLOR(245, 247, 252), 2);
    draw_label_i(&fb, 26, 54, "MOUSE X", ui.in.mouse_x);
    draw_label_i(&fb, 26, 74, "MOUSE Y", ui.in.mouse_y);
    draw_label_i(&fb, 26, 94, "DELTA X", ui.in.mouse_dx);
    draw_label_i(&fb, 26, 114, "DELTA Y", ui.in.mouse_dy);
    draw_label_i(&fb, 26, 134, "LAST KEY", ui.in.last_key);
    draw_label_i(&fb, 26, 154, "LAST CHAR", (int)ui.in.last_char);
    draw_label_i(&fb, 170, 54, "CLICKS", st->clicks);
    draw_label_i(&fb, 170, 74, "COUNT", st->counter);
    draw_label_i(&fb, 170, 94, "HOT", ui.hot);
    draw_label_i(&fb, 170, 114, "ACTIVE", ui.active);
    draw_label_i(&fb, 170, 134, "TOUCHES", touch_count(w));

    ui_begin_window(&ui, &fb, &st->tools);
    int x = st->tools.x + 12;
    int y = st->tools.y + 40;
    if (ui_button(&ui, &fb, x, y, 120, 32, "CLICK ME"))
        st->clicks++;
    y += 46;
    ui_checkbox(&ui, &fb, x, y, &st->checked, "ENABLE TEST");
    y += 30;
    ui_checkbox(&ui, &fb, x, y, &st->hide_cursor, "HIDE CURSOR");
    y += 30;
    ui_checkbox(&ui, &fb, x, y, &st->always_on_top, "ALWAYS ON TOP");
    y += 44;
    ui_slider(&ui, &fb, x, y, 180, &st->value, 0.0f, 100.0f, "SLIDER");
    y += 52;
    ui_slider(&ui, &fb, x, y, 180, &st->opacity, 0.35f, 1.0f, "OPACITY");
    y += 54;
    draw_label_i(&fb, x, y, "VALUE", (int)st->value);
    draw_label_i(&fb, x, y + 20, "CHECKED", st->checked ? 1 : 0);

    ui_begin_window(&ui, &fb, &st->stats);
    x = st->stats.x + 12;
    y = st->stats.y + 40;
    draw_text5x7(&fb, x, y, "WINDOW TEST", COLOR(245, 247, 252), 2);
    y += 28;
    draw_label_i(&fb, x, y, "FOCUSED", window_is_focused(w) ? 1 : 0);
    draw_label_i(&fb, x, y + 20, "VISIBLE", window_is_visible(w) ? 1 : 0);
    draw_label_i(&fb, x, y + 40, "HOVERED", window_is_hovered(w) ? 1 : 0);
    draw_label_i(&fb, x, y + 60, "MINIMIZED", window_is_minimized(w) ? 1 : 0);
    draw_label_i(&fb, x, y + 80, "MAXIMIZED", window_is_maximized(w) ? 1 : 0);
    draw_label_i(&fb, x, y + 100, "FPS", st->fps);
    draw_text5x7(&fb, x, y + 120, "SPACE ADDS COUNT", COLOR(220, 224, 232), 2);
    draw_text5x7(&fb, x, y + 140, "C COPY V PASTE LEN", COLOR(220, 224, 232), 2);

    int mx = ui.in.mouse_x;
    int my = ui.in.mouse_y;
    draw_line(&fb, mx - 10, my, mx + 10, my, COLOR(255, 255, 255), BLEND_NONE);
    draw_line(&fb, mx, my - 10, mx, my + 10, COLOR(255, 255, 255), BLEND_NONE);
    draw_fill_circle(&fb, mx, my, ui.in.mouse_down ? 4 : 3, ui.in.mouse_down ? COLOR(255, 120, 120) : COLOR(120, 210, 255), BLEND_NONE);

    window_present_pixels(w);
}

int main(void)
{
    if (!platform_init())
    {
        fprintf(stderr, "platform_init failed\n");
        return 1;
    }

    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.title = "mwidgets";
    cfg.width = 1280;
    cfg.height = 720;
    cfg.x = WINDOW_POS_CENTERED;
    cfg.y = WINDOW_POS_CENTERED;
    cfg.monitor = 1;
    cfg.mode = WINDOW_WINDOWED;
    cfg.render = RENDER_PIXELS;
    cfg.resizable = true;
    cfg.vsync = false;

    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        fprintf(stderr, "window_create failed\n");
        platform_shutdown();
        return 1;
    }

    DemoState st;
    memset(&st, 0, sizeof(st));
    st.checked = true;
    st.value = 32.0f;
    st.opacity = 1.0f;
    st.tools.x = 360;
    st.tools.y = 110;
    st.tools.w = 250;
    st.tools.h = 290;
    st.tools.title = "TOOLS";
    st.stats.x = 670;
    st.stats.y = 170;
    st.stats.w = 280;
    st.stats.h = 230;
    st.stats.title = "WINDOW";

    app_run(w, frame, &st);

    window_destroy(w);
    platform_shutdown();
    return 0;
}