/*
 * ui_internal.h - private state for zen_ui, shared across the src/ui/*.c units.
 * Not installed; application code only sees zen_ui.h.
 */
#ifndef ZEN_UI_INTERNAL_H
#define ZEN_UI_INTERNAL_H

#include "zen_ui.h"

/* Per-frame input snapshot, lifted from the platform window in zui_begin. */
typedef struct
{
    int      mouse_x, mouse_y;
    int      mouse_dx, mouse_dy;
    float    wheel_x, wheel_y;
    bool     mouse_down;
    bool     mouse_pressed;
    bool     mouse_released;
    int      last_key;
    uint32_t last_char;
    /* Full drain of the platform key/char queues for this frame (text editing
       needs every keystroke, not just the first). */
    int      keys[16];
    int      key_count;
    uint32_t chars[16];
    int      char_count;
    bool     mouse_down_r;     /* right button, for context menus */
    bool     mouse_pressed_r;
    bool     mouse_released_r;
} UiInput;

#define UI_MAX_COLS       16
#define UI_MAX_CONTAINERS 8
#define UI_MAX_COLOR_STACK 32
#define UI_ID_STACK_MAX   32
#define UI_STORE_SIZE     64

typedef enum { ROW_DYNAMIC, ROW_STATIC, ROW_CUSTOM } RowMode;

/* A layout region (the root window-less area, or a window's content box). Rows
   are carved out of `content` top to bottom; cells left to right. */
typedef struct
{
    UiRect  content;     /* widget area, inside padding */
    int     cursor_y;    /* top of the current row, fb coords */
    int     row_height;
    RowMode mode;
    int     columns;
    int     col;         /* next cell index in the current row */
    int     item_w;      /* ROW_STATIC cell width */
    float   widths[UI_MAX_COLS]; /* ROW_CUSTOM: px if >1, else fraction */
    int     row_x;       /* running x within the current row */
    int16_t layer;       /* z layer widgets in this container draw on */
    UiRect  clip_save;   /* ui->clip as it was when this container was pushed */
} UiLayout;

typedef struct
{
    UiColorId id;
    UiColor   prev;
} UiColorSave;

/* ---- file dialog (ui_dialog.c) ----------------------------------------- */

typedef struct
{
    char    *name, *path; /* path_filename and the full path */
    bool     is_dir;
    int64_t  size, mtime;
} UiDirEntry;

typedef struct
{
    bool          open, dirty;
    UiDialogKind  kind;
    char          title[64], filter[128];
    char          dir[1024];     /* current directory (absolute) */
    char          filename[256]; /* editable name (save) / selection */
    int           view;          /* 0 list, 1 icons, 2 details */
    int           sort_col;      /* 0 name, 1 size, 2 date */
    bool          sort_desc;
    int           sel;           /* index into the filtered list, -1 = none */
    double        last_click_t;
    int           last_click_idx;
    UiDirEntry   *entries; int entry_count, entry_cap;
    UiDialogState result;
    char          out[1024];     /* chosen path when OK */
} UiDialog;

struct UiContext
{
    /* immediate-mode interaction state (hot = hovered, active = held) */
    int     hot;
    int     active;
    int     focus;     /* keyboard focus (text input); persists across frames */
    int     caret;     /* caret byte offset in the focused text input */
    char    edit_buf[40]; /* scratch text for editing a numeric field by keyboard */
    int     press_move;   /* accumulated drag distance, to tell a click from a scrub */
    int     next_id;
    int16_t layer; /* current layer widgets push onto; mirrors the top container */

    /* Popups (combo/menu/context/tooltip) draw above everything on a rising
       layer, and eat input from widgets sitting underneath them. */
    int16_t popup_layer;   /* next free popup layer this frame */
    bool    overlay_active;
    UiRect  overlay_rect;  /* input below this rect (lower layer) is blocked */
    int16_t overlay_layer;

    int     want_cursor;   /* CURSOR_* requested this frame, -1 = none */
    int     screen_w, screen_h; /* framebuffer size this frame */

    /* Open menu/context popup: count items this frame so next frame's box fits. */
    ZuiId   menu_id;
    int     menu_item_n;

