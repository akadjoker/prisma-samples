#include "platform.h"

#include <stdio.h>
#include <string.h>

#define GL_VERSION 0x1F02
#define GL_SAMPLE_BUFFERS 0x80A8
#define GL_SAMPLES 0x80A9
#define GL_CONTEXT_FLAGS 0x821E
#define GL_CONTEXT_PROFILE_MASK 0x9126
#define GL_MAJOR_VERSION 0x821B
#define GL_MINOR_VERSION 0x821C
#define GL_CONTEXT_CORE_PROFILE_BIT 0x1
#define GL_CONTEXT_COMPATIBILITY_PROFILE_BIT 0x2
#define GL_CONTEXT_FLAG_DEBUG_BIT 0x2

typedef void (*GetIntegerv)(unsigned name, int *out);
typedef const unsigned char *(*GetString)(unsigned name);

static GetIntegerv gl_get_integerv;
static GetString gl_get_string;
static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
        {                                                          \
            g_pass++;                                              \
        }                                                          \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static int geti(unsigned name)
{
    int v = 0;
    gl_get_integerv(name, &v);
    return v;
}

static int version(void)
{
    return geti(GL_MAJOR_VERSION) * 10 + geti(GL_MINOR_VERSION);
}

static PlatformWindow *make(GLConfig gl)
{
    WindowConfig cfg = {.title = "test_gl_x11", .width = 64, .height = 64, .gl = gl};
    PlatformWindow *w = window_create(&cfg);
    if (w)
    {
        *(void **)&gl_get_integerv = gl_proc_address("glGetIntegerv");
        *(void **)&gl_get_string = gl_proc_address("glGetString");
    }
    return w;
}

static void test_default(void)
{
    PlatformWindow *w = make((GLConfig){0});
    CHECK(w != NULL);
    if (!w)
        return;
    CHECK(version() >= 33);
    CHECK(geti(GL_CONTEXT_PROFILE_MASK) & GL_CONTEXT_CORE_PROFILE_BIT);
    CHECK(!(geti(GL_CONTEXT_FLAGS) & GL_CONTEXT_FLAG_DEBUG_BIT));
    window_set_vsync(w, true);
    window_set_vsync(w, false);
    window_swap(w);
    window_destroy(w);
}

static void test_profiles(void)
{
    PlatformWindow *w = make((GLConfig){.profile = GL_PROFILE_CORE, .major = 3, .minor = 3});
    CHECK(w != NULL);
    if (w)
    {
        CHECK(version() >= 33);
        CHECK(geti(GL_CONTEXT_PROFILE_MASK) & GL_CONTEXT_CORE_PROFILE_BIT);
        window_destroy(w);
    }

    w = make((GLConfig){.profile = GL_PROFILE_COMPAT, .major = 3, .minor = 2});
    CHECK(w != NULL);
    if (w)
    {
        CHECK(version() >= 32);
        CHECK(geti(GL_CONTEXT_PROFILE_MASK) & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT);
        window_destroy(w);
    }

    w = make((GLConfig){.profile = GL_PROFILE_ES, .major = 3, .minor = 0});
    CHECK(w != NULL);
    if (w)
    {
        const char *v = (const char *)gl_get_string(GL_VERSION);
        CHECK(v && strncmp(v, "OpenGL ES 3", 11) == 0);
        window_destroy(w);
    }

    w = make((GLConfig){.profile = GL_PROFILE_ES});
    CHECK(w != NULL);
    if (w)
    {
        const char *v = (const char *)gl_get_string(GL_VERSION);
        CHECK(v && strncmp(v, "OpenGL ES 3", 11) == 0);
        window_destroy(w);
    }
}

static void test_debug(void)
{
    PlatformWindow *w = make((GLConfig){.debug = true});
    CHECK(w != NULL);
    if (!w)
        return;
    CHECK(geti(GL_CONTEXT_FLAGS) & GL_CONTEXT_FLAG_DEBUG_BIT);
    window_destroy(w);
}

static void test_msaa(void)
{
    platform_clear_error();
    PlatformWindow *w = make((GLConfig){.msaa = 4});
    if (!w)
    {
        CHECK(strstr(platform_get_error(), "4 samples") != NULL);
        return;
    }
    CHECK(geti(GL_SAMPLE_BUFFERS) == 1);
    CHECK(geti(GL_SAMPLES) >= 4);
    window_destroy(w);
}

