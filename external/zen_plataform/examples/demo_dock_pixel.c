/*
 * demo_dock_pixel.c - a fixed, Unity-style docked layout on a RENDER_PIXELS
 * window: a left "Hierarchy" panel, a right "Inspector" panel, and a center
 * panel with tabs (Scene / Game / Console). The two side panels are resizable
 * via draggable splitters. Nothing floats; panel rects are recomputed from the
 * window size every frame, so it follows window resizing.
 *
 * Shows: zui_group_begin (panels), zui_tabs (tab strip), zui_splitter
 * (resize handles), plus the usual widgets inside each panel. Esc quits.
 */
#include "platform.h"
#include "zen_ui.h"
#include <stdio.h>
#include <string.h>

typedef struct
{
    UiContext *ui;
    int   left_w, right_w; /* resizable panel widths */
    int   tab;             /* center tab: 0 Scene, 1 Game, 2 Console */
    int   sel;             /* selected object in the hierarchy */
    float pos[3], rot[3], scl[3]; /* inspector: transform */
    bool  visible, is_static, cast_shadows, snap;
    float opacity;
    int   material;
    char  name[48];
    int   log_n;
} Dock;

#define prop(ui, label) zui_prop_row((ui), (label), 0.40f)

static void log_line(Dock *d) { d->log_n++; }

static void panel_hierarchy(Dock *d, UiRect r)
{
    UiContext *ui = d->ui;
    if (!zui_group_begin(ui, "hierarchy", r, ZUI_GROUP_BORDER))
        return;
    static const char *objs[] = {
        "Main Camera", "Directional Light", "Player", "Ground",
        "Enemy", "Enemy (1)", "Pickup", "Canvas", "EventSystem" };

    zui_row(ui, 24, 1);
    zui_label(ui, "Hierarchy");
    zui_row(ui, r.h - 80, 1);
    zui_listbox(ui, "objs", objs, (int)(sizeof objs / sizeof objs[0]), &d->sel, 8);
    zui_row(ui, 26, 2);
    if (zui_button(ui, "+ Criar")) d->sel = 0;
    zui_button(ui, "Apagar");
    zui_group_end(ui);
}

static void panel_inspector(Dock *d, UiRect r)
{
    UiContext *ui = d->ui;
    static const char *materials[] = {"Default", "Metal", "Glass", "Wood"};
    if (!zui_group_begin(ui, "inspector", r, ZUI_GROUP_BORDER))
        return;

    /* object header: enable toggle + name */
    zui_row_begin(ui, 26, 2);
    zui_row_push(ui, 24); zui_checkbox(ui, "##en", &d->visible);
    zui_row_push(ui, 1.0f); zui_text_input(ui, "##name", d->name, sizeof d->name);
    zui_row_end(ui);

    zui_row(ui, 24, 2);
    zui_checkbox(ui, "Static", &d->is_static);
    zui_checkbox(ui, "Sombras", &d->cast_shadows);

    if (zui_collapsing_header(ui, "Transform"))
    {
        prop(ui, "Position"); zui_drag_float3(ui, "##pos", d->pos, 0.05f);
        prop(ui, "Rotation"); zui_drag_float3(ui, "##rot", d->rot, 0.5f);
        prop(ui, "Scale");    zui_drag_float3(ui, "##scl", d->scl, 0.02f);
    }

    if (zui_collapsing_header(ui, "Rendering"))
    {
        prop(ui, "Material"); zui_combo(ui, "##mat", materials, 4, &d->material);
        prop(ui, "Opacidade"); zui_slider(ui, "##op", &d->opacity, 0.0f, 1.0f);
        prop(ui, "Visivel"); zui_checkbox(ui, "##vis2", &d->visible);
    }

    if (zui_collapsing_header(ui, "Fisica"))
    {
        prop(ui, "Massa"); zui_drag_float(ui, "##mass", &d->scl[0], 0.01f, 0.0f, 100.0f);
        prop(ui, "Static"); zui_checkbox(ui, "##st2", &d->is_static);
    }
    zui_group_end(ui);
}

static void panel_center(Dock *d, UiRect r)
{
    UiContext *ui = d->ui;
    if (!zui_group_begin(ui, "center", r, ZUI_GROUP_BORDER))
        return;
    const char *tabs[] = {"Scene", "Game", "Console"};
    zui_tabs(ui, "tabs", tabs, 3, &d->tab);

    char buf[96];
    if (d->tab == 0)
    {
        zui_row(ui, 24, 1);
        snprintf(buf, sizeof buf, "Viewport da cena - objeto #%d selecionado", d->sel);
        zui_label(ui, buf);
        zui_row(ui, 24, 1);
        snprintf(buf, sizeof buf, "Pos (%.1f, %.1f, %.1f)  escala %.2f",
                 d->pos[0], d->pos[1], d->pos[2], d->scl[0]);
        zui_label(ui, buf);
        zui_row(ui, 28, 3);
        if (zui_button(ui, "Play"))  log_line(d);
        if (zui_button(ui, "Pause")) log_line(d);
        if (zui_button(ui, "Step"))  log_line(d);
    }
    else if (d->tab == 1)
    {
        zui_row(ui, 24, 1);
        zui_label(ui, "Game view (preview)");
        zui_row(ui, 24, 1);
        zui_progress(ui, 0.6f);
    }
    else
    {
        zui_row(ui, 24, 1);
        snprintf(buf, sizeof buf, "Console - %d mensagens", d->log_n);
        zui_label(ui, buf);
        for (int i = 0; i < d->log_n && i < 12; i++)
        {
            zui_row(ui, 18, 1);
            snprintf(buf, sizeof buf, "[info] evento %d disparado", i + 1);
            zui_label(ui, buf);
        }
        zui_row(ui, 26, 1);
        if (zui_button(ui, "Limpar")) d->log_n = 0;
    }
    zui_group_end(ui);
}

