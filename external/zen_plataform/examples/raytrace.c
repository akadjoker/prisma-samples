/*
 * raytrace - interactive software ray tracer showcasing the zen platform API.
 *
 * Uses RENDER_PIXELS: lock -> draw -> blit -> present.  
 * Demonstrates framebuffer_alloc / framebuffer_free, draw_blit (bilinear
 * upscale), draw primitives for a HUD overlay, framebuffer_save_bmp for
 * screenshots, key_down / key_pressed / mouse_delta / mouse_set_mode for
 * an interactive camera, event handling for window resize, and window_set_title.
 *
 * Scene: a checkered floor, four coloured spheres, a point light, reflections,
 * and shadows.
 *
 * Controls:
 *   [WASD]      move camera                  (key_down)
 *   [Q/E]       move up/down                 (key_down)
 *   [Mouse]     look around                  (mouse_set_mode CAPTURED + delta)
 *   [Tab]       toggle mouse capture
 *   [Space]     pause/unpause light rotation (key_pressed)
 *   [R]         toggle reflections           (key_pressed)
 *   [F]         toggle shadows               (key_pressed)
 *   [1]         scale resolution down        (key_pressed)
 *   [2]         scale resolution up          (key_pressed)
 *   [F12]       save screenshot to BMP       (framebuffer_save_bmp)
 *   [Esc]       quit                         (key_set_exit)
 */
#include "platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* ========================================================================== */
/*  Maths                                                                     */
/* ========================================================================== */

typedef struct
{
    float x, y, z;
} V3;

static V3 v3(float x, float y, float z) { return (V3){x, y, z}; }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 scale(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) { return scale(a, 1.0f / sqrtf(dot(a, a))); }
static V3 reflect(V3 d, V3 n) { return sub(d, scale(n, 2.0f * dot(d, n))); }

typedef struct
{
    V3 center;
    float radius;
    V3 color;
    float reflect;
} Sphere;

static const Sphere g_spheres[] = {
    {{0.0f, 0.0f, -1.0f}, 0.5f, {0.9f, 0.2f, 0.2f}, 0.3f},
    {{1.1f, -0.1f, -1.4f}, 0.4f, {0.2f, 0.8f, 0.3f}, 0.5f},
    {{-1.1f, -0.15f, -1.2f}, 0.35f, {0.25f, 0.4f, 0.9f}, 0.6f},
    {{0.0f, -0.35f, -0.55f}, 0.15f, {0.95f, 0.85f, 0.2f}, 0.1f},
};
#define SPHERE_COUNT (int)(sizeof g_spheres / sizeof g_spheres[0])

#define PLANE_Y (-0.5f)

static bool hit_sphere(V3 o, V3 d, const Sphere *s, float *t)
{
    V3 oc = sub(o, s->center);
    float b = dot(oc, d);
    float c = dot(oc, oc) - s->radius * s->radius;
    float disc = b * b - c;
    if (disc < 0.0f)
        return false;
    float sq = sqrtf(disc);
    float root = -b - sq;
    if (root < 1e-3f)
        root = -b + sq;
    if (root < 1e-3f)
        return false;
    *t = root;
    return true;
}

/* Nearest hit. Returns the surface point, normal, albedo and reflectivity. */
static bool nearest(V3 o, V3 d, V3 *hit, V3 *n, V3 *albedo, float *reflectivity)
{
    float best = 1e30f;
    bool found = false;

    for (int i = 0; i < SPHERE_COUNT; i++)
    {
        float t;
        if (hit_sphere(o, d, &g_spheres[i], &t) && t < best)
        {
            best = t;
            found = true;
            *hit = add(o, scale(d, t));
            *n = norm(sub(*hit, g_spheres[i].center));
            *albedo = g_spheres[i].color;
            *reflectivity = g_spheres[i].reflect;
        }
    }

    if (d.y < -1e-4f)
    {
        float t = (PLANE_Y - o.y) / d.y;
        if (t > 1e-3f && t < best)
        {
            best = t;
            found = true;
            *hit = add(o, scale(d, t));
            *n = v3(0.0f, 1.0f, 0.0f);
            int c = ((int)floorf(hit->x * 2.0f) + (int)floorf(hit->z * 2.0f)) & 1;
            *albedo = c ? v3(0.85f, 0.85f, 0.85f) : v3(0.15f, 0.15f, 0.18f);
            *reflectivity = 0.25f;
        }
    }
    return found;
}

