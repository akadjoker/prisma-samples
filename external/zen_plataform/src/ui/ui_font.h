/*
 * ui_font.h - private font interface shared by the core and the renderers.
 *
 * A UiFont rasterizes codepoints to 8-bit coverage glyphs and caches them. Two
 * implementations sit behind the same struct: the embedded/loaded TTF path
 * (stb_truetype) and the 5x7 bitmap fallback. The renderer asks for one glyph at
 * a time; the core asks for advances to size widgets.
 */
#ifndef ZEN_UI_FONT_H
#define ZEN_UI_FONT_H

#include "zen_ui.h"

typedef struct
{
    uint32_t cp;
    int      w, h;       /* coverage bitmap dimensions (0 for blank glyphs) */
    int      xoff, yoff; /* top-left offset from the pen, baseline-relative */
    int      advance;    /* horizontal pen advance, pixels */
    uint8_t *cov;        /* w*h coverage, 0..255; NULL when w*h == 0 */
    bool     used;       /* hash slot occupied */
} UiGlyph;

/* Rasterize-on-miss lookup. The returned pointer is valid until the font is
   freed (the cache never evicts). NULL only on allocation failure. */
const UiGlyph *ui_font_glyph(UiFont *f, uint32_t cp);

int ui_font_ascent(const UiFont *f);      /* baseline offset from the top */
int ui_font_line_height(const UiFont *f); /* vertical advance between lines */

#endif /* ZEN_UI_FONT_H */
