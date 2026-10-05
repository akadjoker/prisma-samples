/*
 * demo_widgets_pixel.c - zen_ui widget showcase on a RENDER_PIXELS window.
 *
 * Exercises the toolkit's widgets (buttons, checkboxes, sliders, knob, progress,
 * text input, listbox, combo, tree, menus, context menu, tooltip, dialog and
 * draggable windows) and paints them with the bundled software renderer. No GL.
 *
 * Esc quits. The same UI is built by demo_widgets_gl.c through the GL renderer.
 */
#include "platform.h"
#include "zen_ui.h"
#include <stdio.h>
#include <string.h>

typedef struct
{
    UiContext *ui;

    int    theme;            /* 0 dark, 1 light, 2 classic */
    int    clicks;
    bool   grid, snap, wire;
    bool   m_bold, m_italic, m_wrap; /* checkable items inside the Editar menu */
    int    m_align;                  /* radio group inside the Editar menu */
    float  volume, zoom, pan, gain;
    char   path[128];
    char   name[64];

    int    list_sel;
    int    combo_sel;
    int    tool_sel;

    UiRect tools;
    UiRect props;
} Demo;

static void apply_theme(Demo *d)
{
    UiStyle s;
    if (d->theme == 0)      zui_style_dark(&s);
    else if (d->theme == 1) zui_style_light(&s);
    else                    zui_style_classic(&s);
    zui_set_style(d->ui, &s);
}

