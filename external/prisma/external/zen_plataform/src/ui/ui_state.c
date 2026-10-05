/*
 * ui_state.c - stable widget ids (FNV-1a + id stack) and the generic id-keyed
 *              state store (open-addressed hash).
 *
 * Widget identity must survive reordering between frames; the per-frame counter
 * (next_id) breaks as soon as a new widget is inserted above.  Hashed labels
 * scoped by the parent id stack fix this, exactly as ImGui does it.
 */
#include "ui_internal.h"
#include <string.h>
#include <stdlib.h>

/* ====================================================================== */
/*  FNV-1a hash (public domain)                                            */
/* ====================================================================== */

#define FNV_OFFSET 0x811c9dc5u
#define FNV_PRIME  0x01000193u

static uint32_t fnv_1a(const char *s, int len, uint32_t seed)
{
    uint32_t h = seed;
    for (int i = 0; i < len; ++i)
    {
        h ^= (uint8_t)s[i];
        h *= FNV_PRIME;
    }
    return h;
}

/* ====================================================================== */
/*  ID stack                                                               */
/* ====================================================================== */

void zui_push_id(UiContext *ui, const char *label)
{
    if (ui->id_sp < UI_ID_STACK_MAX)
    {
        ZuiId parent = ui->id_sp > 0 ? ui->id_stack[ui->id_sp - 1] : FNV_OFFSET;
        ui->id_stack[ui->id_sp] = fnv_1a(label, (int)strlen(label), parent);
        ui->id_sp++;
    }
}

void zui_push_id_i(UiContext *ui, int i)
{
    if (ui->id_sp < UI_ID_STACK_MAX)
    {
        ZuiId parent = ui->id_sp > 0 ? ui->id_stack[ui->id_sp - 1] : FNV_OFFSET;
        /* Hash the integer as 4 little-endian bytes */
        uint32_t h = parent;
        for (int b = 0; b < 4; ++b)
        {
            h ^= (uint8_t)(i >> (b * 8));
            h *= FNV_PRIME;
        }
        ui->id_stack[ui->id_sp] = h;
        ui->id_sp++;
    }
}

void zui_pop_id(UiContext *ui)
{
    if (ui->id_sp > 0)
        ui->id_sp--;
}

/* ====================================================================== */
/*  Label -> id (strip "##" / "###" conventions)                           */
/* ====================================================================== */

ZuiId zui_id(UiContext *ui, const char *label)
{
    /* "###fixed": text may vary, id is everything after "###" */
    const char *hash_part = NULL;
    int hash_len = 0;
    const char *display_part = label;
    int display_len = 0;

    if (label[0] == '#' && label[1] == '#' && label[2] == '#')
    {
        /* "###id": no visible text, only id */
        hash_part = label + 3;
        hash_len = (int)strlen(hash_part);
        display_part = NULL;
        display_len = 0;
    }
    else
    {
        /* Look for "##" in the label */
        const char *sep = strstr(label, "##");
        if (sep && sep[2] != '#')
        {
            display_part = label;
            display_len = (int)(sep - label);
            hash_part = sep + 2;
            hash_len = (int)strlen(hash_part);
        }
        else
        {
            /* No separator: the whole label is both display and id */
            display_part = label;
            display_len = (int)strlen(label);
        }
    }

    /* The id is computed from the hash_part only, scoped by the parent stack.
       If no hash_part was extracted, hash the whole label. */
    ZuiId parent = ui->id_sp > 0 ? ui->id_stack[ui->id_sp - 1] : FNV_OFFSET;
    const char *src = hash_part ? hash_part : label;
    int len = hash_part ? hash_len : (int)strlen(label);
    ZuiId id = fnv_1a(src, len, parent);

    (void)display_part;
    (void)display_len;

    return id;
}

/* ====================================================================== */
/*  State store (open-addressed hash, never evicts)                       */
/* ====================================================================== */

/* A mixing step so the store slot index is not just id % size */
static uint32_t store_hash(ZuiId id)
{
    uint32_t h = (uint32_t)id;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

#define TOMBSTONE ((ZuiId)0xFFFFFFFFu)

static int store_find(UiContext *ui, ZuiId id, bool want_empty)
{
    uint32_t h = store_hash(id);
    for (int i = 0; i < UI_STORE_SIZE; ++i)
    {
        int slot = (int)((h + (uint32_t)i) % (uint32_t)UI_STORE_SIZE);
        if (ui->store[slot].tag == 0 || ui->store[slot].id == TOMBSTONE)
        {
            if (want_empty && ui->store[slot].tag == 0)
                return slot;
            if (!want_empty && ui->store[slot].id == id)
                return -1; /* not found, but this is the tombstone case */
            if (ui->store[slot].tag == 0)
                return -1; /* true empty */
            /* tombstone: keep probing */
            if (ui->store[slot].id == TOMBSTONE)
                continue;
        }
        if (ui->store[slot].id == id)
            return slot;
    }
    return -1;
}

static int store_find_or_insert(UiContext *ui, ZuiId id)
{
    uint32_t h = store_hash(id);
    int first_tomb = -1;
    for (int i = 0; i < UI_STORE_SIZE; ++i)
    {
        int slot = (int)((h + (uint32_t)i) % (uint32_t)UI_STORE_SIZE);
        if (ui->store[slot].id == id)
            return slot;
        if (ui->store[slot].id == TOMBSTONE && first_tomb < 0)
            first_tomb = slot;
        if (ui->store[slot].tag == 0)
        {
            /* Empty slot; use first tombstone if we found one, else this empty */
            int use = first_tomb >= 0 ? first_tomb : slot;
            ui->store[use].id = id;
            ui->store[use].key = store_hash(id);
            ui->store[use].tag = 0;
            ui->store[use].vi = 0;
            ui->store[use].vf = 0.0f;
            ui->store[use].vp = NULL;
            return use;
        }
    }
    /* Table full; defensive fallback: overwrite the first slot that doesn't match */
    if (first_tomb >= 0)
    {
        ui->store[first_tomb].id = id;
        ui->store[first_tomb].key = store_hash(id);
        ui->store[first_tomb].tag = 0;
        ui->store[first_tomb].vi = 0;
        ui->store[first_tomb].vf = 0.0f;
        ui->store[first_tomb].vp = NULL;
        return first_tomb;
    }
    return 0; /* truly full; reuse slot 0 */
}

int *zui_state_int(UiContext *ui, ZuiId id, int defv)
{
    int slot = store_find_or_insert(ui, id);
    if (ui->store[slot].tag == 0)
    {
        ui->store[slot].tag = 1;
        ui->store[slot].vi = defv;
    }
    return &ui->store[slot].vi;
}

float *zui_state_float(UiContext *ui, ZuiId id, float defv)
{
    int slot = store_find_or_insert(ui, id);
    if (ui->store[slot].tag == 0)
    {
        ui->store[slot].tag = 2;
        ui->store[slot].vf = defv;
    }
    return &ui->store[slot].vf;
}

void *zui_state_ptr(UiContext *ui, ZuiId id)
{
    int slot = store_find_or_insert(ui, id);
    if (ui->store[slot].tag == 0)
    {
        ui->store[slot].tag = 3;
        ui->store[slot].vp = NULL;
    }
    return &ui->store[slot].vp;
}

void zui_state_set_ptr(UiContext *ui, ZuiId id, void *p)
{
    int slot = store_find_or_insert(ui, id);
    ui->store[slot].tag = 3;
    ui->store[slot].vp = p;
}