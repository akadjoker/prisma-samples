/*
 * ui_font.c - TTF (stb_truetype) and 5x7 bitmap fonts behind one UiFont.
 *
 * Glyphs are rasterized lazily into an open-addressed cache keyed by codepoint.
 * Coverage is 8-bit; the renderer tints and blends it. UTF-8 decoding happens at
 * the call sites (zui_text_width, the renderer) via ui_utf8.
 */
#include "ui_font.h"
#include "ui_utf8.h"
#include "stb_truetype.h"

#include <stdlib.h>
#include <string.h>

/* Embedded default font, produced by cmake/embed_font.cmake. */
extern const unsigned char ui_font_default_data[];
extern const unsigned int  ui_font_default_size;

typedef enum { FONT_TTF, FONT_BUILTIN } FontKind;

struct UiFont
{
    FontKind kind;

    /* TTF */
    stbtt_fontinfo info;
    uint8_t       *owned_data; /* freed on destroy when we copied the bytes */
    float          scale;
    int            ascent, descent, line_gap; /* scaled to pixels */

    /* builtin 5x7 */
    int builtin_scale;

    /* glyph cache (open addressing, power-of-two, never evicts) */
    UiGlyph *slots;
    int      slot_count; /* capacity, power of two */
    int      filled;
};

/* ----------------------------------------------------- 5x7 bitmap glyphs --- */
/* Lifted from the mwidgets demo: digits, A-Z and a little punctuation. Enough
   for ASCII UI labels when no TTF is available. */

static const uint8_t g_digits[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};

static const uint8_t g_upper[26][7] = {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E},
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}};

/* Returns the 7-row pattern for cp, or NULL for a blank (space-width) glyph. */
static const uint8_t *builtin_pattern(uint32_t cp)
{
    if (cp >= 'a' && cp <= 'z')
        cp = cp - 'a' + 'A';
    if (cp >= 'A' && cp <= 'Z')
        return g_upper[cp - 'A'];
    if (cp >= '0' && cp <= '9')
        return g_digits[cp - '0'];
    switch (cp)
    {
    case ':': { static const uint8_t p[7] = {0, 0x04, 0x04, 0, 0x04, 0x04, 0}; return p; }
    case '.': { static const uint8_t p[7] = {0, 0, 0, 0, 0, 0x0C, 0x0C}; return p; }
    case '-': { static const uint8_t p[7] = {0, 0, 0, 0x1F, 0, 0, 0}; return p; }
    case '+': { static const uint8_t p[7] = {0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0}; return p; }
    case '/': { static const uint8_t p[7] = {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}; return p; }
    default: return NULL;
    }
}

/* ------------------------------------------------------------ glyph cache -- */

static void cache_init(struct UiFont *f, int cap)
{
    f->slot_count = cap;
    f->filled = 0;
    f->slots = (UiGlyph *)calloc((size_t)cap, sizeof(UiGlyph));
}

static UiGlyph *cache_find_slot(struct UiFont *f, uint32_t cp)
{
    uint32_t mask = (uint32_t)f->slot_count - 1;
    uint32_t i = (cp * 2654435761u) & mask; /* Knuth multiplicative hash */
    while (f->slots[i].used && f->slots[i].cp != cp)
        i = (i + 1) & mask;
    return &f->slots[i];
}

static void cache_grow(struct UiFont *f)
{
    UiGlyph *old = f->slots;
    int old_count = f->slot_count;
    cache_init(f, old_count * 2);
    for (int i = 0; i < old_count; ++i)
    {
        if (!old[i].used)
            continue;
        UiGlyph *dst = cache_find_slot(f, old[i].cp);
        *dst = old[i];
        ++f->filled;
    }
    free(old);
}

/* ------------------------------------------------------------ rasterizers -- */

static void raster_ttf(struct UiFont *f, uint32_t cp, UiGlyph *out)
{
    int g = stbtt_FindGlyphIndex(&f->info, (int)cp);
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f->info, g, &adv, &lsb);
    out->advance = (int)(adv * f->scale + 0.5f);

    if (g == 0) /* .notdef: keep the advance, no pixels */
        return;

    int w, h, xo, yo;
    unsigned char *bmp = stbtt_GetGlyphBitmap(&f->info, f->scale, f->scale, g, &w, &h, &xo, &yo);
    if (!bmp || w <= 0 || h <= 0)
    {
        if (bmp)
            stbtt_FreeBitmap(bmp, NULL);
        return;
    }
    out->cov = (uint8_t *)malloc((size_t)w * (size_t)h);
    if (out->cov)
    {
        memcpy(out->cov, bmp, (size_t)w * (size_t)h);
        out->w = w;
        out->h = h;
        out->xoff = xo;
        out->yoff = yo; /* relative to the baseline */
    }
    stbtt_FreeBitmap(bmp, NULL);
}

static void raster_builtin(struct UiFont *f, uint32_t cp, UiGlyph *out)
{
    int s = f->builtin_scale;
    out->advance = 6 * s; /* 5px glyph + 1px gap */
    const uint8_t *pat = builtin_pattern(cp);
    if (!pat)
        return; /* blank, advance only */

    int w = 5 * s, h = 7 * s;
    out->cov = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
    if (!out->cov)
        return;
    out->w = w;
    out->h = h;
    out->xoff = 0;
    out->yoff = -ui_font_ascent(f); /* baseline-relative: top of the glyph */
    for (int row = 0; row < 7; ++row)
        for (int col = 0; col < 5; ++col)
            if (pat[row] & (1 << (4 - col)))
                for (int dy = 0; dy < s; ++dy)
                    for (int dx = 0; dx < s; ++dx)
                        out->cov[(row * s + dy) * w + (col * s + dx)] = 255;
}

