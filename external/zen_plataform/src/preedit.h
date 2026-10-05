/*
 * preedit.h - the text an input method is composing, kept as code points and edited
 * the way XIM describes it: replace `length` characters from `first` by new text.
 * Pure logic, no OS: backends feed it and read UTF-8 back.
 */
#ifndef PREEDIT_H
#define PREEDIT_H

#include <stdint.h>

#define PREEDIT_CAP 256

typedef struct
{
    uint32_t text[PREEDIT_CAP];
    int len;
    int caret; /* in characters */
} Preedit;

void preedit_clear(Preedit *p);

/* Replace chars [first, first + length) with ins[0..ins_len). Out-of-range values are
   clamped; text beyond PREEDIT_CAP is dropped. The caret is set (clamped). */
void preedit_draw(Preedit *p, int caret, int first, int length, const uint32_t *ins, int ins_len);

/* UTF-8 of the whole text into out (NUL-terminated, at most cap bytes with the NUL,
   cut on a character boundary). *cursor_bytes (may be NULL) is the caret as a byte
   offset in the result. Returns the number of bytes written, without the NUL. */
int preedit_to_utf8(const Preedit *p, char *out, int cap, int *cursor_bytes);

/* Decode UTF-8 into code points; a malformed byte becomes U+FFFD. Returns the count. */
int preedit_from_utf8(const char *s, int n, uint32_t *out, int cap);

#endif /* PREEDIT_H */
