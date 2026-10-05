#include "platform.h"
#include <stdio.h>
static void frame(PlatformWindow *w, void *u) {
    (void)u;
    Event e; while (poll_event(w, &e)) {}
    Framebuffer fb;
    if (window_lock_pixels(w, &fb)) {
        int off = (int)(time_seconds() * 60);
        for (int y = 0; y < fb.height; y++)
            for (int x = 0; x < fb.width; x++) {
                unsigned r = (x + off) & 0xff, g = (y + off) & 0xff, b = (x ^ y) & 0xff;
                fb.pixels[y * fb.stride + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        window_present_pixels(w);
    }
    if (time_seconds() > 2.0) window_set_should_close(w, true);
}
int main(void) {
    if (!platform_init()) return 1;
    WindowConfig cfg = {.title="pixels", .width=400, .height=300,
        .x=WINDOW_POS_CENTERED, .y=WINDOW_POS_CENTERED, .render=RENDER_PIXELS};
    PlatformWindow *w = window_create(&cfg);
    if (!w) { fprintf(stderr,"create failed\n"); return 1; }
    key_set_exit(w, KEY_ESCAPE);
    int fw, fh; window_get_framebuffer_size(w, &fw, &fh);
    printf("pixel window %dx%d, no GL context\n", fw, fh);
    app_run(w, frame, NULL);
    window_destroy(w); platform_shutdown();
    printf("clean exit\n");
    return 0;
}
