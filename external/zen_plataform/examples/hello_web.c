/*
 * hello_web - smoke test for the Emscripten backend. Same shape as hello_x11 but
 * the browser owns the loop, so app_run never returns. Build with emcmake; open
 * the generated .html. Clears the screen each frame and logs input to the console.
 */
#include "platform.h"

#include <GLES3/gl3.h>
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
        else if (e.type == EVENT_WINDOW_RESIZE)
            printf("resize: %dx%d\n", e.data.resize.w, e.data.resize.h);
    }

    int w_px, h_px;
    window_get_framebuffer_size(w, &w_px, &h_px);
    glViewport(0, 0, w_px, h_px);

    double t = time_seconds();
    glClearColor(0.10f, 0.12f, 0.15f + 0.15f * (float)(0.5 + 0.5 * (t - (int)t)), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg = {
        .title = "hello_web",
        .width = 640,
        .height = 480,
        .gl = {.major = 3, .minor = 0},
        .resizable = true,
        .vsync = true,
    };
    PlatformWindow *w = window_create(&cfg);
    if (!w)
        return 1;

    window_make_current(w);
    printf("GL_VERSION: %s\n", (const char *)glGetString(GL_VERSION));
    printf("content scale: %.2f\n", (double)window_content_scale(w));

    key_set_exit(w, KEY_ESCAPE);
    app_run(w, frame, NULL); /* never returns on the web */
    return 0;
}
