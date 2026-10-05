/*
 * test_dialog.c - the zenity / kdialog / xdg-open plumbing, with stand-in programs.
 * The test puts scripts called zenity, kdialog and xdg-open alone on PATH. Each logs
 * its arguments and prints a canned answer, so the arguments the platform builds and
 * the way it reads the answer are checked without opening a window.
 */
#define _GNU_SOURCE
#include "platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static char g_dir[256], g_log[300];

static void write_stub(const char *name)
{
    char path[300];
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    FILE *f = fopen(path, "w");
    fputs("#!/bin/sh\n"
          "printf '%s\\n' \"$@\" > \"$ZEN_STUB_LOG\"\n"
          "if [ -n \"$ZEN_STUB_OUT\" ]; then printf '%b' \"$ZEN_STUB_OUT\"; fi\n"
          "exit ${ZEN_STUB_EXIT:-0}\n",
          f);
    fclose(f);
    chmod(path, 0755);
}

static void setup(void)
{
    snprintf(g_dir, sizeof g_dir, "/tmp/zen_dialog_test_XXXXXX");
    if (!mkdtemp(g_dir))
        exit(1);
    snprintf(g_log, sizeof g_log, "%s/log.txt", g_dir);
    write_stub("zenity");
    write_stub("xdg-open");
    setenv("PATH", g_dir, 1);
    setenv("ZEN_STUB_LOG", g_log, 1);
}

static void answer(const char *out, int exit_code)
{
    setenv("ZEN_STUB_OUT", out ? out : "", 1);
    char buf[16];
    snprintf(buf, sizeof buf, "%d", exit_code);
    setenv("ZEN_STUB_EXIT", buf, 1);
    unlink(g_log);
}

/* did the stub get this exact argument? */
static bool logged(const char *arg)
{
    FILE *f = fopen(g_log, "r");
    if (!f)
        return false;
    char line[2048];
    bool found = false;
    while (fgets(line, sizeof line, f))
    {
        line[strcspn(line, "\n")] = '\0';
        found |= strcmp(line, arg) == 0;
    }
    fclose(f);
    return found;
}

static bool wait_logged(const char *arg)
{
    for (int i = 0; i < 100; i++)
    {
        if (logged(arg))
            return true;
        usleep(20000);
    }
    return false;
}

static void test_messages(void)
{
    answer("", 0);
    CHECK(message_box(NULL, MESSAGE_INFO, "Title", "Hello <b>there</b>"));
    CHECK(platform_get_error()[0] == '\0');
    CHECK(logged("--info") && logged("--title=Title") && logged("--text=Hello <b>there</b>"));
    CHECK(logged("--no-markup")); /* the text is shown as written */

    CHECK(message_box(NULL, MESSAGE_WARNING, "W", "w"));
    CHECK(logged("--warning"));
    CHECK(message_box(NULL, MESSAGE_ERROR, "E", "e"));
    CHECK(logged("--error"));

    answer("", 0);
    CHECK(confirm_box(NULL, "Q", "Sure?") == 1);
    CHECK(logged("--question") && logged("--text=Sure?"));
    answer("", 1);
    CHECK(confirm_box(NULL, "Q", "Sure?") == 0);
    answer("", 5); /* neither yes nor no */
    CHECK(confirm_box(NULL, "Q", "Sure?") == -1);
}

