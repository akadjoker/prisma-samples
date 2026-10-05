/*
 * ui_dialog.c - modal file dialog (open / save / pick folder).
 *
 * A self-contained modal rendered on the popup layer over a dimmed backdrop.
 * Navigates the real filesystem with the platform fs API and offers three
 * views (list / icons / details), a places sidebar, a clickable breadcrumb,
 * create-folder and column sorting. Being part of the core it uses the private
 * helpers (ui_top, ui_push_*, ui_popup_open) directly.
 */
#include "ui_internal.h"
#include "ui_font.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define COL(ui, id) ((ui)->style.colors[id])

/* ----------------------------------------------------------------- text --- */

static void dtext(UiContext *ui, int x, int cell_y, int h, const char *s, UiColor c)
{
    int top = ui_text_baseline_y(cell_y, h, ui_font_ascent(ui->font));
    ui_push_text(ui, x, top, s, c);
}
static void dtext_c(UiContext *ui, UiRect r, const char *s, UiColor c)
{
    int tw = zui_text_width(ui->font, s, -1);
    dtext(ui, r.x + (r.w - tw) / 2, r.y, r.h, s, c);
}

/* A small clickable chip drawn at an explicit rect (the dialog is topmost, so a
   plain ui_mouse_in test is enough). */
static bool chip(UiContext *ui, UiRect r, const char *label, bool active, bool border)
{
    int id = ui_next_id(ui);
    bool hov = ui_mouse_in(ui, r);
    if (hov) ui->hot = id;
    if (hov && ui->in.mouse_pressed) ui->active = id;

    UiColor bg = active ? COL(ui, UI_COL_ACCENT)
                        : (hov ? COL(ui, UI_COL_BUTTON_HOVER) : COL(ui, UI_COL_BUTTON));
    ui_push_round_rect(ui, r, bg, 0, ui->style.rounding);
    if (border)
        ui_push_round_rect(ui, r, COL(ui, UI_COL_BORDER), 1, ui->style.rounding);
    dtext_c(ui, r, label, active ? ZUI_RGB(255, 255, 255) : COL(ui, UI_COL_TEXT));

    bool clicked = ui->in.mouse_released && ui->active == id && hov;
    if (clicked) ui->active = 0;
    return clicked;
}

/* ----------------------------------------------------------- formatting --- */

static void fmt_size(int64_t b, char *out, int cap)
{
    if (b < 0) { snprintf(out, cap, "-"); return; }
    if (b < 1024) snprintf(out, cap, "%lld B", (long long)b);
    else if (b < 1024 * 1024) snprintf(out, cap, "%.1f KB", b / 1024.0);
    else if (b < 1024LL * 1024 * 1024) snprintf(out, cap, "%.1f MB", b / (1024.0 * 1024));
    else snprintf(out, cap, "%.1f GB", b / (1024.0 * 1024 * 1024));
}
static void fmt_date(int64_t t, char *out, int cap)
{
    if (t <= 0) { snprintf(out, cap, "-"); return; }
    time_t tt = (time_t)t;
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    strftime(out, (size_t)cap, "%Y-%m-%d %H:%M", &lt);
}

/* ------------------------------------------------------------ listing ----- */

/* strcasecmp is POSIX only (strings.h is missing on MSVC). */
static int ui_stricmp(const char *a, const char *b)
{
    for (;; a++, b++)
    {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        if (!ca) return 0;
    }
}

static int g_sort_col;
static bool g_sort_desc;
static int cmp_entry(const void *a, const void *b)
{
    const UiDirEntry *x = (const UiDirEntry *)a, *y = (const UiDirEntry *)b;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1; /* folders first */
    int r;
    if (g_sort_col == 1) r = (x->size > y->size) - (x->size < y->size);
    else if (g_sort_col == 2) r = (x->mtime > y->mtime) - (x->mtime < y->mtime);
    else r = ui_stricmp(x->name, y->name);
    return g_sort_desc ? -r : r;
}

