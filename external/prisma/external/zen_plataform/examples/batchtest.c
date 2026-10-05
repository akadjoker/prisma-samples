#include "platform.h"
#include "zen_2d.h"
#include <glad/gl.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    platform_init();
    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 320;
    cfg.height = 240;
    cfg.render = RENDER_GL;
    cfg.gl.major = 3;
    cfg.gl.minor = 3;
    PlatformWindow *w = window_create(&cfg);
    window_make_current(w);
    if (!r2d_init())
    {
        printf("init fail\n");
        return 1;
    }
    window_begin_frame(w);
    glClear(GL_COLOR_BUFFER_BIT);

    /* 1) only shapes: 100 lines + 200 rects + 50 circles, all white texture */
    r2d_begin(320, 240);
    for (int i = 0; i < 100; i++)
        r2d_line(0, i, 300, i, 1, 0xFFFFFFFF);
    for (int i = 0; i < 200; i++)
        r2d_rect(i, 0, 4, 4, 0xFF00FF00);
    for (int i = 0; i < 50; i++)
        r2d_circle(50 + i, 120, 10, 0xFF3399FF);
    unsigned before_end = r2d_draw_calls();
    r2d_end();
    printf("shapes-only: 350 primitives -> %u draw call(s) (mid-frame: %u)\n",
           r2d_draw_calls(), before_end);

    /* 2) mixed: shapes, then a texture, then shapes again */
    uint32_t px[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    R2dTexture t = r2d_texture_create(px, 2, 2);
    r2d_begin(320, 240);
    for (int i = 0; i < 100; i++)
        r2d_line(0, i, 300, i, 1, 0xFFFFFFFF);       /* white batch */
    r2d_texture_draw(t, 10, 10, 20, 20, 0xFFFFFFFF); /* tex switch -> flush */
    for (int i = 0; i < 100; i++)
        r2d_rect(i, 0, 3, 3, 0xFFFF0000); /* back to white -> flush */
    r2d_end();                            /* final flush */
    printf("mixed (100 lines | 1 texture | 100 rects): %u draw calls\n", r2d_draw_calls());

    /* 3) clip changes */
    r2d_begin(320, 240);
    for (int k = 0; k < 5; k++)
    {
        r2d_clip(0, 0, 100, 100);
        r2d_rect(0, 0, 50, 50, 0xFFFFFFFF);
    }
    r2d_end();
    printf("5 rects, same clip set 5x: %u draw calls\n", r2d_draw_calls());

    r2d_shutdown();
    window_destroy(w);
    platform_shutdown();
    return 0;
}