const UiGlyph *ui_font_glyph(UiFont *f, uint32_t cp)
{
    UiGlyph *slot = cache_find_slot(f, cp);
    if (slot->used)
        return slot;

    if ((f->filled + 1) * 10 >= f->slot_count * 7) /* load factor > 0.7 */
    {
        cache_grow(f);
        slot = cache_find_slot(f, cp);
    }

    memset(slot, 0, sizeof(*slot));
    slot->cp = cp;
    slot->used = true;
    ++f->filled;

    if (f->kind == FONT_TTF)
        raster_ttf(f, cp, slot);
    else
        raster_builtin(f, cp, slot);
    return slot;
}

int ui_font_ascent(const UiFont *f)
{
    return f->kind == FONT_TTF ? f->ascent : 7 * f->builtin_scale;
}

int ui_font_line_height(const UiFont *f)
{
    if (f->kind == FONT_TTF)
        return f->ascent - f->descent + f->line_gap;
    return 8 * f->builtin_scale;
}

/* --------------------------------------------------------------- public --- */

static UiFont *ttf_from_mem(const uint8_t *data, int px, uint8_t *owned)
{
    UiFont *f = (UiFont *)calloc(1, sizeof(UiFont));
    if (!f)
        return NULL;
    f->kind = FONT_TTF;
    f->owned_data = owned;
    if (!stbtt_InitFont(&f->info, data, stbtt_GetFontOffsetForIndex(data, 0)))
    {
        free(owned);
        free(f);
        return NULL;
    }
    f->scale = stbtt_ScaleForPixelHeight(&f->info, (float)px);
    int a, d, g;
    stbtt_GetFontVMetrics(&f->info, &a, &d, &g);
    f->ascent = (int)(a * f->scale + 0.5f);
    f->descent = (int)(d * f->scale - 0.5f);
    f->line_gap = (int)(g * f->scale + 0.5f);
    cache_init(f, 128);
    return f;
}

UiFont *zui_font_load_ttf_mem(const uint8_t *data, int size, int px)
{
    (void)size;
    return ttf_from_mem(data, px, NULL); /* not copied; caller keeps data alive */
}

UiFont *zui_font_load_ttf(const char *path, int px)
{
    size_t size = 0;
    uint8_t *data = file_read(path, &size);
    if (!data)
        return NULL;
    UiFont *f = ttf_from_mem(data, px, data); /* font owns the bytes now */
    if (!f)
        fs_free(data);
    return f;
}

/* Default fonts are owned by the library and cached per pixel size. */
#define DEFAULT_CACHE 8
static UiFont *g_default[DEFAULT_CACHE];
static int     g_default_px[DEFAULT_CACHE];

UiFont *zui_font_default(int px)
{
    for (int i = 0; i < DEFAULT_CACHE; ++i)
        if (g_default[i] && g_default_px[i] == px)
            return g_default[i];
    UiFont *f = ttf_from_mem(ui_font_default_data, px, NULL);
    if (!f)
        return zui_font_builtin(px / 8 < 1 ? 1 : px / 8);
    for (int i = 0; i < DEFAULT_CACHE; ++i)
        if (!g_default[i])
        {
            g_default[i] = f;
            g_default_px[i] = px;
            break;
        }
    return f;
}

UiFont *zui_font_builtin(int scale)
{
    UiFont *f = (UiFont *)calloc(1, sizeof(UiFont));
    if (!f)
        return NULL;
    f->kind = FONT_BUILTIN;
    f->builtin_scale = scale < 1 ? 1 : scale;
    cache_init(f, 128);
    return f;
}

void zui_font_free(UiFont *f)
{
    if (!f)
        return;
    for (int i = 0; i < f->slot_count; ++i)
        if (f->slots[i].used)
            free(f->slots[i].cov);
    free(f->slots);
    if (f->owned_data)
        fs_free(f->owned_data);
    free(f);
}

int zui_text_width(const UiFont *f, const char *utf8, int len)
{
    if (len < 0)
        len = (int)strlen(utf8);
    int width = 0, i = 0;
    while (i < len)
    {
        uint32_t cp;
        int adv = ui_utf8_decode(utf8 + i, len - i, &cp);
        if (adv <= 0)
            break;
        i += adv;
        width += ui_font_glyph((UiFont *)f, cp)->advance;
    }
    return width;
}

int zui_font_height(const UiFont *f)
{
    return ui_font_line_height(f);
}

/* Public glyph seam for external renderers (see zen_ui.h). Mirrors the internal
   UiGlyph but hides the cache slot bookkeeping. */
bool zui_font_glyph(const UiFont *f, uint32_t codepoint, UiGlyphInfo *out)
{
    const UiGlyph *g = ui_font_glyph((UiFont *)f, codepoint);
    if (!g || !out)
        return false;
    out->w = g->w;
    out->h = g->h;
    out->xoff = g->xoff;
    out->yoff = g->yoff;
    out->advance = g->advance;
    out->coverage = g->cov;
    return true;
}

int zui_font_ascent(const UiFont *f)
{
    return ui_font_ascent(f);
}
