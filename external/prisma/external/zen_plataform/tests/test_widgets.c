/*
 * test_widgets.c - comprehensive test of all zen_ui widgets in both pixel
 * (software) and OpenGL modes, validating interaction logic, draw-list
 * population, and software framebuffer output. Uses backend_fake so no
 * display or real GL context is required.
 */
#include "platform.h"
#include "backend_fake.h"
#include "zen_ui.h"
#include "zen_2d.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* Stub for the generated TTF default font data. The real data is embedded
 * only into the zen_ui library via cmake/embed_font.cmake; tests that
 * compile ui_font.c directly provide a dummy here and use zui_font_builtin
 * instead of zui_font_default to avoid touching the TTF path at runtime. */
const unsigned char ui_font_default_data[1] = {0};
const unsigned int  ui_font_default_size = 0;

/* Default theme metrics (from ui_theme.c common_metrics):
 *   pad_x = 10, pad_y = 10, row_height = 28, spacing_y = 6
 * The first widget row starts at y = pad_y = 10 in the root container.
 * Cells have h = row_height = 28 by default.
 * So the first cell is roughly at (10, 10, width/cols, 28).
 */
static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
        {                                                          \
            g_pass++;                                              \
        }                                                          \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

/* ------------------------------------------------------------------ helpers -- */

static PlatformWindow *make_window(RenderMode render)
{
    WindowConfig cfg = {.title = "test", .width = 800, .height = 600,
                        .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED,
                        .render = render};
    return window_create(&cfg);
}

/* Drive one full frame: begin, call the widgets function (which sets up its own
 * layout via zui_row etc.), end, return the draw list. */
static const UiDrawList *run_frame(PlatformWindow *w, UiContext *ui,
                                   void (*widgets)(UiContext *))
{
    window_begin_frame(w);
    zui_begin(ui, w);
    widgets(ui);
    zui_end(ui);
    return zui_draw_list(ui);
}

/* ------------------------------------------------------------------ layout checks -- */

static void widgets_label_row(UiContext *ui)
{
    zui_row(ui, 0, 3);
    zui_label(ui, "Label A");
    zui_label(ui, "Label B");
    zui_label(ui, "Label C");
}

static void widgets_label_row_static(UiContext *ui)
{
    zui_row_static(ui, 30, 100, 2);
    zui_label(ui, "A");
    zui_label(ui, "B");
    zui_label(ui, "C");
}

static void widgets_label_row_custom(UiContext *ui)
{
    zui_row_begin(ui, 26, 3);
    zui_row_push(ui, 0.5f);
    zui_row_push(ui, 0.3f);
    zui_row_push(ui, 0.2f);
    zui_row_end(ui);
    zui_label(ui, "A");
    zui_label(ui, "B");
    zui_label(ui, "C");
}

static void test_layout_rows(PlatformWindow *w, UiContext *ui)
{
    const UiDrawList *dl = run_frame(w, ui, widgets_label_row);
    int text_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_TEXT)
            text_count++;
    CHECK(text_count >= 3);

    dl = run_frame(w, ui, widgets_label_row_static);
    text_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_TEXT)
            text_count++;
    CHECK(text_count >= 3);

    dl = run_frame(w, ui, widgets_label_row_custom);
    text_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_TEXT)
            text_count++;
    CHECK(text_count >= 3);
}

/* ------------------------------------------------------------------ button -- */

static bool g_button_clicked;
static void widgets_button(UiContext *ui)
{
    zui_row(ui, 30, 3);
    g_button_clicked = zui_button(ui, "Click Me");
}

static void test_button_interaction(PlatformWindow *w, UiContext *ui)
{
    /* First cell of row is at roughly x=10..(800-20)/3+10 */
    /* hover only - y=20 is inside first row (pad_y=10, h=30) */
    fake_mouse_move(w, 60, 20);
    run_frame(w, ui, widgets_button);
    CHECK(!g_button_clicked);

    /* press */
    fake_mouse_button(w, MOUSE_LEFT, true);
    fake_mouse_move(w, 60, 20);
    run_frame(w, ui, widgets_button);
    CHECK(!g_button_clicked);

    /* release -> click */
    fake_mouse_button(w, MOUSE_LEFT, false);
    fake_mouse_move(w, 60, 20);
    const UiDrawList *dl = run_frame(w, ui, widgets_button);
    CHECK(g_button_clicked);

    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 1);
}

