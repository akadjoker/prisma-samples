/*
 * hello_x11 - smoke test for the X11 backend. Opens a real window, makes the GL
 * context current, clears the screen each frame, prints input events, and exits
 * on Escape, the close button, or after a couple of seconds (so it can run
 * unattended in a check).
 */
#include "platform.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#include <stdio.h>

static void frame(PlatformWindow *w, void *user)
{
    (void)user;

    Event e;
    while (poll_event(w, &e))
    {
        if (e.type == EVENT_KEY && e.data.key.down && !e.data.key.repeat)
            printf("key down: %d (mods %d)\n", e.data.key.key, e.data.key.mods);
        else if (e.type == EVENT_CHAR)
            printf("char: U+%04X\n", e.data.codepoint);
        else if (e.type == EVENT_MOUSE_BUTTON && e.data.mouse.down)
            printf("mouse button %d at %d,%d\n", e.data.mouse.button, e.data.mouse.x, e.data.mouse.y);
        else if (e.type == EVENT_WINDOW_RESIZE)
            printf("resize: %dx%d\n", e.data.resize.w, e.data.resize.h);
    }

    int w_px, h_px;
    window_get_framebuffer_size(w, &w_px, &h_px);
    glViewport(0, 0, w_px, h_px);

    double t = time_seconds();
    glClearColor(0.10f, 0.12f, 0.15f + 0.15f * (float)(0.5 + 0.5 * (t - (int)t)), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (t > 2.5)
        window_set_should_close(w, true);
}

int main(void)
{
    if (!platform_init())
    {
        fprintf(stderr, "platform_init failed (no display?)\n");
        return 1;
    }

    printf("monitors: %d\n", monitor_count());
    MonitorInfo m;
    if (monitor_get_info(0, &m))
        printf("monitor 0: %s  %dx%d @ (%d,%d)  %dHz  primary=%d  scale=%.2f\n",
               m.name, m.width, m.height, m.x, m.y, m.refresh_hz, m.primary, m.content_scale);

    WindowConfig cfg = {
        .title = "hello_x11",
        .width = 640,
        .height = 480,
        .x = WINDOW_POS_CENTERED,
        .y = WINDOW_POS_CENTERED,
        .gl = {.major = 3, .minor = 3},
        .resizable = true,
        .vsync = true,
    };
    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        fprintf(stderr, "window_create failed\n");
        platform_shutdown();
        return 1;
    }

    window_make_current(w);
    printf("GL_VERSION:  %s\n", (const char *)glGetString(GL_VERSION));
    printf("GL_RENDERER: %s\n", (const char *)glGetString(GL_RENDERER));

    clipboard_set("zen platform clipboard test");
    printf("clipboard round-trip: \"%s\"\n", clipboard_get());

    key_set_exit(w, KEY_ESCAPE);
    app_run(w, frame, NULL);

    window_destroy(w);
    platform_shutdown();
    printf("clean exit\n");
    return 0;
}