static void frame(PlatformWindow *w, void *user)
{
    Dock *d = (Dock *)user;
    Framebuffer fb;
    if (!window_lock_pixels(w, &fb))
        return;
    draw_clear(&fb, 0xFF1B1B1Bu);

    int W, H;
    window_get_framebuffer_size(w, &W, &H);

    zui_begin(d->ui, w);
    UiContext *ui = d->ui;
    int th = zui_style(ui)->title_height;

    /* top menu bar */
    zui_menubar_begin(ui);
    if (zui_menu_begin(ui, "File", 190))
    {
        zui_menu_item(ui, "New Scene");
        zui_menu_item(ui, "Open Scene");
        zui_menu_separator(ui);
        zui_menu_item(ui, "Save");
        zui_menu_item(ui, "Save As...");
        zui_menu_separator(ui);
        zui_menu_item(ui, "Exit");
        zui_menu_end(ui);
    }
    if (zui_menu_begin(ui, "Edit", 190))
    {
        zui_menu_item(ui, "Undo");
        zui_menu_item(ui, "Redo");
        zui_menu_separator(ui);
        zui_menu_item_check(ui, "Snap to grid", &d->snap);
        zui_menu_end(ui);
    }
    if (zui_menu_begin(ui, "Window", 190))
    {
        zui_menu_item_radio(ui, "Scene", &d->tab, 0);
        zui_menu_item_radio(ui, "Game", &d->tab, 1);
        zui_menu_item_radio(ui, "Console", &d->tab, 2);
        zui_menu_end(ui);
    }
    zui_menubar_end(ui);

    int top = th + 8;
    int status_h = zui_statusbar_height(ui);
    int ah = H - top - status_h;

    /* clamp panel widths to sane bounds for the current window size */
    if (d->left_w < 140) d->left_w = 140;
    if (d->right_w < 160) d->right_w = 160;
    if (d->left_w + d->right_w > W - 160)
        d->left_w = W - 160 - d->right_w > 140 ? W - 160 - d->right_w : d->left_w;

    UiRect left   = zui_rect(0, top, d->left_w, ah);
    UiRect right  = zui_rect(W - d->right_w, top, d->right_w, ah);
    UiRect center = zui_rect(d->left_w + 5, top, (W - d->right_w - 5) - (d->left_w + 5), ah);

    panel_hierarchy(d, left);
    panel_center(d, center);
    panel_inspector(d, right);

    /* splitters between panels (drag to resize); applied next frame */
    d->left_w  += zui_splitter(ui, "split_l", zui_rect(d->left_w, top, 5, ah), true);
    d->right_w -= zui_splitter(ui, "split_r", zui_rect(W - d->right_w - 5, top, 5, ah), true);

    /* bottom status bar */
    {
        char sb[128];
        const char *names[] = {"Scene", "Game", "Console"};
        snprintf(sb, sizeof sb, "Pronto   |   Objeto #%d   |   Vista: %s   |   %d msgs   |   snap %s",
                 d->sel, names[d->tab], d->log_n, d->snap ? "on" : "off");
        zui_statusbar_begin(ui);
        zui_row(ui, zui_statusbar_height(ui), 1);
        zui_label(ui, sb);
        zui_statusbar_end(ui);
    }

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
    cfg.title = "zen_ui - dock layout (Unity-style)";
    cfg.width = 1100;
    cfg.height = 680;
    cfg.x = WINDOW_POS_CENTERED;
    cfg.y = WINDOW_POS_CENTERED;
    cfg.render = RENDER_PIXELS;
    cfg.resizable = true;

    PlatformWindow *w = window_create(&cfg);
    if (!w) { platform_shutdown(); return 1; }

    Dock d;
    memset(&d, 0, sizeof d);
    d.ui = zui_create();
    zui_set_font(d.ui, zui_font_default(14));
    d.left_w = 220;
    d.right_w = 300;
    d.scl[0] = d.scl[1] = d.scl[2] = 1.0f;
    d.opacity = 1.0f;
    d.visible = true;
    d.cast_shadows = true;
    strcpy(d.name, "Main Camera");
    d.log_n = 3;

    key_set_exit(w, KEY_ESCAPE);
    app_run(w, frame, &d);

    zui_destroy(d.ui);
    window_destroy(w);
    platform_shutdown();
    return 0;
}
