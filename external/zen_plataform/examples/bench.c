#include "platform.h"
#include "zen_2d.h"
#include <glad/gl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static PlatformWindow *w;
static int W, H;
static R2dTexture tex;

static double run(const char *name, int count, int frames, void (*draw)(int))
{
    /* warmup */
    for (int f = 0; f < 3; f++)
    {
        window_begin_frame(w);
        glClear(GL_COLOR_BUFFER_BIT);
        r2d_begin(W, H);
        draw(count);
        r2d_end();
        glFinish();
    }
    double t0 = time_seconds();
    unsigned dc = 0;
    for (int f = 0; f < frames; f++)
    {
        window_begin_frame(w);
        glClear(GL_COLOR_BUFFER_BIT);
        r2d_begin(W, H);
        draw(count);
        r2d_end();
        glFinish();
        dc = r2d_draw_calls();
    }
    double t = time_seconds() - t0;
    double mspf = 1000.0 * t / frames;
    double fps = frames / t;
    double max60 = count * (16.6667 / mspf);
    printf("%-22s %8d/frame  %6.2f ms  %6.0f fps  %3u draws  ~%.0fk @60fps\n",
           name, count, mspf, fps, dc, max60 / 1000.0);
    return mspf;
}
static void d_sprite(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W, y = (i * 53) % H;
        r2d_sprite(tex, 0, 0, 16, 16, x, y, 8, 8, 1, 1, (float)(i % 360), 0, 0xFFFFFFFF);
    }
}
static void d_rect(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W, y = (i * 53) % H;
        r2d_rect(x, y, 12, 12, 0xFF3399FF);
    }
}
static void d_line(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W;
        r2d_line(x, 0, W - x, H, 1.5f, 0xFFFFFFFF);
    }
}
static void d_rectline(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W, y = (i * 53) % H;
        r2d_rect_line(x, y, 20, 20, 2, 0xFFFFAA00);
    }
}
static void d_circle(int n)
{
    for (int i = 0; i < n; i++)
    {
        float x = (i * 37) % W, y = (i * 53) % H;
        r2d_circle(x, y, 10, 0xFF66CC33);
    }
}

int main(void)
{
    platform_init();
    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 1280;
    cfg.height = 720;
    cfg.render = RENDER_GL;
    cfg.gl.major = 3;
    cfg.gl.minor = 3;
    cfg.vsync = false;
    w = window_create(&cfg);
    window_make_current(w);
    if (!r2d_init())
    {
        printf("init fail\n");
        return 1;
    }
    window_get_framebuffer_size(w, &W, &H);
    uint32_t px[16 * 16];
    for (int i = 0; i < 256; i++)
        px[i] = 0xFFFFFFFF;
    tex = r2d_texture_create(px, 16, 16);
    printf("== r2d benchmark (1280x720, no vsync, glFinish per frame) ==\n");
    run("sprites (rot)", 100000, 60, d_sprite);
    run("rects", 100000, 60, d_rect);
    run("lines", 100000, 60, d_line);
    run("rect outlines", 50000, 60, d_rectline);
    run("circles r=10", 50000, 60, d_circle);
    r2d_shutdown();
    window_destroy(w);
    platform_shutdown();
    return 0;
}
