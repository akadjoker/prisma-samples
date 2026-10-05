/*
 * test_preedit.c - the composition buffer an input method edits (XIM preedit
 * semantics), and its UTF-8 in and out.
 */
#include "../src/preedit.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
            g_pass++;                                              \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static void ins(Preedit *p, int caret, int first, int length, const char *utf8)
{
    uint32_t cps[64];
    int n = preedit_from_utf8(utf8, (int)strlen(utf8), cps, 64);
    preedit_draw(p, caret, first, length, cps, n);
}

static const char *text(const Preedit *p, int *cursor)
{
    static char buf[512];
    preedit_to_utf8(p, buf, sizeof buf, cursor);
    return buf;
}

static void test_typing(void)
{
    Preedit p;
    preedit_clear(&p);
    int cur;

    /* pinyin: n, i, then the method replaces "ni" with the character */
    ins(&p, 1, 0, 0, "n");
    CHECK(strcmp(text(&p, &cur), "n") == 0 && cur == 1);
    ins(&p, 2, 1, 0, "i");
    CHECK(strcmp(text(&p, &cur), "ni") == 0 && cur == 2);
    ins(&p, 1, 0, 2, "\xE4\xBD\xA0"); /* 你 */
    CHECK(strcmp(text(&p, &cur), "\xE4\xBD\xA0") == 0);
    CHECK(cur == 3); /* one character: 3 bytes */
    CHECK(p.len == 1 && p.caret == 1);

    /* insert in the middle, delete from the middle */
    ins(&p, 1, 0, 0, "ab");
    CHECK(strcmp(text(&p, &cur), "ab\xE4\xBD\xA0") == 0 && p.len == 3);
    preedit_draw(&p, 1, 1, 1, NULL, 0); /* delete "b" */
    CHECK(strcmp(text(&p, &cur), "a\xE4\xBD\xA0") == 0 && p.len == 2 && cur == 1);

    /* an empty text from the method ends the composition */
    preedit_draw(&p, 0, 0, p.len, NULL, 0);
    CHECK(p.len == 0 && strcmp(text(&p, &cur), "") == 0 && cur == 0);
}

static void test_clamping(void)
{
    Preedit p;
    preedit_clear(&p);
    int cur;
    ins(&p, 3, 0, 0, "abc");

    ins(&p, 0, 99, 5, "X"); /* start beyond the end: appended */
    CHECK(strcmp(text(&p, &cur), "abcX") == 0);
    ins(&p, 0, -4, 1, "Y"); /* a negative start is the beginning */
    CHECK(strcmp(text(&p, &cur), "YbcX") == 0);
    ins(&p, 1, 1, 100, "Z"); /* a length past the end stops at it */
    CHECK(strcmp(text(&p, &cur), "YZ") == 0);
    preedit_draw(&p, 50, 0, 0, NULL, 0); /* a caret past the end is the end */
    CHECK(p.caret == 2);
    preedit_draw(&p, -3, 0, 0, NULL, 0);
    CHECK(p.caret == 0);
    preedit_draw(&p, 0, 0, 0, NULL, -5); /* a negative count inserts nothing */
    CHECK(p.len == 2);

    /* the buffer has a capacity: the excess is dropped, not written past the end */
    preedit_clear(&p);
    uint32_t many[PREEDIT_CAP + 50];
    for (int i = 0; i < PREEDIT_CAP + 50; i++)
        many[i] = 'a';
    preedit_draw(&p, 0, 0, 0, many, PREEDIT_CAP + 50);
    CHECK(p.len == PREEDIT_CAP);
}

static void test_utf8(void)
{
    char out[16];
    Preedit p;
    preedit_clear(&p);
    int cur;

    /* cut to the buffer on a character boundary */
    ins(&p, 3, 0, 0, "\xE4\xBD\xA0\xE5\xA5\xBD!"); /* 你好! */
    int n = preedit_to_utf8(&p, out, 7, &cur); /* room for 6 bytes + NUL: two characters */
    CHECK(n == 6 && strcmp(out, "\xE4\xBD\xA0\xE5\xA5\xBD") == 0);
    n = preedit_to_utf8(&p, out, 6, &cur); /* 5 bytes: only one character fits */
    CHECK(n == 3 && strcmp(out, "\xE4\xBD\xA0") == 0);
    n = preedit_to_utf8(&p, out, 1, &cur);
    CHECK(n == 0 && out[0] == '\0');
    CHECK(preedit_to_utf8(&p, out, 0, &cur) == 0 && cur == 0);

    /* the caret is a byte offset in the result */
    p.caret = 2;
    preedit_to_utf8(&p, out, sizeof out, &cur);
    CHECK(cur == 6);
    p.caret = 0;
    preedit_to_utf8(&p, out, sizeof out, &cur);
    CHECK(cur == 0);

    /* astral characters (4 bytes) and invalid code points */
    preedit_clear(&p);
    uint32_t cps[] = {0x1F600, 0xD800, 0x110000, 'x'};
    preedit_draw(&p, 4, 0, 0, cps, 4);
    preedit_to_utf8(&p, out, sizeof out, &cur);
    CHECK(strncmp(out, "\xF0\x9F\x98\x80", 4) == 0);               /* the emoji */
    CHECK(strncmp(out + 4, "\xEF\xBF\xBD\xEF\xBF\xBD", 6) == 0);   /* a surrogate and out of range -> U+FFFD */
    CHECK(out[10] == 'x' && cur == 11);

    /* decoding: malformed bytes become U+FFFD and decoding goes on */
    uint32_t dec[8];
    int c = preedit_from_utf8("a\xE4\xBD\xA0z", 5, dec, 8);
    CHECK(c == 3 && dec[0] == 'a' && dec[1] == 0x4F60 && dec[2] == 'z');
    c = preedit_from_utf8("a\xFFz", 3, dec, 8);
    CHECK(c == 3 && dec[1] == 0xFFFD && dec[2] == 'z');
    c = preedit_from_utf8("\xE4\xBD", 2, dec, 8); /* truncated sequence */
    CHECK(c == 2 && dec[0] == 0xFFFD);
    c = preedit_from_utf8("abcdef", 6, dec, 3); /* capacity */
    CHECK(c == 3);
}

int main(void)
{
    test_typing();
    test_clamping();
    test_utf8();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
