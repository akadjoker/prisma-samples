/*
 * demo_dialog.c - the zen_ui modal file dialog: open / save / pick folder.
 *
 * Three buttons open each dialog kind; the chosen path is shown below. The dialog
 * is modal (dims and blocks the rest of the UI). ESC cancels it; close the window
 * to quit (ESC is intentionally NOT bound to exit so it can cancel the dialog).
 */
#include "platform.h"
#include "zen_ui.h"
#include <stdio.h>
#include <string.h>

typedef struct
{
    UiContext *ui;
    char       chosen[1024];
    int        theme;
} App;

static void open_dialog(App *a, UiDialogKind kind, const char *title, const char *filter)
{
    UiDialogConfig cfg = {kind, title, NULL, filter};
    zui_dialog_open(a->ui, &cfg);
}

static void frame(PlatformWindow *w, void *user)
{
    App *a = (App *)user;
    Framebuffer fb;
    if (!window_lock_pixels(w, &fb))
        return;
    draw_clear(&fb, 0xFF1E1E22u);

    UiContext *ui = a->ui;
    zui_begin(ui, w);

    zui_row(ui, 30, 1);
    zui_label(ui, "zen_ui - file dialog");

    zui_row(ui, 32, 3);
    if (zui_button(ui, "Abrir ficheiro"))
        open_dialog(a, ZUI_DIALOG_OPEN_FILE, "Abrir ficheiro", ".c;.h;.png;.bmp;.txt;.md");
    if (zui_button(ui, "Guardar como"))
        open_dialog(a, ZUI_DIALOG_SAVE_FILE, "Guardar ficheiro", NULL);
    if (zui_button(ui, "Escolher pasta"))
        open_dialog(a, ZUI_DIALOG_PICK_FOLDER, "Escolher pasta", NULL);

    zui_row(ui, 26, 1);
    zui_label(ui, "Escolhido:");
    zui_row(ui, 26, 1);
    zui_label(ui, a->chosen[0] ? a->chosen : "(nada)");

    /* Drive the dialog every frame; capture the result. */
    char path[1024];
    if (zui_dialog(ui, path, sizeof path) == ZUI_DIALOG_OK)
        snprintf(a->chosen, sizeof a->chosen, "%s", path);

    zui_end(ui);
    zui_render_software(&fb, zui_draw_list(ui));
    window_present_pixels(w);
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "zen_ui - demo dialog";
    cfg.width = 1000;
    cfg.height = 680;
    cfg.x = WINDOW_POS_CENTERED;
    cfg.y = WINDOW_POS_CENTERED;
    cfg.render = RENDER_PIXELS;
    cfg.resizable = true;

    PlatformWindow *w = window_create(&cfg);
    if (!w) { platform_shutdown(); return 1; }

    App a;
    memset(&a, 0, sizeof a);
    a.ui = zui_create();
    zui_set_font(a.ui, zui_font_default(14));

    /* Note: ESC is left unbound so it cancels the dialog; quit via the window. */
    app_run(w, frame, &a);

    zui_destroy(a.ui);
    window_destroy(w);
    platform_shutdown();
    return 0;
}
