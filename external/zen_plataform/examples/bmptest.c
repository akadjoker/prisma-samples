#include "platform.h"
#include <stdio.h>
static Framebuffer sprite;
static void frame(PlatformWindow *w, void *u)
{
    (void)u;
    Event e;
    while (poll_event(w, &e))
    {
    }
    Framebuffer fb;
    if (window_lock_pixels(w, &fb))
    {
        draw_clear(&fb, 0xFF101418);
        draw_blit(&fb, &sprite, 0, 0, sprite.width, sprite.height,
                  20, 20, 160, 160, BLEND_NONE, SCALE_NEAREST);
        draw_blit(&fb, &sprite, 0, 0, sprite.width, sprite.height,
                  200, 20, 160, 160, BLEND_NONE, SCALE_BILINEAR);
        window_present_pixels(w);
    }
    if (time_seconds() > 1.5)
        window_set_should_close(w, true);
}
int main(void)
{
    /* build a checker sprite, save + reload it as BMP */
    Framebuffer tmp;
    framebuffer_alloc(&tmp, 16, 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            draw_pixel(&tmp, x, y, ((x ^ y) & 1) ? 0xFFFF8020 : 0xFF2040FF, BLEND_NONE);
    if (!framebuffer_save_bmp(&tmp, "sprite_tmp.bmp"))
    {
        printf("save failed\n");
        return 1;
    }
    framebuffer_free(&tmp);
    if (!framebuffer_load_bmp(&sprite, "sprite_tmp.bmp"))
    {
        printf("load failed\n");
        return 1;
    }
    printf("loaded BMP %dx%d\n", sprite.width, sprite.height);

    if (!platform_init())
        return 1;
    WindowConfig cfg = {.title = "bmp", .width = 400, .height = 220, .x = WINDOW_POS_CENTERED, .y = WINDOW_POS_CENTERED, .render = RENDER_PIXELS};
    PlatformWindow *w = window_create(&cfg);
    if (!w)
        return 1;
    key_set_exit(w, KEY_ESCAPE);
    app_run(w, frame, NULL);
    window_destroy(w);
    platform_shutdown();
    framebuffer_free(&sprite);
    remove("sprite_tmp.bmp");
    printf("BMP load + scaled blit: clean exit\n");
    return 0;
}
