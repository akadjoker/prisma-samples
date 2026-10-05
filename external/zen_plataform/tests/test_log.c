/*
 * test_log.c - level filter, custom sink, truncation, and the platform's own
 * error reports reaching the log at DEBUG level.
 */
#include "platform.h"
#include "error_internal.h"

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

static struct
{
    int count;
    LogLevel level;
    char text[2048];
    void *user;
} g_got;

static void sink(LogLevel level, const char *msg, void *user)
{
    g_got.count++;
    g_got.level = level;
    g_got.user = user;
    snprintf(g_got.text, sizeof g_got.text, "%s", msg);
}

int main(void)
{
    int tag = 0;
    log_set_callback(sink, &tag);

    CHECK(log_get_level() == LOGLEVEL_INFO); /* the default */

    log_debug("hidden");
    CHECK(g_got.count == 0);

    log_info("loaded %d assets from %s", 3, "pack");
    CHECK(g_got.count == 1);
    CHECK(g_got.level == LOGLEVEL_INFO);
    CHECK(strcmp(g_got.text, "loaded 3 assets from pack") == 0);
    CHECK(g_got.user == &tag);

    log_warn("w");
    CHECK(g_got.level == LOGLEVEL_WARN);
    log_error("e");
    CHECK(g_got.level == LOGLEVEL_ERROR && g_got.count == 3);

    log_set_level(LOGLEVEL_ERROR);
    log_warn("dropped");
    CHECK(g_got.count == 3);
    log_error("kept");
    CHECK(g_got.count == 4);

    log_set_level(LOGLEVEL_OFF);
    log_error("silent");
    CHECK(g_got.count == 4);

    /* the platform reports its errors at DEBUG */
    log_set_level(LOGLEVEL_INFO);
    error_set("cannot open %s", "x.bin");
    CHECK(g_got.count == 4);
    CHECK(strcmp(platform_get_error(), "cannot open x.bin") == 0);
    log_set_level(LOGLEVEL_DEBUG);
    error_set("cannot open %s", "y.bin");
    CHECK(g_got.count == 5);
    CHECK(g_got.level == LOGLEVEL_DEBUG);
    CHECK(strcmp(g_got.text, "cannot open y.bin") == 0);

    /* a long line is cut, still terminated */
    char big[3000];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    log_error("%s", big);
    CHECK(strlen(g_got.text) == 1023);

    /* NULL restores the default sink: no crash, nothing reaches ours */
    int before = g_got.count;
    log_set_callback(NULL, NULL);
    log_set_level(LOGLEVEL_OFF);
    log_error("not seen");
    CHECK(g_got.count == before);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