static void dlg_free_entries(UiDialog *d)
{
    for (int i = 0; i < d->entry_count; i++)
    {
        free(d->entries[i].name);
        free(d->entries[i].path);
    }
    free(d->entries);
    d->entries = NULL;
    d->entry_count = d->entry_cap = 0;
}

static bool filter_match(const char *filter, const char *path)
{
    if (!filter || !filter[0]) return true;
    char tmp[128];
    strncpy(tmp, filter, sizeof tmp - 1);
    tmp[sizeof tmp - 1] = 0;
    for (char *tok = strtok(tmp, ";"); tok; tok = strtok(NULL, ";"))
        if (path_has_extension(path, tok)) return true;
    return false;
}

static void dlg_push_entry(UiDialog *d, const char *path, bool is_dir)
{
    if (d->entry_count == d->entry_cap)
    {
        d->entry_cap = d->entry_cap ? d->entry_cap * 2 : 64;
        d->entries = (UiDirEntry *)realloc(d->entries, (size_t)d->entry_cap * sizeof(UiDirEntry));
    }
    UiDirEntry *e = &d->entries[d->entry_count++];
    e->name = strdup(path_filename(path));
    e->path = strdup(path);
    e->is_dir = is_dir;
    e->size = is_dir ? -1 : file_size(path);
    e->mtime = file_mod_time(path);
}

void ui_dialog_free(UiContext *ui)
{
    dlg_free_entries(&ui->dlg);
}

static void dlg_reload(UiDialog *d)
{
    dlg_free_entries(d);
    d->dirty = false;
    DirList dl;
    if (!dir_list(d->dir, &dl))
        return;
    for (int i = 0; i < dl.count; i++)
    {
        bool is_dir = dir_exists(dl.paths[i]);
        if (!is_dir && !filter_match(d->filter, dl.paths[i]))
            continue;
        dlg_push_entry(d, dl.paths[i], is_dir);
    }
    dir_list_free(&dl);
    g_sort_col = d->sort_col;
    g_sort_desc = d->sort_desc;
    qsort(d->entries, (size_t)d->entry_count, sizeof(UiDirEntry), cmp_entry);
    if (d->sel >= d->entry_count) d->sel = -1;
}

static void dlg_goto(UiDialog *d, const char *path)
{
    strncpy(d->dir, path, sizeof d->dir - 1);
    d->dir[sizeof d->dir - 1] = 0;
    d->dirty = true;
    d->sel = -1;
}

static void dlg_up(UiDialog *d)
{
    char parent[1024];
    path_directory(d->dir, parent, sizeof parent);
    if (parent[0] && strcmp(parent, d->dir) != 0)
        dlg_goto(d, parent);
}

static void dlg_confirm(UiContext *ui, UiDialog *d, const char *name_or_null)
{
    if (d->kind == ZUI_DIALOG_PICK_FOLDER)
        snprintf(d->out, sizeof d->out, "%s", d->dir);
    else
        snprintf(d->out, sizeof d->out, "%s/%s", d->dir,
                 name_or_null ? name_or_null : d->filename);
    d->result = ZUI_DIALOG_OK;
    d->open = false;
    (void)ui;
}

/* ------------------------------------------------------------ public ------ */

void zui_dialog_open(UiContext *ui, const UiDialogConfig *cfg)
{
    UiDialog *d = &ui->dlg;
    dlg_free_entries(d);
    memset(d->dir, 0, sizeof d->dir);
    d->kind = cfg->kind;
    snprintf(d->title, sizeof d->title, "%s", cfg->title ? cfg->title : "Ficheiro");
    snprintf(d->filter, sizeof d->filter, "%s", cfg->filter ? cfg->filter : "");
    snprintf(d->dir, sizeof d->dir, "%s", cfg->start_dir ? cfg->start_dir : dir_current());
    d->filename[0] = 0;
    d->sel = -1;
    d->view = 2;        /* details by default */
    d->sort_col = 0;
    d->sort_desc = false;
    d->open = true;
    d->dirty = true;
    d->result = ZUI_DIALOG_RUNNING;
    d->last_click_idx = -1;
}

