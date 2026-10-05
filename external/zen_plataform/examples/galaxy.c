/*
 * galaxy - 80s-style space shooter over RENDER_PIXELS.
 * Starfield, player ship, enemy formation, bullets, scoring.
 *
 * Controls:
 *   [A/D] or [Left/Right]  move ship
 *   [Space]                 fire
 *   [R]                     restart when dead
 *   [Esc]                   quit
 */
#include "platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/*  Constants                                                                  */
/* ========================================================================== */

#define FB_W 320
#define FB_H 200
#define MAX_STARS 100
#define ENEMY_COLS 8
#define ENEMY_ROWS 4
#define MAX_BULLETS 10
#define PLAYER_SPEED 180.0f
#define BULLET_SPEED 300.0f
#define ENEMY_SPEED 60.0f

/* ========================================================================== */
/*  Game state                                                                 */
/* ========================================================================== */

typedef struct
{
    float x, y;
} Vec2;

typedef struct
{
    Vec2 pos;
    bool alive;
} Bullet;

typedef struct
{
    Vec2 pos;
    bool alive;
} Enemy;

typedef struct
{
    float x, y;
    float speed;
} Star;

static struct
{
    Star stars[MAX_STARS];
    Vec2 player;
    Enemy enemies[ENEMY_ROWS][ENEMY_COLS];
    Bullet player_bullets[MAX_BULLETS];
    Bullet enemy_bullets[3];
    float enemy_dir;   /* 1 = right, -1 = left */
    float enemy_timer; /* time accumulator for step movement */
    int score;
    bool dead;
} g;

/* ========================================================================== */
/*  Init                                                                       */
/* ========================================================================== */

static void init_game(void)
{
    memset(&g, 0, sizeof g);
    g.player.x = FB_W / 2.0f;
    g.player.y = FB_H - 20.0f;
    g.enemy_dir = 1.0f;

    for (int i = 0; i < MAX_STARS; i++)
    {
        g.stars[i].x = (float)(rand() % FB_W);
        g.stars[i].y = (float)(rand() % FB_H);
        g.stars[i].speed = 30.0f + (float)(rand() % 80);
    }

    float start_x = 40.0f, start_y = 30.0f;
    float spacing_x = 30.0f, spacing_y = 24.0f;
    for (int r = 0; r < ENEMY_ROWS; r++)
    {
        for (int c = 0; c < ENEMY_COLS; c++)
        {
            g.enemies[r][c].pos.x = start_x + c * spacing_x;
            g.enemies[r][c].pos.y = start_y + r * spacing_y;
            g.enemies[r][c].alive = true;
        }
    }
}

/* ========================================================================== */
/*  Drawing helpers (direct pixel access)                                      */
/* ========================================================================== */

static inline void put_pixel(uint32_t *pix, int stride, int x, int y, uint32_t c)
{
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H)
        pix[y * stride + x] = c;
}

static void draw_ship(uint32_t *pix, int stride, int cx, int cy, uint32_t c)
{
    /* Simple triangle: point up */
    put_pixel(pix, stride, cx, cy - 5, c);
    put_pixel(pix, stride, cx, cy - 4, c);
    for (int x = -2; x <= 2; x++)
    {
        put_pixel(pix, stride, cx + x, cy - 3, c);
    }
    for (int x = -3; x <= 3; x++)
    {
        put_pixel(pix, stride, cx + x, cy - 2, c);
    }
    for (int x = -3; x <= 3; x++)
    {
        put_pixel(pix, stride, cx + x, cy - 1, c);
    }
    for (int x = -4; x <= 4; x++)
    {
        put_pixel(pix, stride, cx + x, cy, c);
    }
    for (int x = -1; x <= 1; x++)
    {
        put_pixel(pix, stride, cx + x, cy + 1, c);
    }
    put_pixel(pix, stride, cx - 3, cy + 1, c);
    put_pixel(pix, stride, cx + 3, cy + 1, c);
    /* engine glow */
    put_pixel(pix, stride, cx, cy + 2, 0xFFFF4400);
    put_pixel(pix, stride, cx - 1, cy + 2, 0xFFFF4400);
    put_pixel(pix, stride, cx + 1, cy + 2, 0xFFFF4400);
}