/* Builds the whole UI for one frame. Shared in spirit with the GL demo. */
static void build_ui(Demo *d, PlatformWindow *w)
{
    UiContext *ui = d->ui;
    const char *theme_names[3] = {"Tema: escuro", "Tema: claro", "Tema: classic"};
    const char *files[5] = {"main.c", "platform.h", "zen_ui.c", "README.md", "Makefile"};
    const char *modes[3] = {"Pequeno", "Medio", "Grande"};
    const char *tools[3] = {"Mover", "Rodar", "Escalar"};

    zui_begin(ui, w);

    /* Menu bar across the top. */
    zui_menubar_begin(ui);
    if (zui_menu_begin(ui, "Ficheiro", 160))
    {
        if (zui_menu_item(ui, "Abrir..."))
        {
            UiDialogConfig cfg = {ZUI_DIALOG_OPEN_FILE, "Abrir ficheiro", NULL, NULL};
            zui_dialog_open(ui, &cfg);
        }
        if (zui_menu_item(ui, "Guardar..."))
        {
            UiDialogConfig cfg = {ZUI_DIALOG_SAVE_FILE, "Guardar ficheiro", NULL, NULL};
            zui_dialog_open(ui, &cfg);
        }
        zui_menu_item(ui, "Sair");
        zui_menu_end(ui);
    }
    if (zui_menu_begin(ui, "Editar", 180))
    {
        zui_menu_item(ui, "Desfazer");
        zui_menu_item(ui, "Refazer");
        /* checkable + radio entries (proper menu items, not embedded widgets) */
        zui_menu_item_check(ui, "Negrito", &d->m_bold);
        zui_menu_item_check(ui, "Italico", &d->m_italic);
        zui_menu_item_check(ui, "Quebra linha", &d->m_wrap);
        zui_menu_item_radio(ui, "Alinhar esquerda", &d->m_align, 0);
        zui_menu_item_radio(ui, "Alinhar centro", &d->m_align, 1);
        zui_menu_item_radio(ui, "Alinhar direita", &d->m_align, 2);
        zui_menu_end(ui);
    }
    if (zui_menu_begin(ui, "Ajuda", 160))
    {
        zui_menu_item(ui, "Sobre");
        zui_menu_end(ui);
    }
    zui_menubar_end(ui);

    /* Header + theme cycler. */
    zui_row(ui, 28, 1);
    zui_label(ui, "zen_ui - PIXELS (software renderer)");

    zui_row(ui, 28, 3);
    if (zui_button(ui, "Botao A")) d->clicks++;
    if (zui_button(ui, "Botao B")) d->clicks += 2;
    if (zui_button(ui, theme_names[d->theme]))
    {
        d->theme = (d->theme + 1) % 3;
        apply_theme(d);
    }

    /* Tools window: input controls. */
    if (zui_window_begin(ui, "Controlos", &d->tools))
    {
        char buf[64];
        zui_row(ui, 22, 1);
        snprintf(buf, sizeof buf, "Cliques: %d", d->clicks);
        zui_label(ui, buf);

        zui_row(ui, 24, 2);
        zui_checkbox(ui, "Grelha", &d->grid);
        zui_checkbox(ui, "Snap", &d->snap);

        zui_row(ui, 24, 1);
        zui_label(ui, "Volume");
        zui_slider(ui, "##vol", &d->volume, 0.0f, 100.0f);
        zui_label(ui, "Zoom");
        zui_slider(ui, "##zoom", &d->zoom, 1.0f, 8.0f);

        zui_row(ui, 30, 1);
        zui_progress(ui, d->volume / 100.0f);

        zui_row_begin(ui, 26, 2);
        zui_row_push(ui, 0.32f);
        zui_label(ui, "Nome:");
        zui_row_push(ui, 0.68f);
        zui_text_input(ui, "##name", d->name, sizeof d->name);
        zui_row_end(ui);

        zui_row(ui, 110, 2);
        zui_knob(ui, "Pan", &d->pan, -100.0f, 100.0f);
        zui_knob(ui, "Gain", &d->gain, 0.0f, 100.0f);

        zui_window_end(ui);
    }

    /* Properties window: lists, combo, tree, toggle group. */
    if (zui_window_begin(ui, "Propriedades", &d->props))
    {
        zui_row(ui, 24, 1);
        zui_label(ui, "Ferramenta");
        zui_toggle_group(ui, tools, 3, &d->tool_sel);

        zui_row(ui, 24, 1);
        zui_label(ui, "Tamanho");
        zui_combo(ui, "##mode", modes, 3, &d->combo_sel);

        zui_row(ui, 110, 1);
        zui_listbox(ui, "##files", files, 5, &d->list_sel, 4);

        zui_row(ui, 20, 1);
        if (zui_tree_node(ui, "Avancado"))
        {
            zui_row(ui, 24, 1);
            zui_checkbox(ui, "Wireframe", &d->wire);
            zui_label(ui, "Gain");
            zui_slider(ui, "##gain2", &d->gain, 0.0f, 100.0f);
            zui_tree_pop(ui);
        }

        zui_window_end(ui);
    }

    /* Right-click anywhere for a context menu. */
    if (zui_context_menu_begin(ui, "ctx", 150))
    {
        if (zui_menu_item(ui, "Reset cliques")) d->clicks = 0;
        zui_menu_item(ui, "Duplicar");
        zui_menu_item(ui, "Apagar");
        zui_context_menu_end(ui);
    }

    /* Drive any open file dialog. */
    char chosen[256];
    if (zui_dialog(ui, chosen, sizeof chosen) == ZUI_DIALOG_OK)
        strncpy(d->path, chosen, sizeof d->path - 1);

    zui_end(ui);
}

static void frame(PlatformWindow *w, void *user)
{
    Demo *d = (Demo *)user;
    static const uint32_t bg[3] = {0xFF1E1E1E, 0xFFE6E6E6, 0xFF3A6EA5};

    Framebuffer fb;
    if (!window_lock_pixels(w, &fb))
        return;
    draw_clear(&fb, bg[d->theme]);

    build_ui(d, w);
    zui_render_software(&fb, zui_draw_list(d->ui));

    window_present_pixels(w);
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "zen_ui - demo widgets (PIXELS)";
    cfg.width = 960;
    cfg.height = 620;
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
    zui_set_font(d.ui, zui_font_default(14));
    d.volume = 65.0f;
    d.zoom = 2.0f;
    d.gain = 40.0f;
    d.list_sel = -1;
    d.combo_sel = 1;
    d.tools = zui_rect(30, 110, 330, 430);
    d.props = zui_rect(390, 130, 320, 340);
    strcpy(d.name, "untitled");
    strcpy(d.path, "(nenhum)");
    apply_theme(&d);

    key_set_exit(w, KEY_ESCAPE);
    app_run(w, frame, &d);

    zui_destroy(d.ui);
    window_destroy(w);
    platform_shutdown();
    return 0;
}