/* Draw one file row (list / details). Returns the action: 0 none, 1 selected,
   2 activated (double-click). Clamped to `bounds` for hit-testing. */
static int file_row(UiContext *ui, UiDialog *d, int i, UiRect r, UiRect bounds, bool details)
{
    UiDirEntry *e = &d->entries[i];
    int id = ui_next_id(ui);
    bool hov = ui_mouse_in(ui, r) && ui_mouse_in(ui, bounds);
    if (hov) ui->hot = id;
    if (hov && ui->in.mouse_pressed) ui->active = id;

    if (d->sel == i)
        ui_push_rect(ui, r, COL(ui, UI_COL_SELECT), 0);
    else if (hov)
        ui_push_rect(ui, r, COL(ui, UI_COL_BUTTON_HOVER), 0);

    int icon_s = r.h - 6;
    UiRect ib = zui_rect(r.x + 4, r.y + 3, icon_s, icon_s);
    ui_push_icon(ui, ib, e->is_dir ? UI_ICON_FOLDER : UI_ICON_FILE,
                 e->is_dir ? COL(ui, UI_COL_ACCENT) : COL(ui, UI_COL_TEXT_DIM));
    UiColor tc = (d->sel == i) ? ZUI_RGB(255, 255, 255) : COL(ui, UI_COL_TEXT);
    int name_x = r.x + icon_s + 10;

    if (details)
    {
        int col_size = r.x + (int)(r.w * 0.56f);
        int col_date = r.x + (int)(r.w * 0.74f);
        dtext(ui, name_x, r.y, r.h, e->name, tc);
        char b[32];
        if (!e->is_dir) { fmt_size(e->size, b, sizeof b); dtext(ui, col_size, r.y, r.h, b, tc); }
        fmt_date(e->mtime, b, sizeof b); dtext(ui, col_date, r.y, r.h, b, tc);
    }
    else
        dtext(ui, name_x, r.y, r.h, e->name, tc);

    int action = 0;
    if (ui->in.mouse_released && ui->active == id && hov)
    {
        double now = time_seconds();
        if (d->last_click_idx == i && (now - d->last_click_t) < 0.4)
            action = 2;
        else
            action = 1;
        d->last_click_t = now;
        d->last_click_idx = i;
        ui->active = 0;
    }
    return action;
}

/* Icon-grid cell. Returns 0/1/2 like file_row. */
static int file_icon(UiContext *ui, UiDialog *d, int i, UiRect r, UiRect bounds)
{
    UiDirEntry *e = &d->entries[i];
    int id = ui_next_id(ui);
    bool hov = ui_mouse_in(ui, r) && ui_mouse_in(ui, bounds);
    if (hov) ui->hot = id;
    if (hov && ui->in.mouse_pressed) ui->active = id;

    if (d->sel == i)
        ui_push_round_rect(ui, r, COL(ui, UI_COL_SELECT), 0, ui->style.rounding);
    else if (hov)
        ui_push_round_rect(ui, r, COL(ui, UI_COL_BUTTON_HOVER), 0, ui->style.rounding);

    int is = 34;
    UiRect ib = zui_rect(r.x + (r.w - is) / 2, r.y + 6, is, is);
    ui_push_icon(ui, ib, e->is_dir ? UI_ICON_FOLDER : UI_ICON_FILE,
                 e->is_dir ? COL(ui, UI_COL_ACCENT) : COL(ui, UI_COL_TEXT_DIM));
    UiColor tc = (d->sel == i) ? ZUI_RGB(255, 255, 255) : COL(ui, UI_COL_TEXT);
    int tw = zui_text_width(ui->font, e->name, -1);
    if (tw > r.w - 4) tw = r.w - 4;
    dtext(ui, r.x + (r.w - tw) / 2, r.y + is + 8, 18, e->name, tc);

    int action = 0;
    if (ui->in.mouse_released && ui->active == id && hov)
    {
        double now = time_seconds();
        action = (d->last_click_idx == i && (now - d->last_click_t) < 0.4) ? 2 : 1;
        d->last_click_t = now; d->last_click_idx = i; ui->active = 0;
    }
    return action;
}

