/*
 * clipboard_mem.h - an in-process clipboard store for the backends that have no
 * system clipboard to talk to (fake, Android, web). Header-only and static: each
 * backend that includes it gets its own copy of the state.
 */
#ifndef CLIPBOARD_MEM_H
#define CLIPBOARD_MEM_H

#include "platform.h"
#include "mime_util.h"

#include <stdlib.h>
#include <string.h>

#define CLIPMEM_MAX 8

typedef struct
{
    char *mime;
    uint8_t *data;
    size_t size;
} ClipMemItem;

static ClipMemItem g_clipmem[CLIPMEM_MAX];
static int g_clipmem_count;

static void clipmem_clear(void)
{
    for (int i = 0; i < g_clipmem_count; i++)
    {
        free(g_clipmem[i].mime);
        free(g_clipmem[i].data);
    }
    g_clipmem_count = 0;
}

static bool clipmem_set(const ClipboardItem *items, int count)
{
    clipmem_clear();
    if (count > CLIPMEM_MAX)
        count = CLIPMEM_MAX;
    for (int i = 0; i < count; i++)
    {
        ClipMemItem *it = &g_clipmem[g_clipmem_count];
        size_t mlen = strlen(items[i].mime);
        it->mime = malloc(mlen + 1);
        it->data = malloc(items[i].size + 1);
        if (!it->mime || !it->data)
        {
            free(it->mime);
            free(it->data);
            clipmem_clear();
            return false;
        }
        memcpy(it->mime, items[i].mime, mlen + 1);
        if (items[i].size)
            memcpy(it->data, items[i].data, items[i].size);
        it->data[items[i].size] = 0;
        it->size = items[i].size;
        g_clipmem_count++;
    }
    return true;
}

static const ClipMemItem *clipmem_find(const char *mime)
{
    for (int i = 0; i < g_clipmem_count; i++)
        if (clip_mime_equal(g_clipmem[i].mime, mime))
            return &g_clipmem[i];
    return NULL;
}

static bool clipmem_has(const char *mime)
{
    return clipmem_find(mime) != NULL;
}

static void *clipmem_get(const char *mime, size_t *size)
{
    const ClipMemItem *it = clipmem_find(mime);
    if (!it)
        return NULL;
    uint8_t *copy = malloc(it->size + 1);
    if (!copy)
        return NULL;
    memcpy(copy, it->data, it->size + 1);
    *size = it->size;
    return copy;
}

#endif /* CLIPBOARD_MEM_H */
