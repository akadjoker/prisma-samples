#include "preedit.h"

#include <stdbool.h>
#include <string.h>

void preedit_clear(Preedit *p)
{
    p->len = 0;
    p->caret = 0;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void preedit_draw(Preedit *p, int caret, int first, int length, const uint32_t *ins, int ins_len)
{
    first = clampi(first, 0, p->len);
    length = clampi(length, 0, p->len - first);
    if (ins_len < 0 || !ins)
        ins_len = 0;

    /* what stays after the replaced range, then the new text, then that tail */
    uint32_t tail[PREEDIT_CAP];
    int tail_len = p->len - first - length;
    memcpy(tail, p->text + first + length, (size_t)tail_len * sizeof tail[0]);

    int n = first;
    for (int i = 0; i < ins_len && n < PREEDIT_CAP; i++)
        p->text[n++] = ins[i];
    for (int i = 0; i < tail_len && n < PREEDIT_CAP; i++)
        p->text[n++] = tail[i];
    p->len = n;
    p->caret = clampi(caret, 0, p->len);
}

static int encode(uint32_t cp, char *out)
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
    out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

int preedit_to_utf8(const Preedit *p, char *out, int cap, int *cursor_bytes)
{
    int used = 0, cursor = -1;
    if (cap < 1)
    {
        if (cursor_bytes)
            *cursor_bytes = 0;
        return 0;
    }
    for (int i = 0; i <= p->len; i++)
    {
        if (i == p->caret)
            cursor = used;
        if (i == p->len)
            break;
        char buf[4];
        uint32_t cp = p->text[i] > 0x10FFFF || (p->text[i] >= 0xD800 && p->text[i] < 0xE000) ? 0xFFFD : p->text[i];
        int n = encode(cp, buf);
        if (used + n > cap - 1)
            break; /* does not fit: stop on the boundary */
        memcpy(out + used, buf, (size_t)n);
        used += n;
    }
    out[used] = '\0';
    if (cursor_bytes)
        *cursor_bytes = cursor < 0 ? used : cursor;
    return used;
}

int preedit_from_utf8(const char *s, int n, uint32_t *out, int cap)
{
    int count = 0;
    const unsigned char *p = (const unsigned char *)s;
    for (int i = 0; i < n && count < cap;)
    {
        uint32_t cp;
        int len;
        if (p[i] < 0x80)
            cp = p[i], len = 1;
        else if ((p[i] >> 5) == 0x6)
            cp = p[i] & 0x1F, len = 2;
        else if ((p[i] >> 4) == 0xE)
            cp = p[i] & 0x0F, len = 3;
        else if ((p[i] >> 3) == 0x1E)
            cp = p[i] & 0x07, len = 4;
        else
        {
            out[count++] = 0xFFFD;
            i++;
            continue;
        }
        bool bad = i + len > n;
        for (int k = 1; !bad && k < len; k++)
        {
            if ((p[i + k] & 0xC0) != 0x80)
                bad = true;
            else
                cp = (cp << 6) | (p[i + k] & 0x3F);
        }
        if (bad)
        {
            out[count++] = 0xFFFD;
            i++;
            continue;
        }
        out[count++] = cp;
        i += len;
    }
    return count;
}