static void handle_action(UiContext *ui, UiDialog *d, int i, int action)
{
    if (!action) return;
    UiDirEntry *e = &d->entries[i];
    if (action == 1)
    {
        d->sel = i;
        if (!e->is_dir) snprintf(d->filename, sizeof d->filename, "%s", e->name);
    }
    else /* double-click */
    {
        if (e->is_dir) dlg_goto(d, e->path);
        else dlg_confirm(ui, d, e->name);
    }
}

UiDialogState zui_dialog(UiContext *ui, char *out_path, int cap)
{
    UiDialog *d = &ui->dlg;
    if (!d->open)
        return ZUI_DIALOG_CANCEL;
    if (d->dirty)
        dlg_reload(d);

    /* ESC cancels */
    for (int k = 0; k < ui->in.key_count; k++)
        if (ui->in.keys[k] == KEY_ESCAPE) { d->result = ZUI_DIALOG_CANCEL; d->open = false; }

    int W = ui->screen_w, H = ui->screen_h;
    int16_t layer = ui_popup_open(ui, zui_rect(0, 0, W, H));
    ui->layer = layer;
    UiRect saved_clip = ui->clip;

    /* dim backdrop */
    ui->clip = zui_rect(0, 0, W, H);
    ui_push_rect(ui, zui_rect(0, 0, W, H), ZUI_RGBA(0, 0, 0, 150), 0);

    /* centred panel */
    int pw = W - 120 < 760 ? W - 120 : 760;
    int ph = H - 120 < 540 ? H - 120 : 540;
    if (pw < 480) pw = W - 20;
    if (ph < 360) ph = H - 20;
    UiRect panel = zui_rect((W - pw) / 2, (H - ph) / 2, pw, ph);
    ui->clip = panel;

    int th = ui->style.title_height;
    ui_push_rect(ui, panel, COL(ui, UI_COL_WINDOW_BG), 0);
    ui_push_rect(ui, zui_rect(panel.x, panel.y, panel.w, th), COL(ui, UI_COL_TITLE_ACTIVE), 0);
    ui_push_round_rect(ui, panel, COL(ui, UI_COL_BORDER), 1, 0);
    dtext(ui, panel.x + 12, panel.y, th, d->title, COL(ui, UI_COL_TEXT));

    int pad = 8;
    int toolbar_h = 32, bottom_h = 40, sidebar_w = 150;
    int cx = panel.x + pad, cw = panel.w - 2 * pad;
    int toolbar_y = panel.y + th + pad;
    int body_y = toolbar_y + toolbar_h + pad;
    int bottom_y = panel.y + panel.h - bottom_h - pad;
    int body_h = bottom_y - body_y - pad;

    /* ---- toolbar: Up / New folder / breadcrumb / view toggle ---- */
    {
        ui->clip = zui_rect(cx, toolbar_y, cw, toolbar_h);
        int bx = cx, by = toolbar_y, bh = toolbar_h;
        if (chip(ui, zui_rect(bx, by, 56, bh), "Subir", false, true)) dlg_up(d);
        bx += 60;
        if (chip(ui, zui_rect(bx, by, 90, bh), "Nova pasta", false, true))
        {
            char np[1024];
            snprintf(np, sizeof np, "%s/Nova Pasta", d->dir);
            if (dir_make(np)) d->dirty = true;
        }
        bx += 96;

        /* view toggle on the right */
        const char *vlabels[3] = {"Lista", "Icones", "Detalhes"};
        int vw = 80;
        for (int v = 0; v < 3; v++)
        {
            UiRect vr = zui_rect(cx + cw - (3 - v) * (vw + 2), by, vw, bh);
            if (chip(ui, vr, vlabels[v], d->view == v, false)) d->view = v;
        }

        /* breadcrumb between, clipped to the gap */
        int crumbs_right = cx + cw - 3 * (vw + 2) - 8;
        ui->clip = zui_rect(bx, by, crumbs_right - bx, bh);
        char acc[1024] = {0};
        const char *p = d->dir;
        /* leading root */
        {
            UiRect rr = zui_rect(bx, by, 24, bh);
            if (chip(ui, rr, "/", false, false)) dlg_goto(d, "/");
            bx += 26;
        }
        char seg[256];
        while (*p)
        {
            while (*p == '/') p++;
            int n = 0;
            while (p[n] && p[n] != '/') n++;
            if (n == 0) break;
            if (n > 255) n = 255;
            memcpy(seg, p, n); seg[n] = 0;
            strncat(acc, "/", sizeof acc - strlen(acc) - 1);
            strncat(acc, seg, sizeof acc - strlen(acc) - 1);
            int sw = zui_text_width(ui->font, seg, -1) + 14;
            if (bx + sw > crumbs_right) { dtext(ui, bx, by, bh, "...", COL(ui, UI_COL_TEXT_DIM)); break; }
            if (chip(ui, zui_rect(bx, by, sw, bh), seg, false, false)) dlg_goto(d, acc);
            bx += sw + 2;
            p += n;
        }
    }

    /* ---- sidebar: places ---- */
    {
        UiRect sb = zui_rect(cx, body_y, sidebar_w, body_h);
        ui_push_round_rect(ui, sb, COL(ui, UI_COL_PANEL), 0, ui->style.rounding);
        ui_push_round_rect(ui, sb, COL(ui, UI_COL_BORDER), 1, ui->style.rounding);
        ui->clip = sb;
        const char *home = getenv("HOME");
        struct { const char *label, *path; } places[4] = {
            {"Home", home ? home : "/"},
            {"Raiz", "/"},
            {"Atual", dir_current()},
            {"Dados", dir_data()},
        };
        int iy = sb.y + 6;
        for (int i = 0; i < 4; i++)
        {
            UiRect r = zui_rect(sb.x + 4, iy, sb.w - 8, 26);
            bool sel = strcmp(d->dir, places[i].path) == 0;
            if (chip(ui, r, places[i].label, sel, false)) dlg_goto(d, places[i].path);
            iy += 28;
        }
    }

    /* ---- file area (list / icons / details) ---- */
    {
        UiRect fa = zui_rect(cx + sidebar_w + pad, body_y, cw - sidebar_w - pad, body_h);
        int header_h = (d->view == 2) ? 24 : 0;

        if (d->view == 2) /* details header (sortable) */
        {
            UiRect hr = zui_rect(fa.x, fa.y, fa.w, header_h);
            ui->clip = hr;
            ui_push_rect(ui, hr, COL(ui, UI_COL_PANEL), 0);
            const char *cols[3] = {"Nome", "Tamanho", "Data"};
            float at[3] = {0.0f, 0.56f, 0.74f};
            for (int c = 0; c < 3; c++)
            {
                int x0 = hr.x + (int)(hr.w * at[c]);
                int x1 = hr.x + (c < 2 ? (int)(hr.w * at[c + 1]) : hr.w);
                UiRect cr = zui_rect(x0, hr.y, x1 - x0, hr.h);
                int id = ui_next_id(ui);
                bool hov = ui_mouse_in(ui, cr);
                if (hov && ui->in.mouse_pressed) ui->active = id;
                char lab[48];
                snprintf(lab, sizeof lab, "%s%s", cols[c],
                         d->sort_col == c ? (d->sort_desc ? " v" : " ^") : "");
                dtext(ui, cr.x + 6, cr.y, cr.h, lab, COL(ui, UI_COL_TEXT_DIM));
                if (ui->in.mouse_released && ui->active == id && hov)
                {
                    if (d->sort_col == c) d->sort_desc = !d->sort_desc;
                    else { d->sort_col = c; d->sort_desc = false; }
                    d->dirty = true;
                    ui->active = 0;
                }
            }
        }

        UiRect lr = zui_rect(fa.x, fa.y + header_h, fa.w, fa.h - header_h);
        if (zui_group_begin(ui, "dlg_files", lr, ZUI_GROUP_BORDER))
        {
            UiLayout *L = ui_top(ui);
            int content_w = L->content.w;
            if (d->view == 1) /* icon grid */
            {
                int cell_w = 96, cell_h = 72;
                int ncol = content_w / cell_w; if (ncol < 1) ncol = 1;
                for (int i = 0; i < d->entry_count;)
                {
                    zui_row(ui, cell_h, ncol);
                    for (int c = 0; c < ncol; c++)
                    {
                        UiRect cell = ui_layout_next(ui);
                        if (i >= d->entry_count) continue;
                        handle_action(ui, d, i, file_icon(ui, d, i, cell, lr));
                        i++;
                    }
                }
            }
            else /* list or details: one row per entry */
            {
                int row_h = 24;
                for (int i = 0; i < d->entry_count; i++)
                {
                    zui_row(ui, row_h, 1);
                    UiRect r = ui_layout_next(ui);
                    handle_action(ui, d, i, file_row(ui, d, i, r, lr, d->view == 2));
                }
            }
            zui_group_end(ui);
        }
    }

    /* ---- bottom: filename + buttons ---- */
    {
        UiRect bb = zui_rect(cx, bottom_y, cw, bottom_h);
        ui->layer = layer; /* group_end above reset it; keep dialog on top */
        ui->clip = bb;
        const char *ok_label = d->kind == ZUI_DIALOG_SAVE_FILE ? "Guardar"
                             : d->kind == ZUI_DIALOG_PICK_FOLDER ? "Escolher" : "Abrir";
        int btn_w = 96;
        UiRect cancel_r = zui_rect(bb.x + bb.w - btn_w, bb.y + 6, btn_w, 28);
        UiRect ok_r = zui_rect(cancel_r.x - btn_w - 6, bb.y + 6, btn_w, 28);

        if (d->kind != ZUI_DIALOG_PICK_FOLDER)
        {
            UiRect fr = zui_rect(bb.x, bb.y + 6, ok_r.x - bb.x - 8, 28);
            /* a layout container is needed for the text input widget */
            ui_push_container(ui, zui_rect(fr.x, fr.y, fr.w, fr.h), layer);
            ui->clip = bb;
            zui_row(ui, 28, 1);
            zui_text_input(ui, "##dlgname", d->filename, (int)sizeof d->filename);
            ui_pop_container(ui);
            ui->layer = layer; /* pop reset the layer; restore the dialog layer */
        }
        else
            dtext(ui, bb.x + 2, bb.y + 6, 28, d->dir, COL(ui, UI_COL_TEXT_DIM));

        ui->clip = bb;
        if (chip(ui, ok_r, ok_label, true, false))
            dlg_confirm(ui, d, NULL);
        if (chip(ui, cancel_r, "Cancelar", false, true))
        { d->result = ZUI_DIALOG_CANCEL; d->open = false; }
    }

    ui->clip = saved_clip;

    if (!d->open) /* closed this frame: report once */
    {
        if (d->result == ZUI_DIALOG_OK && out_path && cap > 0)
        {
            strncpy(out_path, d->out, (size_t)cap - 1);
            out_path[cap - 1] = 0;
        }
        UiDialogState r = d->result;
        d->result = ZUI_DIALOG_RUNNING;
        return r;
    }
    return ZUI_DIALOG_RUNNING;
}