/* ---- toggles ---- */
static bool g_reflections = true;
static bool g_shadows = true;
static bool g_paused = false;

static bool in_shadow(V3 p, V3 light)
{
    V3 d = norm(sub(light, p));
    V3 h, n, a;
    float r;
    if (!nearest(add(p, scale(d, 1e-3f)), d, &h, &n, &a, &r))
        return false;
    float light_dist = sqrtf(dot(sub(light, p), sub(light, p)));
    float hit_dist = sqrtf(dot(sub(h, p), sub(h, p)));
    return hit_dist < light_dist;
}

static V3 trace(V3 o, V3 d, V3 light, int depth);

static V3 shade(V3 hit, V3 n, V3 d, V3 albedo, float reflectivity, V3 light, int depth)
{
    V3 l = norm(sub(light, hit));
    float diff = dot(n, l);
    if (diff < 0.0f)
        diff = 0.0f;
    if (g_shadows && diff > 0.0f && in_shadow(hit, light))
        diff *= 0.15f;

    V3 view = norm(scale(d, -1.0f));
    V3 halfv = norm(add(l, view));
    float spec = powf(dot(n, halfv) > 0 ? dot(n, halfv) : 0, 48.0f);

    V3 color = add(scale(albedo, 0.15f + 0.85f * diff), scale(v3(1, 1, 1), spec * 0.6f));

    if (g_reflections && reflectivity > 0.0f && depth < 3)
    {
        V3 rd = reflect(d, n);
        V3 rc = trace(add(hit, scale(rd, 1e-3f)), rd, light, depth + 1);
        color = add(scale(color, 1.0f - reflectivity), scale(rc, reflectivity));
    }
    return color;
}

static V3 trace(V3 o, V3 d, V3 light, int depth)
{
    V3 hit, n, albedo;
    float reflectivity;
    if (!nearest(o, d, &hit, &n, &albedo, &reflectivity))
    {
        float t = 0.5f * (d.y + 1.0f);
        return add(scale(v3(0.5f, 0.7f, 1.0f), t), scale(v3(1, 1, 1), 1.0f - t));
    }
    return shade(hit, n, d, albedo, reflectivity, light, depth);
}

static uint32_t pack(V3 c)
{
    float r = sqrtf(c.x > 1 ? 1 : c.x < 0 ? 0 : c.x);
    float g = sqrtf(c.y > 1 ? 1 : c.y < 0 ? 0 : c.y);
    float b = sqrtf(c.z > 1 ? 1 : c.z < 0 ? 0 : c.z);
    return 0xFF000000u | ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
}

/* ========================================================================== */
/*  Camera                                                                    */
/* ========================================================================== */

typedef struct
{
    V3 pos;
    float yaw, pitch;
} Camera;

static Camera g_cam;

static V3 cam_forward(const Camera *cam)
{
    return v3(sinf(cam->yaw) * cosf(cam->pitch),
              -sinf(cam->pitch),
              cosf(cam->yaw) * cosf(cam->pitch));
}

static V3 cam_right(const Camera *cam)
{
    return v3(cosf(cam->yaw), 0.0f, -sinf(cam->yaw));
}

static V3 cam_up(const Camera *cam)
{
    return cross(cam_forward(cam), cam_right(cam));
}

static void render(Framebuffer *rt, const Camera *cam, V3 light)
{
    V3 fwd = cam_forward(cam);
    V3 rgt = cam_right(cam);
    V3 up = cam_up(cam);
    float aspect = (float)rt->width / rt->height;

    for (int y = 0; y < rt->height; y++)
    {
        uint32_t *row = rt->pixels + (size_t)y * rt->stride;
        float py = (1.0f - 2.0f * (y + 0.5f) / rt->height) * 0.6f;
        for (int x = 0; x < rt->width; x++)
        {
            float px = (2.0f * (x + 0.5f) / rt->width - 1.0f) * 0.6f * aspect;
            V3 dir = norm(add(add(scale(rgt, px), scale(up, py)), fwd));
            row[x] = pack(trace(cam->pos, dir, light, 0));
        }
    }
}

