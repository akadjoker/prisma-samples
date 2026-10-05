/*
 * ui_utf8.c - see ui_utf8.h. Branchy but tiny; no tables.
 */
#include "ui_utf8.h"

int ui_utf8_decode(const char *s, int len, uint32_t *cp)
{
    if (len <= 0)
    {
        *cp = 0;
        return 0;
    }

    const unsigned char *u = (const unsigned char *)s;
    unsigned char b0 = u[0];

    /* ASCII fast path. */
    if (b0 < 0x80)
    {
        *cp = b0;
        return 1;
    }

    int n;            /* total bytes in this sequence */
    uint32_t c;       /* accumulated codepoint */
    uint32_t lo_bound; /* smallest value legal for this length (overlong check) */

    if ((b0 & 0xE0) == 0xC0) { n = 2; c = b0 & 0x1F; lo_bound = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { n = 3; c = b0 & 0x0F; lo_bound = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { n = 4; c = b0 & 0x07; lo_bound = 0x10000; }
    else { *cp = UI_UTF8_REPLACEMENT; return 1; } /* stray continuation / 5-6 byte */

    if (n > len)
    {
        *cp = UI_UTF8_REPLACEMENT;
        return 1;
    }

    for (int i = 1; i < n; ++i)
    {
        if ((u[i] & 0xC0) != 0x80) /* expected a continuation byte */
        {
            *cp = UI_UTF8_REPLACEMENT;
            return 1; /* resync from the next byte */
        }
        c = (c << 6) | (u[i] & 0x3F);
    }

    /* Reject overlong encodings, surrogates and out-of-range. */
    if (c < lo_bound || (c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF)
    {
        *cp = UI_UTF8_REPLACEMENT;
        return n;
    }

    *cp = c;
    return n;
}

int ui_utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80)
    {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800)
    {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000)
    {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int ui_utf8_count(const char *s, int len)
{
    int n = 0;
    int i = 0;
    while (i < len)
    {
        uint32_t cp;
        int adv = ui_utf8_decode(s + i, len - i, &cp);
        if (adv <= 0)
            break;
        i += adv;
        ++n;
    }
    return n;
}
