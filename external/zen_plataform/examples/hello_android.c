/*
 * hello_android - smoke test for the Android backend. The backend's android_main
 * (from the glue) calls this main(). Output goes to logcat, not stdout. Builds to
 * libhello_android.so, the loadable NativeActivity module.
 */
#include "platform.h"

#include <GLES3/gl3.h>

static void frame(PlatformWindow *w, void *user)
{
    (void)user;

    Event e;
    while (poll_event(w, &e))
    {
        /* a real app would dispatch here; the backend already cooked the state */
    }

    int fw, fh;
    window_get_framebuffer_size(w, &fw, &fh);
    glViewport(0, 0, fw, fh);

    double t = time_seconds();
    glClearColor(0.10f, 0.12f, 0.15f + 0.15f * (float)(0.5 + 0.5 * (t - (int)t)), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg = {.title = "hello_android", .gl = {.major = 3, .minor = 0}, .vsync = true};
    PlatformWindow *w = window_create(&cfg);
    if (!w)
        return 1;

    window_make_current(w);
    key_set_exit(w, KEY_ESCAPE); /* the system Back key maps to Escape */
    app_run(w, frame, NULL);

    window_destroy(w);
    platform_shutdown();
    return 0;
}