/* ------------------------------------------------------------------ checkbox -- */

static bool g_check_checked;
static bool g_check_changed;
static void widgets_checkbox(UiContext *ui)
{
    zui_row(ui, 30, 3);
    g_check_changed = zui_checkbox(ui, "Option", &g_check_checked);
}

static void test_checkbox_toggle(PlatformWindow *w, UiContext *ui)
{
    g_check_checked = false;

    /* press in first cell */
    fake_mouse_move(w, 60, 22);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_checkbox);

    /* release */
    fake_mouse_button(w, MOUSE_LEFT, false);
    fake_mouse_move(w, 60, 22);
    run_frame(w, ui, widgets_checkbox);
    CHECK(g_check_checked);
    CHECK(g_check_changed);

    /* toggle off */
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_checkbox);
    fake_mouse_button(w, MOUSE_LEFT, false);
    fake_mouse_move(w, 60, 22);
    const UiDrawList *dl = run_frame(w, ui, widgets_checkbox);
    CHECK(!g_check_checked);

    int text_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_TEXT)
            text_count++;
    CHECK(text_count >= 1);
}

/* ------------------------------------------------------------------ slider -- */

static float g_slider_val;
static bool  g_slider_changed;
static void widgets_slider(UiContext *ui)
{
    zui_row(ui, 40, 2);
    g_slider_changed = zui_slider(ui, "Slider", &g_slider_val, 0.0f, 100.0f);
}

static void test_slider_drag(PlatformWindow *w, UiContext *ui)
{
    g_slider_val = 30.0f;

    /* press in the slider cell (first col, x ~ 10..(800-20)/2+10 = 10..400) */
    fake_mouse_move(w, 100, 27);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_slider);

    /* drag right */
    fake_mouse_move(w, 300, 27);
    run_frame(w, ui, widgets_slider);
    CHECK(g_slider_val > 30.0f);

    /* release */
    fake_mouse_button(w, MOUSE_LEFT, false);
    const UiDrawList *dl = run_frame(w, ui, widgets_slider);

    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 3);
}

/* ------------------------------------------------------------------ slider_v -- */

static float g_slider_v_val;
static bool  g_slider_v_changed;
static void widgets_slider_v(UiContext *ui)
{
    zui_row(ui, 200, 3);
    g_slider_v_changed = zui_slider_v(ui, "VSlider", &g_slider_v_val, 0.0f, 100.0f);
}

static void test_slider_v_drag(PlatformWindow *w, UiContext *ui)
{
    g_slider_v_val = 50.0f;

    fake_mouse_move(w, 100, 100);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_slider_v);

    /* drag down -> value decreases (top=max, bottom=min) */
    fake_mouse_move(w, 100, 160);
    run_frame(w, ui, widgets_slider_v);
    CHECK(g_slider_v_val < 50.0f);

    fake_mouse_button(w, MOUSE_LEFT, false);
    const UiDrawList *dl = run_frame(w, ui, widgets_slider_v);

    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 3);
}

/* ------------------------------------------------------------------ knob -- */

static float g_knob_val;
static bool  g_knob_changed;
static void widgets_knob(UiContext *ui)
{
    zui_row(ui, 120, 3);
    g_knob_changed = zui_knob(ui, "Knob", &g_knob_val, 0.0f, 100.0f);
}

static void test_knob_drag(PlatformWindow *w, UiContext *ui)
{
    g_knob_val = 30.0f;

    fake_mouse_move(w, 200, 70);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_knob);

    /* drag up */
    fake_mouse_move(w, 200, 30);
    run_frame(w, ui, widgets_knob);
    CHECK(g_knob_changed);

    fake_mouse_button(w, MOUSE_LEFT, false);
    const UiDrawList *dl = run_frame(w, ui, widgets_knob);

    /* body and ring are rects, the pointer is a line */
    int rect_count = 0, line_count = 0;
    for (int i = 0; i < dl->count; i++)
    {
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
        else if (dl->cmds[i].type == UI_CMD_LINE)
            line_count++;
    }
    CHECK(rect_count >= 2);
    CHECK(line_count >= 1);
}

/* ------------------------------------------------------------------ progress -- */

static void widgets_progress(UiContext *ui)
{
    zui_row(ui, 20, 1);
    zui_progress(ui, 0.6f);
}

