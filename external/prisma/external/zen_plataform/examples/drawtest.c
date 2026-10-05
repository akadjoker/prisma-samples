#include "platform.h"
#include <stdio.h>
static void frame(PlatformWindow *w, void *u) {
    (void)u; Event e; while (poll_event(w, &e)) {}
    Framebuffer fb;
    if (window_lock_pixels(w, &fb)) {
        draw_clear(&fb, 0xFF202830);
        draw_fill_rect(&fb, 20, 20, 120, 80, 0xFF3366CC, BLEND_NONE);
        draw_fill_circle(&fb, 220, 90, 50, 0xFFCC4422, BLEND_NONE);
        draw_fill_rect(&fb, 60, 60, 120, 80, 0x804422CC, BLEND_ALPHA); /* translucent overlap */
        draw_fill_triangle(&fb, 160, 200, 320, 160, 240, 280, 0xFF22CC66, BLEND_NONE);
        draw_line(&fb, 0, 0, fb.width-1, fb.height-1, 0xFFFFFFFF, BLEND_NONE);
        draw_circle(&fb, 320, 90, 60, 0xFFFFFF00, BLEND_NONE);
        window_present_pixels(w);
    }
    if (time_seconds() > 1.5) window_set_should_close(w, true);
}
int main(void){
    if (!platform_init()) return 1;
    WindowConfig cfg = {.title="draw2d", .width=400, .height=300,
        .x=WINDOW_POS_CENTERED, .y=WINDOW_POS_CENTERED, .render=RENDER_PIXELS};
    PlatformWindow *w = window_create(&cfg);
    if (!w) return 1;
    key_set_exit(w, KEY_ESCAPE);
    app_run(w, frame, NULL);
    window_destroy(w); platform_shutdown();
    printf("draw2d over pixel surface: clean exit\n");
    return 0;
}
