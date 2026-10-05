#ifndef MIME_UTIL_H
#define MIME_UTIL_H

#include <stdbool.h>

/* MIME types match ignoring case and any ";parameter" tail. */
static inline bool clip_mime_equal(const char *a, const char *b)
{
    for (;; a++, b++)
    {
        char ca = *a == ';' ? '\0' : *a, cb = *b == ';' ? '\0' : *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return false;
        if (!ca)
            return true;
    }
}

#endif /* MIME_UTIL_H */