static void draw_enemy(uint32_t *pix, int stride, int cx, int cy, uint32_t c)
{
    /* Bug-like shape */
    for (int x = -3; x <= 3; x++)
    {
        put_pixel(pix, stride, cx + x, cy - 3, c);
    }
    for (int x = -4; x <= 4; x++)
    {
        put_pixel(pix, stride, cx + x, cy - 2, c);
    }
    for (int x = -4; x <= 4; x++)
    {
        put_pixel(pix, stride, cx + x, cy - 1, c);
    }
    for (int x = -3; x <= 3; x++)
    {
        put_pixel(pix, stride, cx + x, cy, c);
    }
    /* eyes */
    put_pixel(pix, stride, cx - 2, cy - 1, 0xFF000000);
    put_pixel(pix, stride, cx + 2, cy - 1, 0xFF000000);
    /* antennae */
    put_pixel(pix, stride, cx - 2, cy - 4, c);
    put_pixel(pix, stride, cx + 2, cy - 4, c);
    /* legs */
    put_pixel(pix, stride, cx - 2, cy + 1, c);
    put_pixel(pix, stride, cx + 2, cy + 1, c);
    put_pixel(pix, stride, cx - 3, cy + 2, c);
    put_pixel(pix, stride, cx + 3, cy + 2, c);
}

static void draw_bullet(uint32_t *pix, int stride, int x, int y, uint32_t c)
{
    put_pixel(pix, stride, x, y - 1, c);
    put_pixel(pix, stride, x, y, c);
    put_pixel(pix, stride, x, y + 1, c);
}

/* ========================================================================== */
/*  Mini 5x7 font for score                                                    */
/* ========================================================================== */

static const uint8_t g_digits[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}, {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}, {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};

static void draw_digit(uint32_t *pix, int stride, int x, int y, int d, uint32_t c, int scale)
{
    const uint8_t *g = g_digits[d];
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 5; col++)
            if (g[row] & (1 << (4 - col)))
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        put_pixel(pix, stride, x + col * scale + sx, y + row * scale + sy, c);
}

static void draw_score(uint32_t *pix, int stride, int score)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%d", score);
    int x = 8, y = 4;
    for (char *s = buf; *s; s++)
    {
        draw_digit(pix, stride, x, y, *s - '0', 0xFF00FF44, 2);
        x += 12;
    }
}

/* ========================================================================== */
/*  Game logic                                                                 */
/* ========================================================================== */

