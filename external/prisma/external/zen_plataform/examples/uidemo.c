/*
 * uidemo.c - zen_ui over a RENDER_PIXELS window: themed widgets, row layout,
 * draggable windows. The whole UI is described per frame; the software renderer
 * paints the sorted command list into the framebuffer.
 */
#include "platform.h"
#include "zen_ui.h"
#include <stdio.h>
#include <string.h>

typedef struct
{
    UiContext *ui;
    bool       check_a, check_b;
    float      volume, zoom;
    int        clicks;
    int        theme; /* 0 dark, 1 light, 2 classic */
    char       path[128];
    UiRect     tools;
    UiRect     props;
} Demo;

static void frame(PlatformWindow *w, void *user)
{
    Demo *d = (Demo *)user;

    static const uint32_t bg[3] = {0xFF1E1E1E, 0xFFE6E6E6, 0xFF3A6EA5};
    const char *names[3] = {"Tema: escuro", "Tema: claro", "Tema: classic"};

    Framebuffer fb;
    if (!window_lock_pixels(w, &fb))
        return;
    draw_clear(&fb, bg[d->theme]);

    zui_begin(d->ui, w);

    /* Root region (no window): a header row + a theme cycler. */
    zui_row(d->ui, 30, 1);
    zui_label(d->ui, "zen_ui - layout + tema + rounded");
    zui_row(d->ui, 28, 3);
    if (zui_button(d->ui, "Botao A"))
        d->clicks++;
    if (zui_button(d->ui, "Botao B"))
        d->clicks++;
    if (zui_button(d->ui, names[d->theme]))
    {
        d->theme = (d->theme + 1) % 3;
        UiStyle s;
        if (d->theme == 0)      zui_style_dark(&s);
        else if (d->theme == 1) zui_style_light(&s);
        else                    zui_style_classic(&s);
        zui_set_style(d->ui, &s);
    }

    /* Tools window: mixed row layouts. */
    if (zui_window_begin(d->ui, "Ferramentas", &d->tools))
    {
        zui_row(d->ui, 26, 1);
        zui_checkbox(d->ui, "Ativar grelha", &d->check_a);
        zui_checkbox(d->ui, "Snap", &d->check_b);
        zui_row(d->ui, 24, 1);
        zui_label(d->ui, "Volume");
        zui_slider(d->ui, "##vol", &d->volume, 0.0f, 100.0f);
        zui_label(d->ui, "Zoom");
        zui_slider(d->ui, "##zoom", &d->zoom, 1.0f, 8.0f);
        zui_row_begin(d->ui, 26, 2);
        zui_row_push(d->ui, 0.35f);
        zui_label(d->ui, "Path:");
        zui_row_push(d->ui, 0.65f);
        zui_text_input(d->ui, "##path", d->path, sizeof d->path);
        zui_row_end(d->ui);
        zui_window_end(d->ui);
    }

    /* Properties window: a red Run button via a scoped color override. */
    if (zui_window_begin(d->ui, "Propriedades", &d->props))
    {
        char buf[64];
        zui_row(d->ui, 22, 1);
        snprintf(buf, sizeof buf, "Cliques: %d", d->clicks);
        zui_label(d->ui, buf);
        snprintf(buf, sizeof buf, "Volume: %d   Zoom: %d", (int)d->volume, (int)d->zoom);
        zui_label(d->ui, buf);
        zui_row(d->ui, 30, 2);
        if (zui_button(d->ui, "Run"))
            d->clicks += 10;
        zui_button(d->ui, "Stop");
        zui_window_end(d->ui);
    }

    zui_end(d->ui);
    zui_render_software(&fb, zui_draw_list(d->ui));

    window_present_pixels(w);
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "zen_ui demo";
    cfg.width = 900;
    cfg.height = 560;
    cfg.x = WINDOW_POS_CENTERED;
    cfg.y = WINDOW_POS_CENTERED;
    cfg.render = RENDER_PIXELS;
    cfg.resizable = true;

    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        platform_shutdown();
        return 1;
    }

    Demo d;
    memset(&d, 0, sizeof d);
    d.ui = zui_create();
    d.volume = 65.0f;
    d.zoom = 2.0f;
    strcpy(d.path, "/home/user/projeto");
    d.tools = zui_rect(40, 120, 320, 280);
    d.props = zui_rect(400, 150, 300, 220);

    app_run(w, frame, &d);

    zui_destroy(d.ui);
    window_destroy(w);
    platform_shutdown();
    return 0;
}