    /* Horizontal menubar layout (menu labels are auto-width, side by side). */
    bool    in_menubar;
    UiRect  menubar_rect;
    int     menubar_x;

    /* Open scroll groups: track bounds + scroll so group_end can measure the
       content, clamp the offset and draw the scrollbar. */
    struct { ZuiId id; UiRect bounds; float *scroll; int flags; } group_stk[4];
    int     group_sp;

    /* window drag state, kept across frames */
    int drag_id;
    int drag_off_x, drag_off_y;

    PlatformWindow *win; /* window this frame is bound to (for cursor requests) */
    int ime_x, ime_y;    /* the caret rectangle last given to the input method */
    UiDialog   dlg;      /* the (single) modal file dialog */
    UiInput    in;
    UiDrawList dl;
    UiFont    *font; /* current font widgets draw with (default: zui_font_default) */

    UiStyle style;

    /* layout container stack; [0] is the root region */
    UiLayout containers[UI_MAX_CONTAINERS];
    int      container_sp;
    int      window_count; /* per frame, to assign rising layers */

    /* scoped color overrides */
    UiColorSave color_stack[UI_MAX_COLOR_STACK];
    int         color_sp;

    UiRect clip; /* current scissor */

    /* id stack: parent ids are hashed into child ids for stable identity */
    ZuiId id_stack[UI_ID_STACK_MAX];
    int   id_sp;

    /* generic id-keyed state store, open-addressed hash */
    struct { ZuiId id; uint32_t key; int vi; float vf; void *vp; uint8_t tag; } store[UI_STORE_SIZE];
    /* tag: 0=empty, 1=int, 2=float, 3=ptr */
};

/* ---- command buffer (ui_core.c) ---------------------------------------- */

void   ui_dl_reset(UiDrawList *dl);
void   ui_dl_free(UiDrawList *dl);
UiCmd *ui_dl_push(UiContext *ui, UiCmdType type); /* returns a zeroed cmd to fill */
const char *ui_dl_intern(UiContext *ui, const char *str, int len); /* copy into arena */
void   ui_dl_sort(UiDrawList *dl); /* stable sort by layer, then issue order */

/* convenience emitters used by widgets */
void ui_push_rect(UiContext *ui, UiRect r, UiColor color, int thickness);
void ui_push_round_rect(UiContext *ui, UiRect r, UiColor color, int thickness, int rounding);
void ui_push_text(UiContext *ui, int x, int y, const char *str, UiColor color); /* uses ui->font */
void ui_push_line(UiContext *ui, int x0, int y0, int x1, int y1, UiColor color);
void ui_push_icon(UiContext *ui, UiRect box, UiIcon icon, UiColor color);
void ui_push_gradient(UiContext *ui, UiRect r, UiColor top, UiColor bottom);

/* ---- layout (ui_layout.c) --------------------------------------------- */

UiLayout *ui_top(UiContext *ui);            /* current container */
UiRect    ui_layout_next(UiContext *ui);    /* next cell rect, advances the cursor */
void      ui_push_container(UiContext *ui, UiRect content, int16_t layer);
void      ui_pop_container(UiContext *ui);

/* ---- interaction helpers (ui_widgets.c) -------------------------------- */

int  ui_next_id(UiContext *ui);
bool ui_mouse_in(const UiContext *ui, UiRect r);
bool ui_hit(UiContext *ui, int id, UiRect r);
/* Hover test that also returns false when the point is under an open popup on a
   higher layer (so widgets behind a dropdown don't react). */
bool ui_hovered(UiContext *ui, UiRect r);
/* Open a popup layer: bumps and returns the layer to draw the popup on, and
   registers its rect so lower widgets stop receiving input this frame. */
int16_t ui_popup_open(UiContext *ui, UiRect rect);

/* ---- file dialog (ui_dialog.c) ----------------------------------------- */
void ui_dialog_free(UiContext *ui); /* release the dialog's cached entries */

/* Vertically center text of height `th` in a cell of height `h`. */
static inline int ui_text_baseline_y(int cell_y, int h, int th)
{
    return cell_y + (h - th) / 2;
}

#endif /* ZEN_UI_INTERNAL_H */