static void test_progress_bar(PlatformWindow *w, UiContext *ui)
{
    const UiDrawList *dl = run_frame(w, ui, widgets_progress);
    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 2);
}

/* ------------------------------------------------------------------ selectable -- */

static bool g_select_clicked;
static void widgets_selectable(UiContext *ui)
{
    zui_row(ui, 30, 3);
    g_select_clicked = zui_selectable(ui, "Item", false);
}

static void test_selectable_click(PlatformWindow *w, UiContext *ui)
{
    g_select_clicked = false;

    fake_mouse_move(w, 60, 22);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_selectable);

    fake_mouse_button(w, MOUSE_LEFT, false);
    fake_mouse_move(w, 60, 22);
    const UiDrawList *dl = run_frame(w, ui, widgets_selectable);
    CHECK(g_select_clicked);

    int text_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_TEXT)
            text_count++;
    CHECK(text_count >= 1);
}

/* ------------------------------------------------------------------ listbox -- */

static int  g_list_selected;
static int  g_list_changed;
static const char *g_list_items[] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon"};
static void widgets_listbox(UiContext *ui)
{
    zui_row(ui, 120, 2);
    g_list_changed = zui_listbox(ui, "mylist", g_list_items, 5, &g_list_selected, 3);
}

static void test_listbox_selection(PlatformWindow *w, UiContext *ui)
{
    g_list_selected = 0;
    g_list_changed = -1;

    /* click the second row (y ~ 10 + 28 = 38) */
    fake_mouse_move(w, 200, 40);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_listbox);

    fake_mouse_button(w, MOUSE_LEFT, false);
    fake_mouse_move(w, 200, 40);
    const UiDrawList *dl = run_frame(w, ui, widgets_listbox);

    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 1);
}

/* ------------------------------------------------------------------ combo -- */

static int  g_combo_selected;
static bool g_combo_changed;
static const char *g_combo_items[] = {"Option 1", "Option 2", "Option 3"};
static void widgets_combo(UiContext *ui)
{
    zui_row(ui, 30, 3);
    g_combo_changed = zui_combo(ui, "mycombo", g_combo_items, 3, &g_combo_selected);
}

static void test_combo_open(PlatformWindow *w, UiContext *ui)
{
    g_combo_selected = 0;

    /* click combo button in first cell */
    fake_mouse_move(w, 80, 22);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_combo);

    fake_mouse_button(w, MOUSE_LEFT, false);
    fake_mouse_move(w, 80, 22);
    const UiDrawList *dl = run_frame(w, ui, widgets_combo);

    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 2);
}

/* ------------------------------------------------------------------ toggle group -- */

static int  g_toggle_changed;
static const char *g_toggle_labels[] = {"Tab A", "Tab B", "Tab C"};
static void widgets_toggle_group(UiContext *ui)
{
    zui_row(ui, 30, 3);
    g_toggle_changed = zui_toggle_group(ui, g_toggle_labels, 3, &g_toggle_changed);
}

static void test_toggle_group_switch(PlatformWindow *w, UiContext *ui)
{
    g_toggle_changed = 0;

    /* click second toggle (2nd cell: x~260..520, y~10..40) */
    fake_mouse_move(w, 400, 25);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_toggle_group);

    fake_mouse_button(w, MOUSE_LEFT, false);
    fake_mouse_move(w, 400, 25);
    const UiDrawList *dl = run_frame(w, ui, widgets_toggle_group);

    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 3);
}

/* ------------------------------------------------------------------ icon -- */

static void widgets_icon(UiContext *ui)
{
    zui_icon(ui, zui_rect(10, 10, 20, 20), UI_ICON_CHECK, 0xFFFFFFFFu);
}

static void test_icon_command(PlatformWindow *w, UiContext *ui)
{
    const UiDrawList *dl = run_frame(w, ui, widgets_icon);
    int icon_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_ICON)
            icon_count++;
    CHECK(icon_count >= 1);
}

/* ------------------------------------------------------------------ tooltip -- */

static void widgets_tooltip(UiContext *ui)
{
    zui_tooltip(ui, "Tooltip text");
}

static void test_tooltip_draw(PlatformWindow *w, UiContext *ui)
{
    fake_mouse_move(w, 200, 200);
    const UiDrawList *dl = run_frame(w, ui, widgets_tooltip);
    int rect_count = 0, text_count = 0;
    for (int i = 0; i < dl->count; i++)
    {
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
        if (dl->cmds[i].type == UI_CMD_TEXT)
            text_count++;
    }
    CHECK(rect_count >= 2);
    CHECK(text_count >= 1);
}