static void test_failures(void)
{
    for (int i = 0; i < 20; i++)
    {
        platform_clear_error();
        PlatformWindow *w = make((GLConfig){.profile = GL_PROFILE_CORE, .major = 9, .minor = 9});
        CHECK(w == NULL);
        CHECK(strstr(platform_get_error(), "core 9.9") != NULL);
    }

    platform_clear_error();
    CHECK(make((GLConfig){.profile = (GLProfile)99}) == NULL);
    CHECK(strstr(platform_get_error(), "invalid OpenGL configuration") != NULL);
    CHECK(make((GLConfig){.msaa = -1}) == NULL);
    CHECK(make((GLConfig){.major = -1}) == NULL);

    platform_clear_error();
    CHECK(make((GLConfig){.msaa = 1024}) == NULL);
    CHECK(strstr(platform_get_error(), "1024 samples") != NULL);

    PlatformWindow *w = make((GLConfig){0});
    CHECK(w != NULL);
    if (w)
        window_destroy(w);
}

static void test_monitor_mouse(void)
{
    int x = -1, y = -1;
    bool known = mouse_global_position(&x, &y);
    int under = monitor_from_mouse();
    CHECK(known == (under >= 0) || !known);
    if (!known)
        return;
    CHECK(under == monitor_from_point(x, y));

    WindowConfig cfg = {.title = "monitor", .width = 200, .height = 120,
                        .x = WINDOW_POS_CENTERED, .y = WINDOW_POS_CENTERED,
                        .monitor = MONITOR_MOUSE};
    PlatformWindow *w = window_create(&cfg);
    CHECK(w != NULL);
    if (!w)
        return;
    /* The window manager places the window a few events after creation. */
    for (int i = 0; i < 200 && monitor_from_window(w) != under; i++)
    {
        window_begin_frame(w);
        window_swap(w);
    }
    if (under >= 0)
        CHECK(monitor_from_window(w) == under);
    window_destroy(w);
}

typedef void (*ClearColor)(float r, float g, float b, float a);
typedef void (*Clear)(unsigned mask);
typedef void (*ReadPixels)(int x, int y, int w, int h, unsigned format, unsigned type, void *out);

#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

/* One context draws to two windows: each keeps what was drawn to it. */
static void test_one_context_two_windows(void)
{
    WindowConfig cfg = {.title = "test_gl_x11", .width = 64, .height = 64};
    PlatformWindow *first = window_create(&cfg);
    PlatformWindow *second = window_create(&cfg);
    CHECK(first != NULL);
    CHECK(second != NULL);
    if (!first || !second)
    {
        if (first)
            window_destroy(first);
        if (second)
            window_destroy(second);
        return;
    }
    for (int i = 0; i < 10; i++)
    {
        window_begin_frame(first);
        window_begin_frame(second);
    }

    window_make_current(first);
    ClearColor clear_color = (ClearColor)gl_proc_address("glClearColor");
    Clear clear = (Clear)gl_proc_address("glClear");
    ReadPixels read_pixels = (ReadPixels)gl_proc_address("glReadPixels");
    CHECK(clear_color && clear && read_pixels);
    if (clear_color && clear && read_pixels)
    {
        unsigned char pixel[4] = {0, 0, 0, 0};
        clear_color(1.0f, 0.0f, 0.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);

        window_make_current_on(second, first);
        clear_color(0.0f, 0.0f, 1.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        read_pixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        CHECK(pixel[0] == 0 && pixel[2] == 255);

        /* What a window held before the context left it is not kept by every
           driver, so draw again before reading. */
        window_make_current_on(first, first);
        clear_color(1.0f, 0.0f, 0.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        read_pixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        CHECK(pixel[0] == 255 && pixel[2] == 0);

        window_swap(second);
        window_swap(first);

        /* Destroying the other window leaves the first context current. */
        window_destroy(second);
        second = NULL;
        pixel[0] = 0;
        clear_color(0.0f, 1.0f, 0.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        read_pixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        CHECK(pixel[1] == 255 && pixel[0] == 0);
    }
    if (second)
        window_destroy(second);
    window_destroy(first);
}

int main(void)
{
    if (!platform_init())
    {
        printf("SKIP %s\n", platform_get_error());
        return 77;
    }

    test_default();
    test_profiles();
    test_debug();
    test_msaa();
    test_failures();
    test_monitor_mouse();
    test_one_context_two_windows();

    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
