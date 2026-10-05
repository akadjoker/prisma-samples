#ifndef ERROR_INTERNAL_H
#define ERROR_INTERNAL_H

#include <stdbool.h>

#if defined(__GNUC__) || defined(__clang__)
#define ERROR_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define ERROR_PRINTF(f, a)
#endif

bool error_set(const char *fmt, ...) ERROR_PRINTF(1, 2);

#endif /* ERROR_INTERNAL_H */