/* ------------------------------------------------------------------ scrollbar -- */

static float g_scroll_offset;
static void widgets_scrollbar(UiContext *ui)
{
    zui_scrollbar(ui, zui_rect(780, 10, 12, 580), &g_scroll_offset, 1000.0f, 500.0f, true);
}

static void test_scrollbar_draw(PlatformWindow *w, UiContext *ui)
{
    g_scroll_offset = 100.0f;
    const UiDrawList *dl = run_frame(w, ui, widgets_scrollbar);
    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 2);
}

/* ------------------------------------------------------------------ window -- */

static bool g_window_open;
static void widgets_window(UiContext *ui)
{
    g_window_open = false;
    UiRect wb = zui_rect(100, 80, 300, 200);
    if (zui_window_begin(ui, "TestWin", &wb))
    {
        g_window_open = true;
        zui_label(ui, "Content");
        zui_window_end(ui);
    }
}

static void test_window_creation(PlatformWindow *w, UiContext *ui)
{
    const UiDrawList *dl = run_frame(w, ui, widgets_window);
    CHECK(g_window_open);

    int rect_count = 0, text_count = 0;
    for (int i = 0; i < dl->count; i++)
    {
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
        if (dl->cmds[i].type == UI_CMD_TEXT)
            text_count++;
    }
    CHECK(rect_count >= 3);
    CHECK(text_count >= 2);

    int max_layer = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].layer > max_layer)
            max_layer = dl->cmds[i].layer;
    CHECK(max_layer > 0);
}

/* ------------------------------------------------------------------ tree node -- */

static bool g_tree_expanded;
static void widgets_tree(UiContext *ui)
{
    zui_row(ui, 26, 1);
    g_tree_expanded = zui_tree_node(ui, "Node");
    if (g_tree_expanded)
    {
        zui_label(ui, "Child 1");
        zui_label(ui, "Child 2");
        zui_tree_pop(ui);
    }
}

static void test_tree_node_expand(PlatformWindow *w, UiContext *ui)
{
    run_frame(w, ui, widgets_tree);
    CHECK(!g_tree_expanded);

    /* click to expand (first cell covers most of the width) */
    fake_mouse_move(w, 60, 20);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_tree);

    fake_mouse_button(w, MOUSE_LEFT, false);
    fake_mouse_move(w, 60, 20);
    const UiDrawList *dl = run_frame(w, ui, widgets_tree);

    int icon_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_ICON)
            icon_count++;
    CHECK(icon_count >= 1);
}

/* ------------------------------------------------------------------ menu -- */

static bool g_menu_inside;
static void widgets_menu(UiContext *ui)
{
    g_menu_inside = false;
    zui_row(ui, 26, 6);
    if (zui_menu_begin(ui, "File", 120))
    {
        g_menu_inside = true;
        zui_menu_item(ui, "New");
        zui_menu_item(ui, "Open");
        zui_menu_item(ui, "Save");
        zui_menu_end(ui);
    }
}

static void test_menu_open(PlatformWindow *w, UiContext *ui)
{
    /* click first menu label (first cell, x~10..135) */
    fake_mouse_move(w, 50, 20);
    fake_mouse_button(w, MOUSE_LEFT, true);
    run_frame(w, ui, widgets_menu);

    fake_mouse_button(w, MOUSE_LEFT, false);
    fake_mouse_move(w, 50, 20);
    const UiDrawList *dl = run_frame(w, ui, widgets_menu);

    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 2);
}

/* ------------------------------------------------------------------ context menu -- */

static bool g_ctx_open;
static void widgets_context_menu(UiContext *ui)
{
    g_ctx_open = false;
    if (zui_context_menu_begin(ui, "ctxmenu", 150))
    {
        g_ctx_open = true;
        zui_menu_item(ui, "Cut");
        zui_menu_item(ui, "Copy");
        zui_menu_item(ui, "Paste");
        zui_context_menu_end(ui);
    }
}

static void test_context_menu_state(PlatformWindow *w, UiContext *ui)
{
    const UiDrawList *dl = run_frame(w, ui, widgets_context_menu);
    CHECK(!g_ctx_open);
    (void)dl;
}

