/*
 * zen_ui.h - immediate-mode widget toolkit .
 *
 * Sits on top of zen_platform but knows nothing about windows or GL. Each frame
 * the application calls zui_begin(), issues widget calls (zui_button, ...), then
 * zui_end(). Widgets never touch a Framebuffer: they push UiCmd records into a
 * UiDrawList tagged with a layer for z-ordering (popups and dialogs float above
 * windows). A separate renderer walks the sorted list and paints it.
 *
 * This core links only the platform and never OpenGL. The bundled
 * zui_render_software paints into a Framebuffer (RENDER_PIXELS). The GL renderer
 * is a separate library (see zen_2d.h); the core stays renderer-agnostic.
 */
#ifndef ZEN_UI_H
#define ZEN_UI_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "platform.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ====================================================================== */
    /*  Geometry & color                                                      */
    /* ====================================================================== */

    typedef struct { int x, y; } UiVec2;
    typedef struct { int x, y, w, h; } UiRect;

    /* 0xAARRGGBB, same packing as the rest of the project (Framebuffer). */
    typedef uint32_t UiColor;
#define ZUI_RGBA(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define ZUI_RGB(r, g, b) ZUI_RGBA(r, g, b, 0xFF)

    static inline UiRect zui_rect(int x, int y, int w, int h)
    {
        UiRect r = { x, y, w, h };
        return r;
    }

    /* ====================================================================== */
    /*  Draw command list (the backend-agnostic output of a frame)            */
    /* ====================================================================== */

    typedef enum
    {
        UI_CMD_CLIP,  /* set the scissor rect for following commands */
        UI_CMD_RECT,  /* filled rectangle (optional border via thickness) */
        UI_CMD_LINE,
        UI_CMD_TRI,   /* filled triangle */
        UI_CMD_TEXT,  /* glyph run; str points into the frame's text arena */
        UI_CMD_IMAGE, /* blit a Framebuffer region */
        UI_CMD_ICON,  /* procedural icon in a box */
        UI_CMD_GRADIENT /* vertical two-color gradient rect */
    } UiCmdType;

    typedef struct
    {
        UiCmdType type;
        int16_t   layer; /* higher = drawn later / on top; the z-sort key */
        UiRect    clip;  /* scissor in effect when this command was issued */
        UiColor   color;
        union
        {
            struct { UiRect rect; int thickness; int rounding; } rect; /* thickness 0 = filled */
            struct { int x0, y0, x1, y1; } line;
            struct { int x0, y0, x1, y1, x2, y2; } tri;
            struct { int x, y; const char *str; int len; const struct UiFont *font; } text;
            struct { const Framebuffer *src; UiRect dst; } image;
            struct { UiRect box; int icon; } icon;           /* UI_CMD_ICON */
            struct { UiRect rect; UiColor top, bottom; } gradient; /* UI_CMD_GRADIENT */
        } u;
    } UiCmd;

    typedef struct
    {
        UiCmd *cmds;
        int    count;
        int    cap;
        /* Text arena: widget label bytes are copied here so the str pointers in
           UI_CMD_TEXT stay valid until the next zui_begin. */
        char  *text_arena;
        int    text_len;
        int    text_cap;
    } UiDrawList;

    /* ====================================================================== */
    /*  Context & frame                                                       */
    /* ====================================================================== */

    typedef struct UiContext UiContext; /* opaque; defined in src/ui/ui_internal.h */

    typedef struct UiFont UiFont; /* opaque; src/ui/ui_font.c (used by UiStyle) */

    /* ====================================================================== */
    /*  IDs (stable widget identity, survives reorder between frames)         */
    /* ====================================================================== */

    typedef uint32_t ZuiId;

    /* Hash a label (FNV-1a) mixed with the current id-stack seed so the same
       label under different parents gets a different id.
       Label conventions:
         "Save"       - visible text is also the id
         "Save##a"    - visible text "Save", id is "a" (strip after "##")
         "###fixed"   - text may change, id is "fixed" (starts with "###") */
    ZuiId zui_id(UiContext *ui, const char *label);

    /* Push/pop id scope for loops and trees:
         for (int i=0; i<n; ++i) { zui_push_id_i(ui,i); ...; zui_pop_id(ui); } */
    void  zui_push_id(UiContext *ui, const char *label);
    void  zui_push_id_i(UiContext *ui, int i);
    void  zui_pop_id(UiContext *ui);

    /* ====================================================================== */
    /*  State store (id-keyed persistent state, survives frames)              */
    /* ====================================================================== */

    int  *zui_state_int(UiContext *ui, ZuiId id, int defv);
    float *zui_state_float(UiContext *ui, ZuiId id, float defv);
    void  *zui_state_ptr(UiContext *ui, ZuiId id);
    void   zui_state_set_ptr(UiContext *ui, ZuiId id, void *p);

    /* ====================================================================== */
    /*  Theme (semantic color slots + metrics; one struct, swap presets)      */
    /* ====================================================================== */

    typedef enum
    {
        UI_COL_WINDOW_BG,
        UI_COL_PANEL,
        UI_COL_BORDER,
        UI_COL_TITLE,
        UI_COL_TITLE_ACTIVE,
        UI_COL_TEXT,
        UI_COL_TEXT_DIM,
        UI_COL_BUTTON,
        UI_COL_BUTTON_HOVER,
        UI_COL_BUTTON_ACTIVE,
        UI_COL_ACCENT,     /* checks, focus, highlights */
        UI_COL_TRACK,      /* slider/scroll groove */
        UI_COL_KNOB,
        UI_COL_INPUT_BG,
        UI_COL_SELECT,     /* selected row / text */
        UI_COL_COUNT
    } UiColorId;

    typedef struct
    {
        UiColor colors[UI_COL_COUNT];
        int     pad_x, pad_y;       /* container inner padding */
        int     spacing_x, spacing_y; /* gaps between widgets */
        int     border;             /* border thickness, px */
        int     rounding;           /* corner radius, px (0 = square) */
        int     row_height;         /* default widget height */
        int     scrollbar;          /* scrollbar thickness */
        int     title_height;       /* window title bar */
        UiFont *font;               /* NULL = the context default */
    } UiStyle;

    /* Built-in presets. dark/light are neutral grays; classic is the colored
       XP/Vista-style blue theme. */
    void     zui_style_dark(UiStyle *out);
    void     zui_style_light(UiStyle *out);
    void     zui_style_classic(UiStyle *out);
    void     zui_set_style(UiContext *ui, const UiStyle *s);
    UiStyle *zui_style(UiContext *ui);      /* mutable current style */

    /* Scoped overrides. Pop the same count you pushed before zui_end. */
    void zui_push_color(UiContext *ui, UiColorId id, UiColor c);
    void zui_pop_color(UiContext *ui, int count);

    /* ====================================================================== */
    /*  Fonts (text shaping is per-codepoint advance; UTF-8 in, glyphs out)    */
    /* ====================================================================== */

    /* The embedded default (DejaVuSans, baked at px pixels). Broad Latin/CJK/
       Cyrillic/Greek coverag */
    UiFont *zui_font_default(int px);

    /* A TTF from disk or memory, baked at px pixels. Caller owns it (zui_font_free).
       The _mem variant does not copy: keep data alive for the font's lifetime. */
    UiFont *zui_font_load_ttf(const char *path, int px);
    UiFont *zui_font_load_ttf_mem(const uint8_t *data, int size, int px);

    /* The 5x7 bitmap fallback (ASCII only), scaled. Cheap, no TTF needed. */
    UiFont *zui_font_builtin(int scale);

    void zui_font_free(UiFont *f);

    /* Pixel width of a UTF-8 run (len < 0 = strlen) and the line advance. */
    int zui_text_width(const UiFont *f, const char *utf8, int len);
    int zui_font_height(const UiFont *f);

    /* Glyph access for external renderers. Glyphs are rasterized on the CPU and
       cached by the core; a renderer blits the coverage (software) or packs it
       into an atlas texture (GL). This is the only core/renderer seam. */
    typedef struct
    {
        int            w, h;       /* coverage bitmap size, 0 for blank glyphs */
        int            xoff, yoff; /* top-left offset from the pen, baseline-relative */
        int            advance;    /* horizontal pen advance, px */
        const uint8_t *coverage;   /* w*h, 0..255; NULL when w*h == 0 */
    } UiGlyphInfo;

    bool zui_font_glyph(const UiFont *f, uint32_t codepoint, UiGlyphInfo *out);
    int  zui_font_ascent(const UiFont *f); /* baseline offset from the top */

    /* Font widgets draw with until changed. Defaults to zui_font_default(16). */
    void zui_set_font(UiContext *ui, UiFont *f);

    /* Allocate / free a context. One per window is the common case. */
    UiContext *zui_create(void);
    void       zui_destroy(UiContext *ui);

    /* Per-frame bracket. zui_begin pulls input from the platform window and resets
       the draw list; widget calls go between; zui_end finalizes and z-sorts. */
    void zui_begin(UiContext *ui, PlatformWindow *w);
    void zui_end(UiContext *ui);

    /* The finished, layer-sorted command list for this frame. Valid between
       zui_end and the next zui_begin. Feed it to a renderer below. */
    const UiDrawList *zui_draw_list(UiContext *ui);

    /* ====================================================================== */
    /*  Layout (row-based; widgets consume cells, no explicit coordinates)     */
    /* ====================================================================== */

    /* Start a row of `columns` equal cells, `height` px tall. Following widget
       calls fill the cells left to right. height <= 0 uses the style row_height. */
    void zui_row(UiContext *ui, int height, int columns);

    /* A row of fixed-width cells (item_w px each). */
    void zui_row_static(UiContext *ui, int height, int item_w, int columns);

    /* A row with per-cell widths: call zui_row_push once per column. A width > 1
       is pixels; a width <= 1 is a fraction of the row's content width. */
    void zui_row_begin(UiContext *ui, int height, int columns);
    void zui_row_push(UiContext *ui, float width);
    void zui_row_end(UiContext *ui);

    /* The next widget rect without consuming it (for measuring / custom draw). */
    UiRect zui_layout_peek(UiContext *ui);

    /* ====================================================================== */
    /*  Scroll region + scrollbar                                             */
    /* ====================================================================== */

    enum { ZUI_GROUP_BORDER = 1, ZUI_GROUP_NO_SCROLLBAR = 2 };

    /* Begin a scrollable group. Content between begin/end is offset by the
       group's scroll and clipped to its bounds. The scroll offset is stored
       per-id in the state store. Returns false if the group has no visible area. */
    bool zui_group_begin(UiContext *ui, const char *id, UiRect bounds, int flags);
    void zui_group_end(UiContext *ui);

    /* A standalone scrollbar. `offset` is the current scroll, `content` the
       virtual extent, `view` the visible extent (all in pixels along the scroll
       axis). vertical=true for V, false for H. Returns true while dragging. */
    bool zui_scrollbar(UiContext *ui, UiRect bounds, float *offset, float content, float view, bool vertical);

    /* ====================================================================== */
    /*  Icons (procedural, rendered from draw2d primitives)                   */
    /* ====================================================================== */

    typedef enum {
        UI_ICON_NONE, UI_ICON_ARROW_RIGHT, UI_ICON_ARROW_DOWN, UI_ICON_ARROW_LEFT,
        UI_ICON_CHECK, UI_ICON_CHEVRON, UI_ICON_CLOSE, UI_ICON_PLUS, UI_ICON_MINUS,
        UI_ICON_FOLDER, UI_ICON_FILE, UI_ICON_DOT, UI_ICON_COUNT
    } UiIcon;

    /* Draw an icon inside a box. The renderer rasterizes the shape. */
    void zui_icon(UiContext *ui, UiRect box, UiIcon icon, UiColor color);

    /* ====================================================================== */
    /*  Widgets (immediate mode) - SCAFFOLD, not yet implemented              */
    /* ====================================================================== */

    void zui_label(UiContext *ui, const char *text);
    bool zui_button(UiContext *ui, const char *text);
    bool zui_checkbox(UiContext *ui, const char *text, bool *value);
    bool zui_slider(UiContext *ui, const char *label, float *value, float min, float max);
    bool zui_slider_v(UiContext *ui, const char *label, float *value, float min, float max);
    bool zui_knob(UiContext *ui, const char *label, float *value, float min, float max);
    bool zui_progress(UiContext *ui, float fraction);
    bool zui_selectable(UiContext *ui, const char *label, bool selected);
    int  zui_listbox(UiContext *ui, const char *id, const char **items, int count, int *selected, int rows);
    bool zui_combo(UiContext *ui, const char *id, const char **items, int count, int *selected);

    /* menus & overlays */
    void zui_menubar_begin(UiContext *ui);
    void zui_menubar_end(UiContext *ui);
    bool zui_menu_begin(UiContext *ui, const char *label, int width);
    void zui_menu_end(UiContext *ui);
    bool zui_menu_item(UiContext *ui, const char *label);
    /* A thin divider line between groups of menu entries. */
    void zui_menu_separator(UiContext *ui);
    /* A checkable menu entry: shows a check mark when *checked, toggles it on
       click. Use for on/off options inside a menu instead of a raw checkbox. */
    bool zui_menu_item_check(UiContext *ui, const char *label, bool *checked);
    /* A radio menu entry: shows a dot when *selected == value, picks it on click. */
    bool zui_menu_item_radio(UiContext *ui, const char *label, int *selected, int value);
    bool zui_context_menu_begin(UiContext *ui, const char *id, int width);
    void zui_context_menu_end(UiContext *ui);
    bool zui_tree_node(UiContext *ui, const char *label);
    void zui_tree_pop(UiContext *ui);
    int  zui_toggle_group(UiContext *ui, const char **labels, int count, int *selected);
    void zui_tooltip(UiContext *ui, const char *text);

    /* A tab strip: variable-width tabs, the active one highlighted with an accent
       underline. Consumes one row; updates *active on click and returns the new
       index (or -1 if unchanged). Pair with a switch on *active for the body. */
    int  zui_tabs(UiContext *ui, const char *id, const char **labels, int count, int *active);

    /* A draggable splitter handle for resizable docked panels. `bounds` is the
       grab strip; vertical=true for a vertical bar (resizes width, drag along x),
       false for a horizontal bar (resizes height, drag along y). Returns the drag
       delta in pixels this frame (0 when idle): add it to one panel's size and
       subtract from the neighbour, then clamp. */
    int  zui_splitter(UiContext *ui, const char *id, UiRect bounds, bool vertical);

    bool zui_text_input(UiContext *ui, const char *label, char *buf, int cap);

    /* Inspector-style fields. A numeric field you scrub by dragging horizontally
       (speed = units per pixel); min<max clamps, min==max is unbounded. */
    bool zui_drag_float(UiContext *ui, const char *label, float *v, float speed, float min, float max);
    /* Three tinted X/Y/Z scrub fields packed into one cell (position/rotation/scale). */
    bool zui_drag_float3(UiContext *ui, const char *label, float v[3], float speed);
    /* A collapsing section header (full-width bar + arrow); state kept per-label.
       Returns true while expanded, so guard the section body with it. */
    bool zui_collapsing_header(UiContext *ui, const char *label);

    /* A property row: a label column then an editor column. Call the field widget
       right after; the row closes once both cells are filled. `label_frac` is the
       label column's share of the width (<=0 uses a sensible default). */
    void zui_prop_row(UiContext *ui, const char *label, float label_frac);

    /* A status bar pinned to the bottom of the window, full width. Issue widgets
       (labels/buttons) between begin and end; they lay out left to right. */
    void zui_statusbar_begin(UiContext *ui);
    void zui_statusbar_end(UiContext *ui);
    int  zui_statusbar_height(UiContext *ui); /* px to reserve above it */

    /* Floating, draggable window. Returns false if collapsed/closed; widgets
       issued while it returns true land on that window's layer. */
    bool zui_window_begin(UiContext *ui, const char *title, UiRect *bounds);
    void zui_window_end(UiContext *ui);

    /* ====================================================================== */
    /*  Dialogs (the reason this lib exists) - SCAFFOLD                       */
    /* ====================================================================== */

    typedef enum
    {
        ZUI_DIALOG_OPEN_FILE,
        ZUI_DIALOG_SAVE_FILE,
        ZUI_DIALOG_PICK_FOLDER
    } UiDialogKind;

    typedef struct
    {
        UiDialogKind kind;
        const char  *title;
        const char  *start_dir;  /* NULL = current working directory */
        const char  *filter;     /* e.g. ".png;.bmp"; NULL = all files */
    } UiDialogConfig;

    typedef enum { ZUI_DIALOG_RUNNING, ZUI_DIALOG_OK, ZUI_DIALOG_CANCEL } UiDialogState;

    /* Open a modal dialog (renders on the top layer). Drive it once per frame with
       zui_dialog and read the chosen path when it returns ZUI_DIALOG_OK. */
    void          zui_dialog_open(UiContext *ui, const UiDialogConfig *cfg);
    UiDialogState zui_dialog(UiContext *ui, char *out_path, int cap);

    /* ====================================================================== */
    /*  Renderers (consume a UiDrawList) - SCAFFOLD                           */
    /* ====================================================================== */

    /* Request a mouse cursor shape for the current frame. The backend applies it
       after zui_end. Idempotent; multi-widget calls are fine, last request wins. */
    void zui_request_cursor(UiContext *ui, int cursor);

    /* Software path: paints into a CPU Framebuffer using draw2d. GL-free, bundled
       with the core as the default RENDER_PIXELS renderer.
       The GL renderer is a separate library: zui_render_gl lives in zen_2d.h, so
       the core never depends on OpenGL. */
    void zui_render_software(Framebuffer *fb, const UiDrawList *list);

#ifdef __cplusplus
}
#endif

#endif /* ZEN_UI_H */