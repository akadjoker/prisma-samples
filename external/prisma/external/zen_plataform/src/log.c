/*
 * log.c - leveled logging with a replaceable sink.
 *
 * The default sink writes "[LEVEL] message" to stderr (the browser console on
 * the web), to logcat on Android, and also to the debugger output on Windows
 * when one is attached. Level and sink are plain globals: set them before
 * starting threads. Emitting a message is safe from any thread.
 */
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>

#if defined(__ANDROID__)
#include <android/log.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#define LOG_LINE_MAX 1024

static LogLevel g_level = LOGLEVEL_INFO;
static LogCallback g_callback;
static void *g_user;

static const char *level_name(LogLevel l)
{
    switch (l)
    {
    case LOGLEVEL_DEBUG:
        return "DEBUG";
    case LOGLEVEL_INFO:
        return "INFO";
    case LOGLEVEL_WARN:
        return "WARN";
    case LOGLEVEL_ERROR:
        return "ERROR";
    default:
        return "?";
    }
}

static void default_sink(LogLevel level, const char *msg)
{
#if defined(__ANDROID__)
    static const int prio[] = {ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR};
    __android_log_write(prio[level], "zen_platform", msg);
#else
    fprintf(stderr, "[%s] %s\n", level_name(level), msg);
#if defined(_WIN32)
    if (IsDebuggerPresent())
    {
        OutputDebugStringA(msg);
        OutputDebugStringA("\n");
    }
#endif
#endif
}

void log_set_level(LogLevel level)
{
    g_level = level;
}

LogLevel log_get_level(void)
{
    return g_level;
}

void log_set_callback(LogCallback cb, void *user)
{
    g_callback = cb;
    g_user = user;
}

void log_message(LogLevel level, const char *fmt, ...)
{
    if (level < LOGLEVEL_DEBUG || level >= LOGLEVEL_OFF || level < g_level)
        return;
    char buf[LOG_LINE_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap); /* a longer line is cut, still NUL-terminated */
    va_end(ap);
    if (g_callback)
        g_callback(level, buf, g_user);
    else
        default_sink(level, buf);
}