/* ------------------------------------------------------------------ menubar -- */

static void widgets_menubar(UiContext *ui)
{
    zui_menubar_begin(ui);
    zui_label(ui, "File");
    zui_label(ui, "Edit");
    zui_label(ui, "Help");
    zui_menubar_end(ui);
}

static void test_menubar_creation(PlatformWindow *w, UiContext *ui)
{
    const UiDrawList *dl = run_frame(w, ui, widgets_menubar);
    int rect_count = 0, text_count = 0;
    for (int i = 0; i < dl->count; i++)
    {
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
        if (dl->cmds[i].type == UI_CMD_TEXT)
            text_count++;
    }
    CHECK(rect_count >= 1);
    CHECK(text_count >= 3);
}

/* ------------------------------------------------------------------ group / scroll region -- */

static bool g_group_inside;
static void widgets_group(UiContext *ui)
{
    g_group_inside = zui_group_begin(ui, "scrollgroup",
                                     zui_rect(20, 20, 200, 150), ZUI_GROUP_BORDER);
    if (g_group_inside)
    {
        zui_label(ui, "Row 1");
        zui_label(ui, "Row 2");
        zui_group_end(ui);
    }
}

static void test_scroll_group(PlatformWindow *w, UiContext *ui)
{
    const UiDrawList *dl = run_frame(w, ui, widgets_group);
    CHECK(g_group_inside);
    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 1);
}

/* ------------------------------------------------------------------ text input -- */

static char g_text_buf[64];
static void widgets_text_input(UiContext *ui)
{
    zui_row(ui, 30, 2);
    zui_text_input(ui, "Name", g_text_buf, (int)sizeof(g_text_buf));
}

static void test_text_input_draw(PlatformWindow *w, UiContext *ui)
{
    strcpy(g_text_buf, "Hello");
    const UiDrawList *dl = run_frame(w, ui, widgets_text_input);
    int rect_count = 0;
    for (int i = 0; i < dl->count; i++)
        if (dl->cmds[i].type == UI_CMD_RECT)
            rect_count++;
    CHECK(rect_count >= 2);
}

/* ------------------------------------------------------------------ theme / style -- */

static void test_theme_presets(PlatformWindow *w, UiContext *ui)
{
    UiStyle dark, light, classic;
    zui_style_dark(&dark);
    zui_style_light(&light);
    zui_style_classic(&classic);

    for (int i = 0; i < UI_COL_COUNT; i++)
    {
        CHECK(dark.colors[i] != 0);
        CHECK(light.colors[i] != 0);
        CHECK(classic.colors[i] != 0);
    }
    CHECK(dark.row_height > 0);
    CHECK(light.row_height > 0);
    CHECK(classic.row_height > 0);

    /* style swap inside frames */
    zui_set_style(ui, &dark);
    run_frame(w, ui, widgets_label_row);
    zui_set_style(ui, &light);
    run_frame(w, ui, widgets_label_row);
}

/* ------------------------------------------------------------------ push/pop color -- */

static void test_push_pop_color(PlatformWindow *w, UiContext *ui)
{
    UiStyle *sty = zui_style(ui);
    UiColor old_acc = sty->colors[UI_COL_ACCENT];

    zui_push_color(ui, UI_COL_ACCENT, 0xAABBCCDDu);
    CHECK(sty->colors[UI_COL_ACCENT] == 0xAABBCCDDu);

    zui_pop_color(ui, 1);
    CHECK(sty->colors[UI_COL_ACCENT] == old_acc);

    zui_push_color(ui, UI_COL_ACCENT, 0x11111111u);
    zui_push_color(ui, UI_COL_BORDER, 0x22222222u);
    CHECK(sty->colors[UI_COL_ACCENT] == 0x11111111u);
    CHECK(sty->colors[UI_COL_BORDER] == 0x22222222u);

    zui_pop_color(ui, 2);
    CHECK(sty->colors[UI_COL_ACCENT] == old_acc);
}

/* ------------------------------------------------------------------ state store -- */

