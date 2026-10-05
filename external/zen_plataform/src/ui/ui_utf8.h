/*
 * ui_utf8.h - minimal UTF-8 decode/encode, private to zen_ui.
 *
 * Decoding is permissive: malformed bytes yield U+FFFD and advance by one so a
 * bad stream never stalls. This is all the toolkit needs to turn label bytes
 * into codepoints for the font/emoji lookup.
 */
#ifndef ZEN_UI_UTF8_H
#define ZEN_UI_UTF8_H

#include <stdint.h>

#define UI_UTF8_REPLACEMENT 0xFFFDu

/* Decode one codepoint from s (len bytes available). Writes it to *cp and
   returns the number of bytes consumed (>= 1). At end of string returns 0. */
int ui_utf8_decode(const char *s, int len, uint32_t *cp);

/* Encode cp into out (needs up to 4 bytes). Returns bytes written. */
int ui_utf8_encode(uint32_t cp, char out[4]);

/* Number of codepoints in the first len bytes. */
int ui_utf8_count(const char *s, int len);

#endif /* ZEN_UI_UTF8_H */
