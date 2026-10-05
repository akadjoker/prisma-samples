/*
 * ddemo - DOOM-style ray caster over RENDER_PIXELS. DDA wall casting with
 * camera-plane projection (no fisheye), coloured floor/ceiling, a sprite,
 * a minimap, and FPS controls.
 *
 * Controls:
 *   [WASD]   move          (key_down)
 *   [Mouse]  rotate        (mouse_delta, MOUSE_MODE_CAPTURED)
 *   [Tab]    toggle mouse capture
 *   [Space]  toggle sprite animation
 *   [Esc]    quit
 */
#include "platform.h"

#include <math.h>
#include <stdio.h>

/* ========================================================================== */
/*  Map                                                                       */
/* ========================================================================== */

#define MAP_W 16
#define MAP_H 16

static const int g_map[MAP_H][MAP_W] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

static int map_at(int x, int y)
{
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H)
        return 1;
    return g_map[y][x];
}

/* ========================================================================== */
/*  Player & sprite                                                           */
/* ========================================================================== */

typedef struct
{
    float x, y, angle;
} Player;
static Player g_player;
static float g_sprite_x = 7.5f, g_sprite_y = 7.5f;
static bool g_sprite_move = true;

/* ========================================================================== */
/*  DDA ray cast (returns perpendicular distance, no fisheye)                  */
/* ========================================================================== */

static float cast_ray(float px, float py, float rx, float ry, int *side)
{
    int mx = (int)px, my = (int)py;
    float ddx = (rx == 0) ? 1e30f : fabsf(1.0f / rx);
    float ddy = (ry == 0) ? 1e30f : fabsf(1.0f / ry);
    int sx, sy;
    float sdx, sdy;
    if (rx < 0)
    {
        sx = -1;
        sdx = (px - mx) * ddx;
    }
    else
    {
        sx = 1;
        sdx = (mx + 1.0f - px) * ddx;
    }
    if (ry < 0)
    {
        sy = -1;
        sdy = (py - my) * ddy;
    }
    else
    {
        sy = 1;
        sdy = (my + 1.0f - py) * ddy;
    }

    int hit = 0;
    while (!hit)
    {
        if (sdx < sdy)
        {
            sdx += ddx;
            mx += sx;
            *side = 0;
        }
        else
        {
            sdy += ddy;
            my += sy;
            *side = 1;
        }
        if (map_at(mx, my))
            hit = 1;
    }
    /* perpendicular distance to camera plane */
    if (*side == 0)
        return (mx - px + (1.0f - sx) * 0.5f) / rx;
    else
        return (my - py + (1.0f - sy) * 0.5f) / ry;
}

/* ========================================================================== */
/*  Render                                                                     */
/* ========================================================================== */