/* ========================================================================== */
/*  Mini 5x7 font                                                             */
/* ========================================================================== */

#define FONT_W 5
#define FONT_H 7

static const uint8_t glyph_digits[10][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};

static const uint8_t glyph_upper[26][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x01,0x01,0x01,0x01,0x11,0x11,0x0E},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},{0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x15,0x0A},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},{0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}};

static const uint8_t glyph_dash[7] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
static const uint8_t glyph_colon[7] = {0x00,0x04,0x04,0x00,0x04,0x04,0x00};
static const uint8_t glyph_space[7] = {0};

static const uint8_t *glyph_for(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return glyph_upper[c - 'A'];
    if (c >= '0' && c <= '9') return glyph_digits[c - '0'];
    if (c == '-') return glyph_dash;
    if (c == ':') return glyph_colon;
    return glyph_space;
}

static void draw_char_small(Framebuffer *fb, int x, int y, char c, uint32_t color, int scale)
{
    const uint8_t *g = glyph_for(c);
    for (int row = 0; row < FONT_H; row++)
    {
        uint8_t bits = g[row];
        for (int col = 0; col < FONT_W; col++)
            if (bits & (1 << (FONT_W - 1 - col)))
                draw_fill_rect(fb, x + col * scale, y + row * scale, scale, scale, color, BLEND_NONE);
    }
}

static void draw_text_small(Framebuffer *fb, int x, int y, const char *s, uint32_t color, int scale)
{
    int cx = x;
    for (; *s; s++) { draw_char_small(fb, cx, y, *s, color, scale); cx += (FONT_W + 1) * scale; }
}

/* ========================================================================== */
/*  HUD                                                                       */
/* ========================================================================== */