static void test_state_store(PlatformWindow *w, UiContext *ui)
{
    ZuiId id = zui_id(ui, "test_state");

    int *iv = zui_state_int(ui, id, 42);
    CHECK(*iv == 42);
    *iv = 99;
    int *iv2 = zui_state_int(ui, id, 42);
    CHECK(*iv2 == 99);
    CHECK(iv == iv2);

    float *fv = zui_state_float(ui, id + 1, 3.14f);
    CHECK(*fv == 3.14f);
    *fv = 2.718f;
    float *fv2 = zui_state_float(ui, id + 1, 3.14f);
    CHECK(*fv2 == 2.718f);

    /* zui_state_ptr returns pointer-to-slot; dereference to get the
       stored void* value since the fix in ui_state.c. */
    void **ppv = (void **)zui_state_ptr(ui, id + 2);
    CHECK(*ppv == NULL);
    *ppv = (void *)0xDEADu;
    ppv = (void **)zui_state_ptr(ui, id + 2);
    CHECK(*ppv == (void *)0xDEADu);
}

/* ------------------------------------------------------------------ push_id scope -- */

static void test_id_push_pop(PlatformWindow *w, UiContext *ui)
{
    ZuiId base = zui_id(ui, "parent");
    zui_push_id(ui, "child");
    ZuiId child = zui_id(ui, "item");
    zui_pop_id(ui);
    ZuiId base2 = zui_id(ui, "item");
    CHECK(child != base2);

    zui_push_id_i(ui, 5);
    ZuiId looped = zui_id(ui, "iter");
    CHECK(looped != child);
    CHECK(looped != base2);
    zui_pop_id(ui);
}

/* ------------------------------------------------------------------ font -- */

static void test_font_creation(void)
{
    /* Use a different scale than the context font (scale 2) so freeing
       here doesn't invalidate the context's font pointer. */
    UiFont *f = zui_font_builtin(3);
    CHECK(f != NULL);
    int h = zui_font_height(f);
    CHECK(h > 0);
    int w = zui_text_width(f, "Hello", -1);
    CHECK(w > 0);
    int asc = zui_font_ascent(f);
    CHECK(asc > 0);

    UiGlyphInfo gi;
    bool ok = zui_font_glyph(f, 'A', &gi);
    CHECK(ok);
    CHECK(gi.w > 0 && gi.h > 0);
    CHECK(gi.advance > 0);

    zui_font_free(f);
}

/* ------------------------------------------------------------------ id label rules -- */

static void test_id_label_rules(PlatformWindow *w, UiContext *ui)
{
    ZuiId a = zui_id(ui, "Save");
    CHECK(a != 0);

    ZuiId b = zui_id(ui, "Save##a");
    CHECK(b != 0);
    CHECK(b != a);

    ZuiId fixed1 = zui_id(ui, "###fixed");
    ZuiId fixed2 = zui_id(ui, "Hello World##fixed");
    CHECK(fixed1 == fixed2);
}

/* ------------------------------------------------------------------ software renderer output -- */

static void widgets_render_test(UiContext *ui)
{
    zui_row(ui, 30, 4);
    zui_label(ui, "Label");
    zui_button(ui, "Btn");
    bool ck = false;
    zui_checkbox(ui, "Chk", &ck);
    float fv = 50.0f;
    zui_slider(ui, "Slide", &fv, 0.0f, 100.0f);
}

static void test_software_renderer_output(PlatformWindow *w, UiContext *ui)
{
    const UiDrawList *dl = run_frame(w, ui, widgets_render_test);
    CHECK(dl->count > 0);

    Framebuffer fb;
    if (window_lock_pixels(w, &fb))
    {
        zui_render_software(&fb, dl);

        int total = fb.width * fb.height;
        int non_zero = 0;
        for (int i = 0; i < total; i++)
            if (fb.pixels[i] != 0)
                non_zero++;
        CHECK(non_zero > 100);

        window_present_pixels(w);
    }
    else
    {
        CHECK(false);
    }
}

/* ------------------------------------------------------------------ draw list populated -- */

static void widgets_many(UiContext *ui)
{
    zui_row(ui, 27, 5);
    zui_label(ui, "Row 1");
    zui_label(ui, "Row 2");
    zui_button(ui, "Btn 1");
    zui_button(ui, "Btn 2");

    bool checked = true;
    zui_checkbox(ui, "Opt 1", &checked);
    zui_checkbox(ui, "Opt 2", &checked);

    float v = 42.0f;
    zui_slider(ui, "Slide", &v, 0.0f, 100.0f);
    zui_slider_v(ui, "VSlide", &v, 0.0f, 100.0f);
    zui_knob(ui, "Turn", &v, 0.0f, 100.0f);

    zui_progress(ui, 0.75f);
    zui_selectable(ui, "Sel", false);
    zui_tooltip(ui, "Help here");
    zui_icon(ui, zui_rect(50, 50, 16, 16), UI_ICON_CLOSE, 0xFF4444FFu);
}