static void test_files(void)
{
    char out[256];
    FileFilter filters[] = {{"Images", "*.png;*.jpg"}, {"All files", "*"}};

    answer("/home/user/pic.png\n", 0);
    CHECK(dialog_open_file(NULL, "Open", "/home/user", filters, 2, out, sizeof out));
    CHECK(strcmp(out, "/home/user/pic.png") == 0);
    CHECK(logged("--file-selection") && logged("--title=Open") && logged("--filename=/home/user"));
    CHECK(logged("--file-filter=Images | *.png *.jpg"));
    CHECK(logged("--file-filter=All files | *"));
    CHECK(!logged("--save") && !logged("--directory") && !logged("--multiple"));

    /* cancelled: false, and no error */
    platform_clear_error();
    answer("", 1);
    CHECK(!dialog_open_file(NULL, "Open", NULL, NULL, 0, out, sizeof out));
    CHECK(platform_get_error()[0] == '\0');

    /* a path that does not fit is an error */
    answer("/a/very/long/path/that/will/not/fit/in/the/small/buffer\n", 0);
    char tiny[8];
    CHECK(!dialog_open_file(NULL, "Open", NULL, NULL, 0, tiny, sizeof tiny));
    CHECK(platform_get_error()[0] != '\0');

    answer("/tmp/new.txt\n", 0);
    CHECK(dialog_save_file(NULL, "Save", NULL, NULL, 0, out, sizeof out));
    CHECK(strcmp(out, "/tmp/new.txt") == 0);
    CHECK(logged("--save") && logged("--confirm-overwrite"));

    answer("/home/user/projects\n", 0);
    CHECK(dialog_pick_folder(NULL, "Folder", NULL, out, sizeof out));
    CHECK(strcmp(out, "/home/user/projects") == 0);
    CHECK(logged("--directory"));

    answer("/a/one.png\n/a/two.png\n/a/three.png\n", 0);
    CHECK(dialog_open_files(NULL, "Many", NULL, NULL, 0, out, sizeof out) == 3);
    CHECK(strcmp(out, "/a/one.png\n/a/two.png\n/a/three.png") == 0);
    CHECK(logged("--multiple") && logged("--separator="));

    answer("", 1);
    CHECK(dialog_open_files(NULL, "Many", NULL, NULL, 0, out, sizeof out) == 0);
}

static void test_url(void)
{
    answer("", 0);
    CHECK(open_url("https://example.com/path?q=1&r=2"));
    CHECK(wait_logged("https://example.com/path?q=1&r=2"));

    /* refused: an option, an empty string, control characters */
    unlink(g_log);
    CHECK(!open_url("--evil"));
    CHECK(!open_url(""));
    CHECK(!open_url(NULL));
    CHECK(!open_url("http://x/\n--evil"));
    usleep(150000);
    CHECK(!logged("--evil")); /* none of them reached the program */
}

static void test_fallbacks(void)
{
    /* kdialog when zenity is missing */
    char zenity[300];
    snprintf(zenity, sizeof zenity, "%s/zenity", g_dir);
    unlink(zenity);
    write_stub("kdialog");

    char out[256];
    FileFilter filters[] = {{"Images", "*.png;*.jpg"}, {"Text", "*.txt"}};
    answer("/k/pic.png\n", 0);
    CHECK(dialog_open_file(NULL, "Open", "/k", filters, 2, out, sizeof out));
    CHECK(strcmp(out, "/k/pic.png") == 0);
    CHECK(logged("--getopenfilename") && logged("/k"));
    CHECK(logged("*.png *.jpg|Images\n*.txt|Text") || logged("*.png *.jpg|Images")); /* one filter argument */

    answer("", 0);
    CHECK(message_box(NULL, MESSAGE_ERROR, "T", "m"));
    CHECK(logged("--error"));
    answer("", 1);
    CHECK(confirm_box(NULL, "T", "m") == 0);
    CHECK(logged("--yesno"));

    /* neither program: an error, not a hang */
    char kd[300];
    snprintf(kd, sizeof kd, "%s/kdialog", g_dir);
    unlink(kd);
    platform_clear_error();
    CHECK(!message_box(NULL, MESSAGE_INFO, "T", "m"));
    CHECK(platform_get_error()[0] != '\0');
    CHECK(confirm_box(NULL, "T", "m") == -1);
    CHECK(!dialog_open_file(NULL, "T", NULL, NULL, 0, out, sizeof out));
    CHECK(platform_get_error()[0] != '\0');
}

static void test_parent(void)
{
    /* a parent window is passed on as the dialog's owner (needs a display) */
    write_stub("zenity");
    if (!platform_init())
        return;
    WindowConfig cfg = {.title = "parent", .width = 80, .height = 60, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    PlatformWindow *w = window_create(&cfg);
    if (w)
    {
        char want[64];
        snprintf(want, sizeof want, "--attach=%lu", (unsigned long)(uintptr_t)window_native_handle(w, NATIVE_WINDOW));
        answer("", 0);
        CHECK(message_box(w, MESSAGE_INFO, "T", "m"));
        CHECK(logged("--modal") && logged(want));
        window_destroy(w);
    }
    platform_shutdown();
}

int main(void)
{
    setup();
    test_messages();
    test_files();
    test_url();
    test_parent();
    test_fallbacks();
    const char *files[] = {"zenity", "kdialog", "xdg-open", "log.txt"};
    for (int i = 0; i < 4; i++)
    {
        char path[300];
        snprintf(path, sizeof path, "%s/%s", g_dir, files[i]);
        unlink(path);
    }
    rmdir(g_dir);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