static void draw_hud(Framebuffer *fb, int fps, int res_w, int res_h,
                     bool captured, const Camera *cam)
{
    int hud_x = 10, hud_y = 6, text_h = 14;
    int lx = hud_x + 8, ly = hud_y + 4;
    char buf[64];

    draw_fill_rect(fb, hud_x, hud_y, 270, 130, 0xCC101010, BLEND_ALPHA);
    draw_rect(fb, hud_x - 1, hud_y - 1, 272, 132, 0x44444466, BLEND_ALPHA);

    snprintf(buf, sizeof buf, "%d FPS", fps);
    draw_text_small(fb, lx, ly, buf, 0xFFD0D8E0, 2);

    snprintf(buf, sizeof buf, "%dx%d", res_w, res_h);
    draw_text_small(fb, lx, ly + text_h, "RES:", 0xFFD0D8E0, 2);
    draw_text_small(fb, lx + 60, ly + text_h, buf, 0xFFFFCC55, 2);

    draw_text_small(fb, lx, ly + text_h * 2, "[R]", 0xFFFFCC55, 2);
    draw_text_small(fb, lx + 40, ly + text_h * 2, "REFL:", 0xFFD0D8E0, 2);
    draw_fill_rect(fb, lx + 105, ly + text_h * 2 + 4, 8, 8, g_reflections ? 0xFF30DD50 : 0xFF664444, BLEND_NONE);
    draw_text_small(fb, lx + 120, ly + text_h * 2, g_reflections ? " ON" : "OFF", g_reflections ? 0xFF30DD50 : 0xFF664444, 2);

    draw_text_small(fb, lx, ly + text_h * 3, "[F]", 0xFFFFCC55, 2);
    draw_text_small(fb, lx + 40, ly + text_h * 3, "SHAD:", 0xFFD0D8E0, 2);
    draw_fill_rect(fb, lx + 105, ly + text_h * 3 + 4, 8, 8, g_shadows ? 0xFF30DD50 : 0xFF664444, BLEND_NONE);
    draw_text_small(fb, lx + 120, ly + text_h * 3, g_shadows ? " ON" : "OFF", g_shadows ? 0xFF30DD50 : 0xFF664444, 2);

    draw_text_small(fb, lx, ly + text_h * 4, "[SPC]", 0xFFFFCC55, 2);
    draw_text_small(fb, lx + 60, ly + text_h * 4, "PAUSE:", 0xFFD0D8E0, 2);
    draw_fill_rect(fb, lx + 135, ly + text_h * 4 + 4, 8, 8, g_paused ? 0xFF30DD50 : 0xFF664444, BLEND_NONE);

    snprintf(buf, sizeof buf, "%.1f %.1f %.1f", cam->pos.x, cam->pos.y, cam->pos.z);
    draw_text_small(fb, lx, ly + text_h * 5, "POS:", 0xFFD0D8E0, 2);
    draw_text_small(fb, lx + 55, ly + text_h * 5, buf, 0xFFFFCC55, 2);

    if (captured)
    {
        int cx = fb->width / 2, cy = fb->height / 2;
        draw_line(fb, cx - 10, cy, cx - 4, cy, 0x88FFFFFF, BLEND_ALPHA);
        draw_line(fb, cx + 4, cy, cx + 10, cy, 0x88FFFFFF, BLEND_ALPHA);
        draw_line(fb, cx, cy - 10, cx, cy - 4, 0x88FFFFFF, BLEND_ALPHA);
        draw_line(fb, cx, cy + 4, cx, cy + 10, 0x88FFFFFF, BLEND_ALPHA);
        draw_fill_circle(fb, cx, cy, 2, 0xFFFFFFFF, BLEND_NONE);
    }

    int help_y = fb->height - 18;
    draw_fill_rect(fb, 0, help_y - 2, fb->width, 18, 0xCC101010, BLEND_ALPHA);
    draw_text_small(fb, 6,   help_y, "[TAB] capture",  0xFFFFCC55, 1);
    draw_text_small(fb, 130, help_y, "[WASD/QE] move", 0xFFFFCC55, 1);
    draw_text_small(fb, 274, help_y, "[1/2] res",      0xFFFFCC55, 1);
    draw_text_small(fb, 356, help_y, "[F12] save",     0xFFFFCC55, 1);
    draw_text_small(fb, 460, help_y, "[Esc] quit",     0xFFFFCC55, 1);
}

/* ========================================================================== */
/*  Screenshot                                                                 */
/* ========================================================================== */

static void take_screenshot(PlatformWindow *w, const Framebuffer *rt)
{
    char path[256];
    int n = 0;
    for (; n < 9999; n++)
    {
        snprintf(path, sizeof path, "raytrace_%04d.bmp", n);
        if (!file_exists(path))
            break;
    }
    if (framebuffer_save_bmp(rt, path))
    {
        char title[320];
        snprintf(title, sizeof title, "saved: %s", path);
        window_set_title(w, title);
        printf("screenshot saved: %s\n", path);
    }
    else
        fprintf(stderr, "screenshot failed: %s\n", path);
}

/* ========================================================================== */
/*  Input handling                                                             */
/* ========================================================================== */

static void handle_input(PlatformWindow *w, bool *captured, double dt)
{
    Event e;
    while (poll_event(w, &e))
    {
        if (e.type == EVENT_WINDOW_RESIZE)
            printf("window resized: %dx%d\n", e.data.resize.w, e.data.resize.h);
    }
    if (key_pressed(w, KEY_TAB))
    {
        *captured = !(*captured);
        mouse_set_mode(w, *captured ? MOUSE_MODE_CAPTURED : MOUSE_MODE_NORMAL);
    }
    if (key_pressed(w, KEY_R))     g_reflections = !g_reflections;
    if (key_pressed(w, KEY_F))     g_shadows = !g_shadows;
    if (key_pressed(w, KEY_SPACE)) g_paused = !g_paused;

    if (!g_paused)
    {
        float speed = 2.0f * (float)dt;
        V3 fwd = cam_forward(&g_cam);
        V3 rgt = cam_right(&g_cam);
        if (key_down(w, KEY_W)) g_cam.pos = add(g_cam.pos, scale(fwd, speed));
        if (key_down(w, KEY_S)) g_cam.pos = sub(g_cam.pos, scale(fwd, speed));
        if (key_down(w, KEY_A)) g_cam.pos = sub(g_cam.pos, scale(rgt, speed));
        if (key_down(w, KEY_D)) g_cam.pos = add(g_cam.pos, scale(rgt, speed));
        if (key_down(w, KEY_E)) g_cam.pos.y += speed;
        if (key_down(w, KEY_Q)) g_cam.pos.y -= speed;
        if (*captured)
        {
            int dx, dy;
            mouse_delta(w, &dx, &dy);
            g_cam.yaw += (float)dx * 0.002f;
            g_cam.pitch -= (float)dy * 0.002f;
            if (g_cam.pitch > 1.55f)  g_cam.pitch = 1.55f;
            if (g_cam.pitch < -1.55f) g_cam.pitch = -1.55f;
        }
    }
}