static void test_draw_list_populated(PlatformWindow *w, UiContext *ui)
{
    const UiDrawList *dl = run_frame(w, ui, widgets_many);
    CHECK(dl->count >= 15);

    for (int i = 1; i < dl->count; i++)
        CHECK(dl->cmds[i].layer >= dl->cmds[i - 1].layer);
}

/* ------------------------------------------------------------------ main -- */

int main(void)
{
    if (!platform_init())
    {
        printf("platform_init failed\n");
        return 1;
    }

    /* ============================================================== */
    /*  PIXEL (software) mode tests                                    */
    /* ============================================================== */
    printf("--- PIXEL MODE TESTS ---\n");

    {
        PlatformWindow *w = make_window(RENDER_PIXELS);
        CHECK(w != NULL);
        UiFont *bf = NULL;
        UiContext *ui = zui_create();
        CHECK(ui != NULL);

        /* Force the builtin font so zui_begin doesn't try zui_font_default
           (which references the generated TTF data we stubbed). Set before
           the first frame. */
        {
            bf = zui_font_builtin(2);
            CHECK(bf != NULL);
            zui_set_font(ui, bf);
        }

        test_layout_rows(w, ui);
        test_button_interaction(w, ui);
        test_checkbox_toggle(w, ui);
        test_slider_drag(w, ui);
        test_slider_v_drag(w, ui);
        test_knob_drag(w, ui);
        test_progress_bar(w, ui);
        test_selectable_click(w, ui);
        test_listbox_selection(w, ui);
        test_combo_open(w, ui);
        test_toggle_group_switch(w, ui);
        test_icon_command(w, ui);
        test_tooltip_draw(w, ui);
        test_scrollbar_draw(w, ui);
        test_window_creation(w, ui);
        test_tree_node_expand(w, ui);
        test_menu_open(w, ui);
        test_context_menu_state(w, ui);
        test_menubar_creation(w, ui);
        test_scroll_group(w, ui);
        test_text_input_draw(w, ui);
        test_theme_presets(w, ui);
        test_push_pop_color(w, ui);
        test_state_store(w, ui);
        test_id_push_pop(w, ui);
        test_id_label_rules(w, ui);
        test_font_creation();
        test_software_renderer_output(w, ui);
        test_draw_list_populated(w, ui);

        zui_destroy(ui);
        zui_font_free(bf);
        window_destroy(w);
    }

    /* ============================================================== */
    /*  OPENGL mode tests (widget logic only; no real GL context)      */
    /* ============================================================== */
    printf("--- OPENGL MODE TESTS ---\n");

    {
        PlatformWindow *w = make_window(RENDER_GL);
        CHECK(w != NULL);
        UiFont *bf = NULL;
        UiContext *ui = zui_create();
        CHECK(ui != NULL);

        {
            bf = zui_font_builtin(2);
            CHECK(bf != NULL);
            zui_set_font(ui, bf);
        }

        test_layout_rows(w, ui);
        test_button_interaction(w, ui);
        test_checkbox_toggle(w, ui);
        test_slider_drag(w, ui);
        test_slider_v_drag(w, ui);
        test_knob_drag(w, ui);
        test_progress_bar(w, ui);
        test_selectable_click(w, ui);
        test_listbox_selection(w, ui);
        test_combo_open(w, ui);
        test_toggle_group_switch(w, ui);
        test_icon_command(w, ui);
        test_tooltip_draw(w, ui);
        test_scrollbar_draw(w, ui);
        test_window_creation(w, ui);
        test_tree_node_expand(w, ui);
        test_menu_open(w, ui);
        test_context_menu_state(w, ui);
        test_menubar_creation(w, ui);
        test_scroll_group(w, ui);
        test_text_input_draw(w, ui);
        test_theme_presets(w, ui);
        test_push_pop_color(w, ui);
        test_state_store(w, ui);
        test_id_push_pop(w, ui);
        test_id_label_rules(w, ui);
        test_draw_list_populated(w, ui);

        zui_destroy(ui);
        zui_font_free(bf);
        window_destroy(w);
    }

    platform_shutdown();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}