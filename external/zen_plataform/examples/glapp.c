/*
 * glapp.c - interactive r2d batch stress test on a real GL window.
 *
 * Thousands of moving shapes drawn every frame through one batch, presented to
 * the screen (the loop swaps). Click and drag to spray more; the zen_ui panel
 * shows live FPS, shape count and draw calls. Esc quits.
 */
#include "platform.h"
#include "zen_2d.h"
#include "zen_ui.h"
#include <glad/gl.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_SHAPES 400000

typedef struct
{
    float    x, y, vx, vy, size, angle, spin;
    uint32_t color;
    uint8_t  type; /* 0 sprite, 1 rect, 2 circle, 3 triangle */
} Shape;

typedef struct
{
    UiContext *ui;
    R2dTexture tex;
    Shape     *shapes;
    int        count;
    uint32_t   rng;
    double     last_t, fps_t;
    int        fps_frames, fps;
    unsigned   shape_draws;
    UiRect     panel;
} App;

static uint32_t xr(App *a)
{
    a->rng ^= a->rng << 13;
    a->rng ^= a->rng >> 17;
    a->rng ^= a->rng << 5;
    return a->rng;
}
static float frnd(App *a, float lo, float hi)
{
    return lo + (hi - lo) * ((xr(a) & 0xFFFF) / 65535.0f);
}

static void add_shape(App *a, float x, float y)
{
    if (a->count >= MAX_SHAPES)
        return;
    Shape *s = &a->shapes[a->count++];
    s->x = x;
    s->y = y;
    s->vx = frnd(a, -120, 120);
    s->vy = frnd(a, -120, 120);
    s->size = frnd(a, 8, 22);
    s->angle = frnd(a, 0, 360);
    s->spin = frnd(a, -180, 180);
    s->type = xr(a) & 3;
    uint32_t r = 80 + (xr(a) % 176), g = 80 + (xr(a) % 176), b = 80 + (xr(a) % 176);
    s->color = 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void spray(App *a, float x, float y, int n)
{
    for (int i = 0; i < n; ++i)
        add_shape(a, x + frnd(a, -14, 14), y + frnd(a, -14, 14));
}

static void frame(PlatformWindow *w, void *user)
{
    App *a = (App *)user;
    int W, H;
    window_get_framebuffer_size(w, &W, &H);

    double now = time_seconds();
    float dt = (float)(now - a->last_t);
    a->last_t = now;
    if (dt > 0.05f)
        dt = 0.05f;

    a->fps_frames++;
    if (now - a->fps_t >= 0.4)
    {
        a->fps = (int)(a->fps_frames / (now - a->fps_t) + 0.5);
        a->fps_t = now;
        a->fps_frames = 0;
    }

    /* UI logic first, so we know button clicks and whether the mouse is on it. */
    bool do_clear = false;
    int  burst = 0;
    zui_begin(a->ui, w);
    UiRect panel = a->panel;
    if (zui_window_begin(a->ui, "r2d batch", &panel))
    {
        char buf[64];
        zui_row(a->ui, 22, 1);
        snprintf(buf, sizeof buf, "Shapes: %d", a->count);
        zui_label(a->ui, buf);
        snprintf(buf, sizeof buf, "FPS: %d", a->fps);
        zui_label(a->ui, buf);
        snprintf(buf, sizeof buf, "Draw calls: %u", a->shape_draws);
        zui_label(a->ui, buf);
        zui_row(a->ui, 26, 2);
        if (zui_button(a->ui, "+5000"))
            burst = 5000;
        if (zui_button(a->ui, "Clear"))
            do_clear = true;
        zui_row(a->ui, 18, 1);
        zui_label(a->ui, "Arrasta o rato p/ pintar");
        zui_window_end(a->ui);
    }
    a->panel = panel;
    zui_end(a->ui);

    if (do_clear)
        a->count = 0;
    if (burst)
        spray(a, W * 0.5f, H * 0.5f, burst);

    /* Mouse spray, unless the pointer is over the panel. */
    int mx = mouse_x(w), my = mouse_y(w);
    bool on_panel = mx >= a->panel.x && my >= a->panel.y &&
                    mx < a->panel.x + a->panel.w && my < a->panel.y + a->panel.h;
    if (mouse_button_down(w, MOUSE_LEFT) && !on_panel)
        spray(a, (float)mx, (float)my, 40);

    /* Update + draw all shapes in one batch. */
    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    r2d_begin(W, H);
    for (int i = 0; i < a->count; ++i)
    {
        Shape *s = &a->shapes[i];
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        s->angle += s->spin * dt;
        if (s->x < 0 || s->x > W) s->vx = -s->vx;
        if (s->y < 0 || s->y > H) s->vy = -s->vy;
        switch (s->type)
        {
        case 0:
            r2d_sprite(a->tex, 0, 0, 16, 16, s->x, s->y, 8, 8,
                       s->size / 16, s->size / 16, s->angle, 0, s->color);
            break;
        case 1:
            r2d_rect(s->x - s->size / 2, s->y - s->size / 2, s->size, s->size, s->color);
            break;
        case 2:
            r2d_circle(s->x, s->y, s->size * 0.5f, s->color);
            break;
        default:
            r2d_triangle(s->x, s->y - s->size * 0.6f,
                         s->x - s->size * 0.5f, s->y + s->size * 0.4f,
                         s->x + s->size * 0.5f, s->y + s->size * 0.4f, s->color);
            break;
        }
    }
    r2d_end();
    a->shape_draws = r2d_draw_calls();

    /* UI on top. */
    zui_render_gl(zui_draw_list(a->ui), W, H);
}

int main(void)
{
    if (!platform_init())
        return 1;
    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "r2d batch - clica para adicionar shapes";
    cfg.width = 1200;
    cfg.height = 720;
    cfg.x = WINDOW_POS_CENTERED;
    cfg.y = WINDOW_POS_CENTERED;
    cfg.render = RENDER_GL;
    cfg.gl.major = 3;
    cfg.gl.minor = 3;
    cfg.resizable = true;
    cfg.vsync = true;

    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        platform_shutdown();
        return 1;
    }
    window_make_current(w);
    if (!r2d_init())
    {
        fprintf(stderr, "r2d_init failed\n");
        return 1;
    }

    App a;
    memset(&a, 0, sizeof a);
    a.ui = zui_create();
    a.rng = 0x1234567u;
    a.shapes = malloc(sizeof(Shape) * MAX_SHAPES);
    a.last_t = a.fps_t = time_seconds();
    a.panel = zui_rect(16, 16, 230, 200);

    uint32_t px[16 * 16];
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
        {
            int c = ((x / 4) + (y / 4)) & 1;
            px[y * 16 + x] = c ? 0xFFEC6A5Au : 0xFFFFD166u;
        }
    a.tex = r2d_texture_create(px, 16, 16);
    spray(&a, 600, 360, 3000);

    key_set_exit(w, KEY_ESCAPE);
    app_run(w, frame, &a);

    free(a.shapes);
    zui_destroy(a.ui);
    r2d_shutdown();
    window_destroy(w);
    platform_shutdown();
    return 0;
}