static uint32_t wall_color(int side, float dist)
{
    float s = 1.0f / (1.0f + dist * dist * 0.1f);
    if (s > 1.0f)
        s = 1.0f;
    int r, g, b;
    if (side == 1)
    {
        r = (int)(220 * s);
        g = (int)(100 * s);
        b = (int)(60 * s);
    }
    else
    {
        r = (int)(100 * s);
        g = (int)(120 * s);
        b = (int)(180 * s);
    }
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

#define MAX_W 320  /* max internal framebuffer width */
static float g_zbuf[MAX_W];

static void render(Framebuffer *fb)
{
    int w = fb->width, h = fb->height, stride = fb->stride;
    uint32_t *pix = fb->pixels;

    float fov_scale = 0.66f;
    float px = g_player.x, py = g_player.y;
    float dx = cosf(g_player.angle), dy = sinf(g_player.angle);
    float plx = -dy * fov_scale, ply = dx * fov_scale;

    for (int x = 0; x < w; x++)
    {
        float cx = 2.0f * x / (float)w - 1.0f;
        float rx = dx + plx * cx;
        float ry = dy + ply * cx;
        int side;
        float d = cast_ray(px, py, rx, ry, &side);
        g_zbuf[x] = d; /* store depth for sprite occlusion */
        int wh = (int)(h / d);
        if (wh > h) wh = h;
        int top = (h - wh) / 2;
        if (top < 0) top = 0;
        int bot = top + wh;
        if (bot >= h) bot = h;

        uint32_t col = wall_color(side, d);
        uint32_t *row = pix + x;
        for (int y = 0; y < top; y++, row += stride) *row = 0xFF1A1A2E;
        for (int y = top; y < bot; y++, row += stride) *row = col;
        for (int y = bot; y < h; y++, row += stride) *row = 0xFF2E1A1A;
    }

    /* sprite with z-buffer occlusion */
    float sdx = g_sprite_x - px, sdy = g_sprite_y - py;
    float inv_det = 1.0f / (plx * dy - dx * ply);
    float tx = inv_det * (dy * sdx - dx * sdy);
    float ty = inv_det * (-ply * sdx + plx * sdy);
    if (ty > 0.1f)
    {
        int spx = (int)((0.5f * (1.0f + tx / ty)) * w);
        int sph = (int)(h / ty);
        if (sph > h * 2) sph = h * 2;
        int spt = (h - sph) / 2;
        if (spt < 0) spt = 0;
        int spb = spt + sph;
        if (spb >= h) spb = h;
        int spw = sph / 4;
        if (spw < 2) spw = 2;
        float ss = 1.0f / (1.0f + ty * 0.05f);
        uint32_t sc = 0xFF000000u | ((uint32_t)(0xFF * ss) << 16) | ((uint32_t)(0xDD * ss) << 8) | (uint32_t)(0x00 * ss);
        int sx0 = spx - spw / 2, sx1 = spx + spw / 2;
        if (sx0 < 0) sx0 = 0;
        if (sx1 >= w) sx1 = w - 1;
        for (int ix = sx0; ix <= sx1; ix++)
        {
            if (ty >= g_zbuf[ix]) continue; /* occluded by wall */
            uint32_t *row = pix + ix + spt * stride;
            for (int iy = spt; iy < spb; iy++, row += stride)
                *row = sc;
        }
    }

    /* minimap - direct pixel write (no draw call overhead) */
    int mm_w = 120, mm_h = 120;
    int mm_x = w - mm_w - 8, mm_y = h - mm_h - 8;
    float cw = (float)mm_w / MAP_W, ch = (float)mm_h / MAP_H;

    /* background */
    for (int y = mm_y - 2; y < mm_y + mm_h + 2 && y < h; y++) {
        if (y < 0) continue;
        uint32_t *row = pix + mm_x - 2 + y * stride;
        for (int x = mm_x - 2; x < mm_x + mm_w + 2 && x < w; x++)
            row[x - (mm_x - 2)] = 0xFF181818;
    }
    /* cells */
    for (int my = 0; my < MAP_H; my++) {
        int cy = mm_y + (int)(my * ch);
        for (int mx = 0; mx < MAP_W; mx++) {
            int cx = mm_x + (int)(mx * cw);
            uint32_t col = map_at(mx, my) ? 0xFF444444 : 0xFF1A1A1A;
            for (int y = cy; y < cy + (int)ch + 1 && y < h; y++) {
                if (y < 0) continue;
                uint32_t *row = pix + cx + y * stride;
                for (int x = cx; x < cx + (int)cw + 1 && x < w; x++)
                    row[x - cx] = col;
            }
        }
    }
    /* sprite on minimap */
    {
        int sx = mm_x + (int)(g_sprite_x * cw) - 1, sy = mm_y + (int)(g_sprite_y * ch) - 1;
        for (int y = sy; y < sy + 3 && y < h; y++) {
            if (y < 0) continue;
            uint32_t *row = pix + sx + y * stride;
            for (int x = sx; x < sx + 3 && x < w; x++)
                row[x - sx] = 0xFFFFDD00;
        }
    }
    /* player dot + direction line */
    {
        int p_mx = mm_x + (int)(px * cw), p_my = mm_y + (int)(py * ch);
        for (int y = p_my - 2; y <= p_my + 2 && y < h; y++) {
            if (y < 0) continue;
            uint32_t *row = pix + p_mx - 2 + y * stride;
            for (int x = p_mx - 2; x <= p_mx + 2 && x < w; x++) {
                int dx2 = x - p_mx, dy2 = y - p_my;
                if (dx2 * dx2 + dy2 * dy2 <= 5) row[x - (p_mx - 2)] = 0xFFFF2020;
            }
        }
        int ax = p_mx + (int)(dx * 6), ay = p_my + (int)(dy * 6);
        /* bresenham */
        int lx = p_mx, ly = p_my;
        int sdx = ax > lx ? 1 : -1, sdy = ay > ly ? 1 : -1;
        int adx = (ax - lx) * sdx, ady = (ay - ly) * sdy;
        int err = (adx > ady ? adx : -ady) / 2;
        for (;;) {
            if (lx >= mm_x && lx < mm_x + mm_w && ly >= mm_y && ly < mm_y + mm_h) {
                uint32_t *r = pix + lx + ly * stride;
                *r = 0xFFFF2020;
            }
            if (lx == ax && ly == ay) break;
            int e2 = err;
            if (e2 > -adx) { err -= ady; lx += sdx; }
            if (e2 < ady)  { err += adx; ly += sdy; }
        }
    }
}

/* ========================================================================== */
/*  Input                                                                      */
/* ========================================================================== */

static bool g_captured = true;

static void handle_input(PlatformWindow *w, double dt)
{
    Event e;
    while (poll_event(w, &e))
    {
    }

    if (key_pressed(w, KEY_TAB))
    {
        g_captured = !g_captured;
        mouse_set_mode(w, g_captured ? MOUSE_MODE_CAPTURED : MOUSE_MODE_NORMAL);
    }
    if (key_pressed(w, KEY_SPACE))
        g_sprite_move = !g_sprite_move;

    float spd = 3.0f * (float)dt;
    float mx = cosf(g_player.angle), my = sinf(g_player.angle);
    float nx = g_player.x, ny = g_player.y;

    if (key_down(w, KEY_W))
    {
        nx += mx * spd;
        ny += my * spd;
    }
    if (key_down(w, KEY_S))
    {
        nx -= mx * spd;
        ny -= my * spd;
    }
    if (key_down(w, KEY_A))
    {
        nx += my * spd;
        ny -= mx * spd;
    }
    if (key_down(w, KEY_D))
    {
        nx -= my * spd;
        ny += mx * spd;
    }

    float m = 0.25f;
    if (!map_at((int)(nx + m), (int)(ny)))
        g_player.x = nx;
    else if (!map_at((int)(nx + m), (int)g_player.y))
        g_player.x = nx;
    if (!map_at((int)(nx), (int)(ny + m)))
        g_player.y = ny;
    else if (!map_at((int)g_player.x, (int)(ny + m)))
        g_player.y = ny;

    if (g_captured)
    {
        int dx, dy;
        mouse_delta(w, &dx, &dy);
        g_player.angle += (float)dx * 0.003f;
    }
    if (key_down(w, KEY_RIGHT))
        g_player.angle += 2.5f * (float)dt;
    if (key_down(w, KEY_LEFT))
        g_player.angle -= 2.5f * (float)dt;

    if (g_sprite_move)
    {
        double t = time_seconds();
        g_sprite_x = 7.5f + cosf((float)t * 0.7f) * 2.0f;
        g_sprite_y = 7.5f + sinf((float)t * 0.5f) * 2.0f;
    }
}

/* ========================================================================== */
/*  App                                                                        */
/* ========================================================================== */

typedef struct
{
    Framebuffer rt;
    int frames, fps;
    double last_report;
} App;

static void frame(PlatformWindow *w, void *user)
{
    App *app = user;
    double now = time_seconds();
    static double last_time;
    double dt = now - last_time;
    last_time = now;
    if (dt > 0.1)
        dt = 0.1;

    handle_input(w, dt);
    render(&app->rt);

    Framebuffer fb;
    if (window_lock_pixels(w, &fb))
    {
        draw_blit(&fb, &app->rt, 0, 0, app->rt.width, app->rt.height,
                  0, 0, fb.width, fb.height, BLEND_NONE, SCALE_NEAREST);
        app->frames++;
        double now2 = time_seconds();
        if (now2 - app->last_report >= 1.0)
        {
            app->fps = app->frames;
            app->frames = 0;
            app->last_report = now2;
            char title[64];
            snprintf(title, sizeof title, "ddemo - %d FPS", app->fps);
            window_set_title(w, title);
        }
        window_present_pixels(w);
    }
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg = {.title = "ddemo", .width = 960, .height = 600, .x = WINDOW_POS_CENTERED, .y = WINDOW_POS_CENTERED, .render = RENDER_PIXELS, .resizable = true};
    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        fprintf(stderr, "window_create failed\n");
        return 1;
    }
    key_set_exit(w, KEY_ESCAPE);

    App app = {0};
    framebuffer_alloc(&app.rt, 320, 200);
    app.last_report = time_seconds();

    g_player.x = 2.5f;
    g_player.y = 2.5f;
    g_player.angle = 0.0f;
    mouse_set_mode(w, MOUSE_MODE_CAPTURED);

    app_run(w, frame, &app);

    framebuffer_free(&app.rt);
    window_destroy(w);
    platform_shutdown();
    return 0;
}
