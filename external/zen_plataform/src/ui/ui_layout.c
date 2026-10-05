/*
 * ui_layout.c - row-based layout. Widgets ask ui_layout_next() for their cell;
 * the caller only describes rows (zui_row / zui_row_static / zui_row_begin).
 *
 * A container is a rectangular region with a cursor that walks down row by row.
 * Cells are carved left to right per the active row mode. Windows and the root
 * region are both just containers on a small stack.
 */
#include "ui_internal.h"

UiLayout *ui_top(UiContext *ui)
{
    return &ui->containers[ui->container_sp - 1];
}

void ui_push_container(UiContext *ui, UiRect content, int16_t layer)
{
    if (ui->container_sp >= UI_MAX_CONTAINERS)
        return;
    UiLayout *L = &ui->containers[ui->container_sp++];
    L->content = content;
    L->cursor_y = content.y;
    L->row_height = ui->style.row_height;
    L->mode = ROW_DYNAMIC;
    L->columns = 1;
    L->col = 0;
    L->item_w = 0;
    L->row_x = content.x;
    L->layer = layer;
    L->clip_save = ui->clip;
    ui->layer = layer;
}

void ui_pop_container(UiContext *ui)
{
    if (ui->container_sp > 0)
    {
        ui->clip = ui->containers[ui->container_sp - 1].clip_save;
        ui->container_sp--;
    }
    ui->layer = ui->container_sp > 0 ? ui_top(ui)->layer : 0;
}

/* Resolve a ROW_CUSTOM width spec: > 1 is pixels, <= 1 a fraction of content. */
static int resolve_width(const UiLayout *L, float spec)
{
    if (spec > 1.0f)
        return (int)(spec + 0.5f);
    return (int)(spec * (float)L->content.w + 0.5f);
}

/* Compute the next cell rect for the current row without mutating the cursor. */
static UiRect cell_rect(UiContext *ui, const UiLayout *L)
{
    int sx = ui->style.spacing_x;
    int x, w;
    switch (L->mode)
    {
    case ROW_STATIC:
        w = L->item_w;
        x = L->content.x + L->col * (L->item_w + sx);
        break;
    case ROW_CUSTOM:
        w = resolve_width(L, L->widths[L->col]);
        x = L->row_x;
        break;
    case ROW_DYNAMIC:
    default:
    {
        int gaps = sx * (L->columns - 1);
        int cell = (L->content.w - gaps) / (L->columns > 0 ? L->columns : 1);
        w = cell;
        x = L->content.x + L->col * (cell + sx);
        break;
    }
    }
    return zui_rect(x, L->cursor_y, w, L->row_height);
}

UiRect ui_layout_next(UiContext *ui)
{
    UiLayout *L = ui_top(ui);
    UiRect r = cell_rect(ui, L);

    if (L->mode == ROW_CUSTOM)
        L->row_x += r.w + ui->style.spacing_x;
    L->col++;

    if (L->col >= L->columns) /* row finished: drop to the next line */
    {
        L->col = 0;
        L->cursor_y += L->row_height + ui->style.spacing_y;
        L->row_x = L->content.x;
    }
    return r;
}

UiRect zui_layout_peek(UiContext *ui)
{
    return cell_rect(ui, ui_top(ui));
}

/* Begin a fresh row, flushing any partially filled previous row. */
static UiLayout *begin_row(UiContext *ui, int height)
{
    UiLayout *L = ui_top(ui);
    if (L->col != 0) /* previous row under-filled: move to a new line */
    {
        L->col = 0;
        L->cursor_y += L->row_height + ui->style.spacing_y;
        L->row_x = L->content.x;
    }
    L->row_height = height > 0 ? height : ui->style.row_height;
    L->row_x = L->content.x;
    return L;
}

void zui_row(UiContext *ui, int height, int columns)
{
    UiLayout *L = begin_row(ui, height);
    L->mode = ROW_DYNAMIC;
    L->columns = columns < 1 ? 1 : columns;
}

void zui_row_static(UiContext *ui, int height, int item_w, int columns)
{
    UiLayout *L = begin_row(ui, height);
    L->mode = ROW_STATIC;
    L->columns = columns < 1 ? 1 : columns;
    L->item_w = item_w;
}

void zui_row_begin(UiContext *ui, int height, int columns)
{
    UiLayout *L = begin_row(ui, height);
    L->mode = ROW_CUSTOM;
    L->columns = columns < 1 ? 1 : (columns > UI_MAX_COLS ? UI_MAX_COLS : columns);
    L->col = 0;
    for (int i = 0; i < UI_MAX_COLS; ++i)
        L->widths[i] = 0;
    /* widths get filled by zui_row_push; track how many via item_w (reused). */
    L->item_w = 0;
}

void zui_row_push(UiContext *ui, float width)
{
    UiLayout *L = ui_top(ui);
    if (L->item_w < UI_MAX_COLS)
        L->widths[L->item_w++] = width;
}

void zui_row_end(UiContext *ui)
{
    (void)ui; /* the row auto-closes when its cells are consumed */
}