static bool rects_overlap(float ax, float ay, float aw, float ah,
                          float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void fire_bullet(void)
{
    for (int i = 0; i < MAX_BULLETS; i++)
    {
        if (!g.player_bullets[i].alive)
        {
            g.player_bullets[i].pos.x = g.player.x;
            g.player_bullets[i].pos.y = g.player.y - 6;
            g.player_bullets[i].alive = true;
            return;
        }
    }
}

static void update(double dt)
{
    if (g.dead)
        return;

    /* stars */
    for (int i = 0; i < MAX_STARS; i++)
    {
        g.stars[i].y += g.stars[i].speed * (float)dt;
        if (g.stars[i].y > FB_H)
        {
            g.stars[i].y = 0;
            g.stars[i].x = (float)(rand() % FB_W);
        }
    }

    /* player bullets */
    for (int i = 0; i < MAX_BULLETS; i++)
    {
        if (!g.player_bullets[i].alive)
            continue;
        g.player_bullets[i].pos.y -= BULLET_SPEED * (float)dt;
        if (g.player_bullets[i].pos.y < -10)
            g.player_bullets[i].alive = false;
    }

    /* enemy movement */
    g.enemy_timer += (float)dt;
    if (g.enemy_timer > 0.3f)
    {
        g.enemy_timer = 0;
        bool hit_edge = false;
        for (int r = 0; r < ENEMY_ROWS; r++)
            for (int c = 0; c < ENEMY_COLS; c++)
            {
                if (!g.enemies[r][c].alive)
                    continue;
                float ex = g.enemies[r][c].pos.x + g.enemy_dir * 10.0f;
                if (ex < 10 || ex > FB_W - 10)
                    hit_edge = true;
            }
        if (hit_edge)
        {
            g.enemy_dir = -g.enemy_dir;
            for (int r = 0; r < ENEMY_ROWS; r++)
                for (int c = 0; c < ENEMY_COLS; c++)
                    g.enemies[r][c].pos.y += 15.0f;
        }
        for (int r = 0; r < ENEMY_ROWS; r++)
            for (int c = 0; c < ENEMY_COLS; c++)
                if (g.enemies[r][c].alive)
                    g.enemies[r][c].pos.x += g.enemy_dir * 10.0f;
    }

    /* collisions: player bullets vs enemies */
    for (int i = 0; i < MAX_BULLETS; i++)
    {
        if (!g.player_bullets[i].alive)
            continue;
        for (int r = 0; r < ENEMY_ROWS; r++)
            for (int c = 0; c < ENEMY_COLS; c++)
            {
                if (!g.enemies[r][c].alive)
                    continue;
                if (rects_overlap(g.player_bullets[i].pos.x - 1, g.player_bullets[i].pos.y - 2, 2, 4,
                                  g.enemies[r][c].pos.x - 4, g.enemies[r][c].pos.y - 4, 8, 8))
                {
                    g.enemies[r][c].alive = false;
                    g.player_bullets[i].alive = false;
                    g.score += 10;
                    break;
                }
            }
    }

    /* enemies reach player? */
    for (int r = 0; r < ENEMY_ROWS; r++)
        for (int c = 0; c < ENEMY_COLS; c++)
            if (g.enemies[r][c].alive && g.enemies[r][c].pos.y > FB_H - 30)
                g.dead = true;
}

/* ========================================================================== */
/*  Render                                                                     */
/* ========================================================================== */

static void render(Framebuffer *fb)
{
    uint32_t *pix = fb->pixels;
    int stride = fb->stride;

    /* clear */
    for (int i = 0; i < FB_W * FB_H; i++)
        pix[i] = 0xFF000000;

    /* stars */
    for (int i = 0; i < MAX_STARS; i++)
        put_pixel(pix, stride, (int)g.stars[i].x, (int)g.stars[i].y, 0xFF888888);

    /* player ship */
    if (!g.dead)
        draw_ship(pix, stride, (int)g.player.x, (int)g.player.y, 0xFF20DD40);

    /* enemies */
    for (int r = 0; r < ENEMY_ROWS; r++)
        for (int c = 0; c < ENEMY_COLS; c++)
            if (g.enemies[r][c].alive)
            {
                uint32_t ec = (r == 0) ? 0xFFFF4040 : (r == 1) ? 0xFFFF8844
                                                  : (r == 2)   ? 0xFFFFCC44
                                                               : 0xFF8844FF;
                draw_enemy(pix, stride, (int)g.enemies[r][c].pos.x, (int)g.enemies[r][c].pos.y, ec);
            }

    /* bullets */
    for (int i = 0; i < MAX_BULLETS; i++)
        if (g.player_bullets[i].alive)
            draw_bullet(pix, stride, (int)g.player_bullets[i].pos.x, (int)g.player_bullets[i].pos.y, 0xFF44FF44);

    /* score */
    draw_score(pix, stride, g.score);

    /* death message */
    if (g.dead)
    {
        /* draw "GAME OVER" and "PRESS R" */
        const char *msg = "GAME OVER - PRESS R";
        int mx = FB_W / 2 - 70, my = FB_H / 2 - 4;
        for (const char *s = msg; *s; s++)
        {
            draw_digit(pix, stride, mx, my, *s == ' ' ? -1 : 0, 0xFFFF2020, 2);
            mx += 12;
        }
    }
}

/* ========================================================================== */
/*  Input                                                                      */
/* ========================================================================== */

static void handle_input(PlatformWindow *w, double dt)
{
    Event e;
    while (poll_event(w, &e))
    {
    }

    if (key_pressed(w, KEY_R))
    {
        init_game();
        return;
    }
    if (g.dead)
        return;

    float spd = PLAYER_SPEED * (float)dt;
    if (key_down(w, KEY_A) || key_down(w, KEY_LEFT))
        g.player.x -= spd;
    if (key_down(w, KEY_D) || key_down(w, KEY_RIGHT))
        g.player.x += spd;
    if (g.player.x < 10)
        g.player.x = 10;
    if (g.player.x > FB_W - 10)
        g.player.x = FB_W - 10;

    if (key_pressed(w, KEY_SPACE))
        fire_bullet();
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
    update(dt);
    render(&app->rt);

    Framebuffer fb;
    if (window_lock_pixels(w, &fb))
    {
        draw_blit(&fb, &app->rt, 0, 0, FB_W, FB_H,
                  0, 0, fb.width, fb.height, BLEND_NONE, SCALE_NEAREST);
        app->frames++;
        double now2 = time_seconds();
        if (now2 - app->last_report >= 1.0)
        {
            app->fps = app->frames;
            app->frames = 0;
            app->last_report = now2;
            char title[64];
            snprintf(title, sizeof title, "galaxy - %d FPS - score:%d", app->fps, g.score);
            window_set_title(w, title);
        }
        window_present_pixels(w);
    }
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg = {.title = "galaxy", .width = 960, .height = 600, .x = WINDOW_POS_CENTERED, .y = WINDOW_POS_CENTERED, .render = RENDER_PIXELS, .resizable = true};
    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        fprintf(stderr, "window_create failed\n");
        return 1;
    }
    key_set_exit(w, KEY_ESCAPE);

    init_game();

    App app = {0};
    framebuffer_alloc(&app.rt, FB_W, FB_H);
    app.last_report = time_seconds();

    app_run(w, frame, &app);

    framebuffer_free(&app.rt);
    window_destroy(w);
    platform_shutdown();
    return 0;
}
