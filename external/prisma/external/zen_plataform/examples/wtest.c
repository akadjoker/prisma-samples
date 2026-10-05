/*
 * wtest.c - Interactive widget demo: two windows SIDE-BY-SIDE, PIXELS on the
 * left and GL on the right. Same widgets in both, driven by a single manual
 * event loop so both windows run simultaneously.
 *
 * Press ESC in either window to close it and exit.
 */
#include "platform.h"
#include "zen_ui.h"
#include "zen_2d.h"

#include <glad/gl.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct
{
    UiContext  *ui;
    const char *label;       /* "PIXELS" or "GL" */
    const char *input_text;  /* default text for the input field */
    bool        check_a, check_b;
    float       volume, pan;
    int         clicks;
    int         theme;       /* 0 dark, 1 light, 2 classic */
    char        input_buf[64];
    UiRect      win_bounds;
} WinState;

static void state_init(WinState *st, const char *label, const char *txt, int win_x, int win_y)
{
    st->check_a    = true;
    st->volume     = 75.0f;
    st->pan        = 50.0f;
    st->label      = label;
    st->input_text = txt;
    strcpy(st->input_buf, txt);
    st->ui = zui_create();
    zui_set_font(st->ui, zui_font_default(14));
    UiStyle s;
    zui_style_dark(&s);
    zui_set_style(st->ui, &s);
    st->win_bounds = zui_rect(win_x, win_y, 320, 380);
}

/* Paint the widget tree into the UiContext and return the sorted draw list.
   The caller feeds the list to zui_render_software (pixels) or zui_render_gl. */
static const UiDrawList *build_ui(WinState *st, PlatformWindow *w)
{
    UiContext *ui = st->ui;
    zui_begin(ui, w);

    const char *theme_names[3] = {"Tema Dark", "Tema Light", "Tema Classic"};

    /* Header row */
    {
        char title[80];
        snprintf(title, sizeof title, "zen_ui — %s Widget Demo", st->label);
        zui_row(ui, 30, 1);
        zui_label(ui, title);
    }

    /* Button row */
    zui_row(ui, 28, 4);
    if (zui_button(ui, "Bot A"))
        st->clicks++;
    if (zui_button(ui, "Bot B"))
        st->clicks += 2;
    if (zui_button(ui, "Reset"))
        st->clicks = 0;
    if (zui_button(ui, theme_names[st->theme]))
    {
        st->theme = (st->theme + 1) % 3;
        UiStyle s;
        if      (st->theme == 0) zui_style_dark(&s);
        else if (st->theme == 1) zui_style_light(&s);
        else                     zui_style_classic(&s);
        zui_set_style(ui, &s);
    }

    /* Draggable tools window */
    if (zui_window_begin(ui, "Ferramentas", &st->win_bounds))
    {
        char buf[64];
        zui_row(ui, 22, 1);
        snprintf(buf, sizeof buf, "Cliques: %d", st->clicks);
        zui_label(ui, buf);

        zui_row(ui, 26, 1);
        zui_checkbox(ui, "Opcao A", &st->check_a);
        zui_checkbox(ui, "Opcao B", &st->check_b);

        zui_row(ui, 24, 1);
        zui_label(ui, "Volume");
        zui_slider(ui, "##vol", &st->volume, 0.0f, 100.0f);

        zui_row(ui, 24, 1);
        zui_label(ui, "Pan");
        zui_slider(ui, "##pan2", &st->pan, -100.0f, 100.0f);

        zui_row(ui, 30, 1);
        zui_progress(ui, st->volume / 100.0f);

        zui_row(ui, 28, 1);
        zui_text_input(ui, "Texto", st->input_buf, (int)sizeof(st->input_buf));

        zui_row(ui, 120, 2);
        zui_knob(ui, "Knob", &st->pan, -100.0f, 100.0f);

        /* combo with a few items */
        static const char *items[] = {"Item 1", "Item 2", "Item 3"};
        static int combo_sel = 1;
        zui_combo(ui, "##combo", items, 3, &combo_sel);

        zui_window_end(ui);
    }

    zui_end(ui);
    return zui_draw_list(ui);
}

int main(void)
{
    if (!platform_init()) { fprintf(stderr, "init fail\n"); return 1; }

    /* ---------- create both windows ---------- */
    WindowConfig cfg_pix = {.title = "zen_ui PIXELS", .width = 720, .height = 580,
                            .x = 40,  .y = WINDOW_POS_CENTERED,
                            .render = RENDER_PIXELS, .resizable = true};
    PlatformWindow *wp = window_create(&cfg_pix);
    if (!wp) { platform_shutdown(); return 1; }

    WindowConfig cfg_gl = {.title = "zen_ui GL", .width = 720, .height = 580,
                           .x = 780, .y = WINDOW_POS_CENTERED,
                           .render = RENDER_GL, .gl = {.major = 3, .minor = 3},
                           .resizable = true, .vsync = true};
    PlatformWindow *wg = window_create(&cfg_gl);
    if (!wg) { window_destroy(wp); platform_shutdown(); return 1; }

    /* GL init — make_current is required before r2d_init */
    window_make_current(wg);
    if (!r2d_init()) { fprintf(stderr, "r2d_init fail\n"); return 1; }

    /* ---------- per-window demo state ---------- */
    WinState sp, sg;
    memset(&sp, 0, sizeof sp);
    memset(&sg, 0, sizeof sg);
    state_init(&sp, "PIXELS", "pixel input", 10, 50);
    state_init(&sg, "GL",     "opengl text", 10, 50);

    /* ---------- manual event loop (both concurrently) ---------- */
    while (!window_should_close(wp) && !window_should_close(wg))
    {
        /* ---- PIXELS frame ---- */
        window_begin_frame(wp);
        {
            Framebuffer fb;
            if (window_lock_pixels(wp, &fb))
            {
                draw_clear(&fb, 0xFF1E1E2Eu);
                const UiDrawList *dl = build_ui(&sp, wp);
                zui_render_software(&fb, dl);
            }
            window_present_pixels(wp);
        }

        /* ---- GL frame ---- */
        window_begin_frame(wg);
        {
            int fw, fh;
            window_get_framebuffer_size(wg, &fw, &fh);
            glViewport(0, 0, fw, fh);
            glClearColor(0.12f, 0.13f, 0.18f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            const UiDrawList *dl = build_ui(&sg, wg);
            zui_render_gl(dl, fw, fh);

            window_swap(wg);
        }

        /* ESC on either window quits */
        if (key_pressed(wp, KEY_ESCAPE) || key_pressed(wg, KEY_ESCAPE))
            break;
    }

    /* ---------- cleanup ---------- */
    zui_destroy(sp.ui);
    r2d_shutdown();
    window_destroy(wp);
    platform_shutdown();
    printf("clean exit\n");
    return 0;
}