#include "platform.h"
#include "error_internal.h"

#include <stdarg.h>
#include <stdio.h>

#if defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL _Thread_local
#endif

#define ERROR_CAP 512

static THREAD_LOCAL char g_error[ERROR_CAP];

bool error_set(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error, sizeof g_error, fmt, ap);
    va_end(ap);
    log_message(LOGLEVEL_DEBUG, "%s", g_error);
    return false;
}

const char *platform_get_error(void)
{
    return g_error;
}

void platform_clear_error(void)
{
    g_error[0] = '\0';
}
