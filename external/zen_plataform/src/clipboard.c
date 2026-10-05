/*
 * clipboard.c - the public clipboard API: text, MIME data and PNG images, over
 * the backend's three primitives.
 */
#include "platform.h"
#include "backend.h"
#include "error_internal.h"
#include "png_internal.h"

#include <stdlib.h>
#include <string.h>

static char *g_text; /* what clipboard_get last returned */

bool clipboard_set_items(const ClipboardItem *items, int count)
{
    if (count < 0 || (count > 0 && !items))
        return error_set("invalid clipboard items");
    for (int i = 0; i < count; i++)
        if (!items[i].mime || !items[i].mime[0] || (items[i].size && !items[i].data))
            return error_set("invalid clipboard item %d", i);
    return backend_clipboard_set(items, count);
}

bool clipboard_set_data(const char *mime, const void *data, size_t size)
{
    ClipboardItem it = {mime, data, size};
    return clipboard_set_items(&it, 1);
}

bool clipboard_has_data(const char *mime)
{
    return mime && mime[0] && backend_clipboard_has(mime);
}

void *clipboard_get_data(const char *mime, size_t *out_size)
{
    size_t n = 0;
    void *d = mime && mime[0] ? backend_clipboard_get(mime, &n) : NULL;
    if (out_size)
        *out_size = d ? n : 0;
    return d;
}

void clipboard_set(const char *text)
{
    if (!text)
        clipboard_set_items(NULL, 0);
    else
        clipboard_set_data(CLIPBOARD_TEXT, text, strlen(text));
}

const char *clipboard_get(void)
{
    free(g_text);
    g_text = clipboard_get_data(CLIPBOARD_TEXT, NULL);
    return g_text ? g_text : "";
}

bool clipboard_set_image(const Framebuffer *fb)
{
    size_t n;
    uint8_t *png = png_encode(fb, &n);
    if (!png)
        return error_set("cannot encode the image as PNG");
    bool ok = clipboard_set_data(CLIPBOARD_PNG, png, n);
    free(png);
    return ok;
}

bool clipboard_get_image(Framebuffer *out)
{
    size_t n;
    void *png = clipboard_get_data(CLIPBOARD_PNG, &n);
    if (!png)
        return error_set("the clipboard has no image");
    bool ok = png_decode(png, n, out);
    free(png);
    if (!ok)
        return error_set("cannot decode the clipboard image");
    return true;
}