/* ========================================================================== */
/*  App state & frame                                                          */
/* ========================================================================== */

typedef struct
{
    Framebuffer rt;
    int res_scale;
    bool captured;
    int frames, fps;
    double last_report, last_time, anim_time;
} App;

static int rt_width_for(int scale) { return 120 * scale; }
static int rt_height_for(int scale) { return 90 * scale; }

static void realloc_rt(App *app)
{
    framebuffer_free(&app->rt);
    framebuffer_alloc(&app->rt, rt_width_for(app->res_scale), rt_height_for(app->res_scale));
}

static void frame(PlatformWindow *w, void *user)
{
    App *app = user;
    double now = time_seconds();
    double dt = now - app->last_time;
    app->last_time = now;
    if (dt > 0.1) dt = 0.1;

    handle_input(w, &app->captured, dt);

    int prev = app->res_scale;
    if (key_pressed(w, KEY_ONE) && app->res_scale > 1) app->res_scale--;
    if (key_pressed(w, KEY_TWO) && app->res_scale < 4) app->res_scale++;
    if (app->res_scale != prev) realloc_rt(app);

    if (key_pressed(w, KEY_F12))
        take_screenshot(w, &app->rt);

    if (!g_paused)
        app->anim_time += dt;

    V3 light = v3(cosf((float)app->anim_time) * 2.0f, 2.0f, -1.0f + sinf((float)app->anim_time) * 1.5f);
    render(&app->rt, &g_cam, light);

    Framebuffer fb;
    if (window_lock_pixels(w, &fb))
    {
        draw_blit(&fb, &app->rt, 0, 0, app->rt.width, app->rt.height,
                  0, 0, fb.width, fb.height, BLEND_NONE, SCALE_BILINEAR);
        draw_hud(&fb, app->fps, app->rt.width, app->rt.height, app->captured, &g_cam);
        window_present_pixels(w);
    }

    app->frames++;
    if (now - app->last_report >= 1.0)
    {
        app->fps = app->frames;
        app->frames = 0;
        app->last_report = now;
        char title[64];
        snprintf(title, sizeof title, "raytrace - %d FPS - %dx%d", app->fps, app->rt.width, app->rt.height);
        window_set_title(w, title);
    }
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg = {.title = "raytrace", .width = 900, .height = 680,
                        .x = WINDOW_POS_CENTERED, .y = WINDOW_POS_CENTERED,
                        .render = RENDER_PIXELS, .resizable = true};
    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        fprintf(stderr, "window_create failed\n");
        return 1;
    }
    key_set_exit(w, KEY_ESCAPE);

    App app = {0};
    app.res_scale = 2;
    app.captured = false;
    realloc_rt(&app);
    app.last_time = time_seconds();
    app.last_report = app.last_time;

    g_cam.pos = v3(0.0f, 0.2f, 1.5f);
    g_cam.yaw = 0.0f;
    g_cam.pitch = 0.0f;
    mouse_set_mode(w, MOUSE_MODE_NORMAL);

    app_run(w, frame, &app);

    framebuffer_free(&app.rt);
    window_destroy(w);
    platform_shutdown();
    return 0;
}
