/*
 * r2d.c - batched 2D engine on OpenGL 3.3 core (desktop) and GLES3 / WebGL2.
 *
 * One dynamic vertex buffer, one shader, one bound texture at a time. Geometry
 * accumulates into a CPU array and flushes on a texture change, a clip change,
 * or when the buffer fills. Untextured shapes sample a 1x1 white texture so they
 * batch together with textured draws under the same shader.
 *
 * Coordinates are framebuffer pixels, origin top-left. Color is 0xAARRGGBB,
 * matching Framebuffer; textures upload as RGBA and the shader swizzles the
 * channels back, so the same path works on desktop GL and GLES/WebGL2.
 */
#include "zen_2d.h"

/* GL on desktop comes through glad; web and mobile use GLES3 headers and an
   already-current context (no loader). One GLES3/WebGL2 core path covers both. */
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#define R2D_GLES 1
#include <GLES3/gl3.h>
#else
#include <glad/gl.h>
#endif

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    float         x, y, u, v;
    unsigned char r, g, b, a;
} R2dVertex;

static struct
{
    bool      ready;
    GLuint    program, vao, vbo, white;
    GLint     u_scale, u_tex;
    R2dVertex *verts;
    int       count, cap;
    GLuint    cur_tex;
    int       fb_w, fb_h;
    bool      clip_on;
    int       clip_x, clip_y, clip_w, clip_h;
    unsigned  draw_calls; /* per frame, for profiling the batch */
} g;

/* ---- shaders ---- */

/* Version line and float precision differ between desktop core and GLES/WebGL2;
   the bodies are identical (GLSL ES 3.00 has in/out and layout locations too). */
#ifdef R2D_GLES
static const char *VS_HEAD = "#version 300 es\n";
static const char *FS_HEAD = "#version 300 es\nprecision mediump float;\n";
#else
static const char *VS_HEAD = "#version 330 core\n";
static const char *FS_HEAD = "#version 330 core\n";
#endif

static const char *VS_BODY =
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec4 aColor;\n"
    "uniform vec2 uScale;\n"
    "out vec2 vUV; out vec4 vColor;\n"
    "void main(){ vUV=aUV; vColor=aColor;\n"
    "  gl_Position=vec4(aPos.x*uScale.x-1.0, 1.0-aPos.y*uScale.y, 0.0, 1.0); }\n";

/* Textures upload as RGBA (GL_BGRA is not in GLES/WebGL); our pixels are
   0xAARRGGBB in memory (B,G,R,A), so swizzle .bgra to put them back in order. */
static const char *FS_BODY =
    "in vec2 vUV; in vec4 vColor;\n"
    "uniform sampler2D uTex;\n"
    "out vec4 frag;\n"
    "void main(){ frag = vColor * texture(uTex, vUV).bgra; }\n";

static GLuint compile(GLenum type, const char *head, const char *body)
{
    const char *src[2] = {head, body};
    GLuint s = glCreateShader(type);
    glShaderSource(s, 2, src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool r2d_init(void)
{
    if (g.ready)
        return true;
#ifndef R2D_GLES
    if (!gladLoadGL((GLADloadfunc)gl_proc_address))
        return false;
#endif

    GLuint vs = compile(GL_VERTEX_SHADER, VS_HEAD, VS_BODY);
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS_HEAD, FS_BODY);
    if (!vs || !fs)
        return false;
    g.program = glCreateProgram();
    glAttachShader(g.program, vs);
    glAttachShader(g.program, fs);
    glLinkProgram(g.program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(g.program, GL_LINK_STATUS, &ok);
    if (!ok)
        return false;
    g.u_scale = glGetUniformLocation(g.program, "uScale");
    g.u_tex = glGetUniformLocation(g.program, "uTex");

    glGenVertexArrays(1, &g.vao);
    glGenBuffers(1, &g.vbo);
    glBindVertexArray(g.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(R2dVertex), (void *)offsetof(R2dVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(R2dVertex), (void *)offsetof(R2dVertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(R2dVertex), (void *)offsetof(R2dVertex, r));
    glBindVertexArray(0);

    uint32_t white = 0xFFFFFFFFu;
    g.white = r2d_texture_create(&white, 1, 1).id;

    g.cap = 65536; /* ~10k quads per flush; keeps draw calls low for big scenes */
    g.verts = (R2dVertex *)malloc((size_t)g.cap * sizeof(R2dVertex));
    g.ready = g.verts != NULL;
    return g.ready;
}

void r2d_shutdown(void)
{
    if (!g.ready)
        return;
    glDeleteTextures(1, &g.white);
    glDeleteBuffers(1, &g.vbo);
    glDeleteVertexArrays(1, &g.vao);
    glDeleteProgram(g.program);
    free(g.verts);
    memset(&g, 0, sizeof(g));
}

/* ---- batch ---- */

/* Everything is GL_TRIANGLES, so a flush is only ever forced by a texture
   change, a clip change, or the buffer filling - never by a primitive type.
   Drawing 100 lines in a row is one draw call, not 100. */
void r2d_flush(void)
{
    if (g.count == 0)
        return;
    glBindVertexArray(g.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)g.count * sizeof(R2dVertex), NULL, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)g.count * sizeof(R2dVertex), g.verts);
    glDrawArrays(GL_TRIANGLES, 0, g.count);
    g.count = 0;
    g.draw_calls++;
}

unsigned r2d_draw_calls(void)
{
    return g.draw_calls;
}

static void use_texture(GLuint tex)
{
    if (tex != g.cur_tex)
    {
        r2d_flush();
        g.cur_tex = tex;
        glBindTexture(GL_TEXTURE_2D, tex);
    }
}

/* Reserve space for n vertices. Flushing only happens at a primitive boundary
   so a triangle is never split across draw calls. */
static void ensure(int n)
{
    if (g.count + n > g.cap)
        r2d_flush();
}

static void push(float x, float y, float u, float v, uint32_t c)
{
    R2dVertex *vt = &g.verts[g.count++];
    vt->x = x;
    vt->y = y;
    vt->u = u;
    vt->v = v;
    vt->r = (c >> 16) & 0xFF;
    vt->g = (c >> 8) & 0xFF;
    vt->b = c & 0xFF;
    vt->a = (c >> 24) & 0xFF;
}

/* A textured quad as two triangles. Six vertices keep the path index-free. */
static void quad(float x, float y, float w, float h,
                 float u0, float v0, float u1, float v1, uint32_t c)
{
    ensure(6);
    push(x, y, u0, v0, c);
    push(x + w, y, u1, v0, c);
    push(x + w, y + h, u1, v1, c);
    push(x, y, u0, v0, c);
    push(x + w, y + h, u1, v1, c);
    push(x, y + h, u0, v1, c);
}

/* ---- frame ---- */

void r2d_begin(int fb_w, int fb_h)
{
    g.fb_w = fb_w;
    g.fb_h = fb_h;
    g.count = 0;
    g.draw_calls = 0;
    glViewport(0, 0, fb_w, fb_h);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(g.program);
    glUniform2f(g.u_scale, 2.0f / (float)fb_w, 2.0f / (float)fb_h);
    glUniform1i(g.u_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    g.cur_tex = g.white;
    glBindTexture(GL_TEXTURE_2D, g.white);
    r2d_clip_none();
}

void r2d_end(void)
{
    r2d_flush();
    if (g.clip_on)
        r2d_clip_none();
}

/* ---- clip (GL scissor is bottom-left; convert from top-left) ---- */

void r2d_clip(int x, int y, int w, int h)
{
    /* Idempotent: re-setting the same scissor keeps the batch going. */
    if (g.clip_on && g.clip_x == x && g.clip_y == y && g.clip_w == w && g.clip_h == h)
        return;
    r2d_flush();
    g.clip_on = true;
    g.clip_x = x;
    g.clip_y = y;
    g.clip_w = w;
    g.clip_h = h;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, g.fb_h - (y + h), w < 0 ? 0 : w, h < 0 ? 0 : h);
}

void r2d_clip_none(void)
{
    if (!g.clip_on)
        return;
    r2d_flush();
    g.clip_on = false;
    glDisable(GL_SCISSOR_TEST);
}

/* ---- shapes ---- */

void r2d_rect(float x, float y, float w, float h, uint32_t color)
{
    use_texture(g.white);
    quad(x, y, w, h, 0.5f, 0.5f, 0.5f, 0.5f, color);
}

void r2d_triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color)
{
    use_texture(g.white);
    ensure(3);
    push(x0, y0, 0.5f, 0.5f, color);
    push(x1, y1, 0.5f, 0.5f, color);
    push(x2, y2, 0.5f, 0.5f, color);
}

void r2d_line(float x0, float y0, float x1, float y1, float thickness, uint32_t color)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f)
        return;
    float nx = -dy / len * thickness * 0.5f;
    float ny = dx / len * thickness * 0.5f;
    use_texture(g.white);
    ensure(6);
    push(x0 + nx, y0 + ny, 0.5f, 0.5f, color);
    push(x1 + nx, y1 + ny, 0.5f, 0.5f, color);
    push(x1 - nx, y1 - ny, 0.5f, 0.5f, color);
    push(x0 + nx, y0 + ny, 0.5f, 0.5f, color);
    push(x1 - nx, y1 - ny, 0.5f, 0.5f, color);
    push(x0 - nx, y0 - ny, 0.5f, 0.5f, color);
}

#define R2D_PI 3.14159265358979f

/* Segment count for a circle of radius r: more segments as it grows. */
static int circle_segs(float r)
{
    int n = (int)(r * 0.6f) + 8;
    return n < 8 ? 8 : (n > 64 ? 64 : n);
}

/* Filled arc fan around (cx,cy) with radii (rx,ry), from a0 to a1 radians. */
static void fan(float cx, float cy, float rx, float ry, float a0, float a1, int segs, uint32_t c)
{
    use_texture(g.white);
    for (int i = 0; i < segs; ++i)
    {
        float t0 = a0 + (a1 - a0) * (float)i / (float)segs;
        float t1 = a0 + (a1 - a0) * (float)(i + 1) / (float)segs;
        ensure(3);
        push(cx, cy, 0.5f, 0.5f, c);
        push(cx + rx * cosf(t0), cy + ry * sinf(t0), 0.5f, 0.5f, c);
        push(cx + rx * cosf(t1), cy + ry * sinf(t1), 0.5f, 0.5f, c);
    }
}

void r2d_circle(float cx, float cy, float r, uint32_t color)
{
    fan(cx, cy, r, r, 0.0f, 2.0f * R2D_PI, circle_segs(r), color);
}

void r2d_ellipse(float cx, float cy, float rx, float ry, uint32_t color)
{
    fan(cx, cy, rx, ry, 0.0f, 2.0f * R2D_PI, circle_segs(rx > ry ? rx : ry), color);
}

void r2d_circle_line(float cx, float cy, float r, float thickness, uint32_t color)
{
    int segs = circle_segs(r);
    float ri = r - thickness;
    use_texture(g.white);
    for (int i = 0; i < segs; ++i)
    {
        float t0 = 2.0f * R2D_PI * (float)i / (float)segs;
        float t1 = 2.0f * R2D_PI * (float)(i + 1) / (float)segs;
        float c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
        ensure(6);
        push(cx + ri * c0, cy + ri * s0, 0.5f, 0.5f, color);
        push(cx + r * c0, cy + r * s0, 0.5f, 0.5f, color);
        push(cx + r * c1, cy + r * s1, 0.5f, 0.5f, color);
        push(cx + ri * c0, cy + ri * s0, 0.5f, 0.5f, color);
        push(cx + r * c1, cy + r * s1, 0.5f, 0.5f, color);
        push(cx + ri * c1, cy + ri * s1, 0.5f, 0.5f, color);
    }
}

void r2d_polygon(const float *xy, int count, uint32_t color)
{
    if (count < 3)
        return;
    use_texture(g.white);
    for (int i = 1; i + 1 < count; ++i)
    {
        ensure(3);
        push(xy[0], xy[1], 0.5f, 0.5f, color);
        push(xy[2 * i], xy[2 * i + 1], 0.5f, 0.5f, color);
        push(xy[2 * i + 2], xy[2 * i + 3], 0.5f, 0.5f, color);
    }
}

void r2d_rect_line(float x, float y, float w, float h, float t, uint32_t color)
{
    r2d_rect(x, y, w, t, color);
    r2d_rect(x, y + h - t, w, t, color);
    r2d_rect(x, y + t, t, h - 2 * t, color);
    r2d_rect(x + w - t, y + t, t, h - 2 * t, color);
}

static float clamp_radius(float w, float h, float r)
{
    float m = (w < h ? w : h) * 0.5f;
    return r > m ? m : r;
}

void r2d_round_rect(float x, float y, float w, float h, float radius, uint32_t color)
{
    float r = clamp_radius(w, h, radius);
    if (r <= 0.5f)
    {
        r2d_rect(x, y, w, h, color);
        return;
    }
    r2d_rect(x + r, y, w - 2 * r, h, color);
    r2d_rect(x, y + r, r, h - 2 * r, color);
    r2d_rect(x + w - r, y + r, r, h - 2 * r, color);
    int segs = circle_segs(r);
    fan(x + r, y + r, r, r, R2D_PI, 1.5f * R2D_PI, segs, color);
    fan(x + w - r, y + r, r, r, 1.5f * R2D_PI, 2.0f * R2D_PI, segs, color);
    fan(x + w - r, y + h - r, r, r, 0.0f, 0.5f * R2D_PI, segs, color);
    fan(x + r, y + h - r, r, r, 0.5f * R2D_PI, R2D_PI, segs, color);
}

void r2d_round_rect_line(float x, float y, float w, float h, float radius, float t, uint32_t color)
{
    float r = clamp_radius(w, h, radius);
    if (r <= 0.5f)
    {
        r2d_rect_line(x, y, w, h, t, color);
        return;
    }
    r2d_rect(x + r, y, w - 2 * r, t, color);
    r2d_rect(x + r, y + h - t, w - 2 * r, t, color);
    r2d_rect(x, y + r, t, h - 2 * r, color);
    r2d_rect(x + w - t, y + r, t, h - 2 * r, color);
    /* corner arcs as short line segments */
    struct { float cx, cy, a0; } corner[4] = {
        {x + r, y + r, R2D_PI}, {x + w - r, y + r, 1.5f * R2D_PI},
        {x + w - r, y + h - r, 0.0f}, {x + r, y + h - r, 0.5f * R2D_PI}};
    int segs = circle_segs(r);
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < segs; ++i)
        {
            float t0 = corner[k].a0 + 0.5f * R2D_PI * (float)i / (float)segs;
            float t1 = corner[k].a0 + 0.5f * R2D_PI * (float)(i + 1) / (float)segs;
            r2d_line(corner[k].cx + r * cosf(t0), corner[k].cy + r * sinf(t0),
                     corner[k].cx + r * cosf(t1), corner[k].cy + r * sinf(t1), t, color);
        }
}

void r2d_rect_gradient_v(float x, float y, float w, float h, uint32_t top, uint32_t bottom)
{
    use_texture(g.white);
    ensure(6);
    push(x, y, 0.5f, 0.5f, top);
    push(x + w, y, 0.5f, 0.5f, top);
    push(x + w, y + h, 0.5f, 0.5f, bottom);
    push(x, y, 0.5f, 0.5f, top);
    push(x + w, y + h, 0.5f, 0.5f, bottom);
    push(x, y + h, 0.5f, 0.5f, bottom);
}

/* ---- textures ---- */

R2dTexture r2d_texture_create(const uint32_t *pixels, int w, int h)
{
    R2dTexture t = {0, w, h};
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, g.cur_tex);
    return t;
}

R2dTexture r2d_texture_from_framebuffer(const Framebuffer *fb)
{
    /* Framebuffer rows may be padded (stride > width); upload row by row. */
    if (fb->stride == fb->width)
        return r2d_texture_create(fb->pixels, fb->width, fb->height);
    uint32_t *tight = (uint32_t *)malloc((size_t)fb->width * fb->height * sizeof(uint32_t));
    R2dTexture t = {0, 0, 0};
    if (!tight)
        return t;
    for (int y = 0; y < fb->height; ++y)
        memcpy(tight + (size_t)y * fb->width, fb->pixels + (size_t)y * fb->stride,
               (size_t)fb->width * sizeof(uint32_t));
    t = r2d_texture_create(tight, fb->width, fb->height);
    free(tight);
    return t;
}

void r2d_texture_update(R2dTexture tex, const uint32_t *pixels, int w, int h)
{
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, g.cur_tex);
}

void r2d_texture_subimage(R2dTexture tex, int x, int y, int w, int h, const uint32_t *pixels)
{
    /* Flush first: a bind behind the batch's back would draw buffered geometry
       against the wrong texture. Restore the batch's texture after. */
    r2d_flush();
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, g.cur_tex);
}

void r2d_texture_free(R2dTexture *tex)
{
    if (tex && tex->id)
    {
        glDeleteTextures(1, &tex->id);
        tex->id = 0;
    }
}

void r2d_texture_draw(R2dTexture tex, float x, float y, float w, float h, uint32_t tint)
{
    r2d_texture_draw_uv(tex, 0, 0, 1, 1, x, y, w, h, tint);
}

void r2d_texture_draw_uv(R2dTexture tex, float u0, float v0, float u1, float v1,
                         float x, float y, float w, float h, uint32_t tint)
{
    use_texture(tex.id);
    quad(x, y, w, h, u0, v0, u1, v1, tint);
}

void r2d_sprite(R2dTexture tex,
                float sx, float sy, float sw, float sh,
                float dx, float dy, float ox, float oy,
                float scale_x, float scale_y, float rot_deg, int flip,
                uint32_t tint)
{
    if (sw <= 0)
        sw = (float)tex.w;
    if (sh <= 0)
        sh = (float)tex.h;

    /* UVs from the source rect, swapped on flip. */
    float u0 = sx / tex.w, v0 = sy / tex.h;
    float u1 = (sx + sw) / tex.w, v1 = (sy + sh) / tex.h;
    if (flip & R2D_FLIP_X) { float t = u0; u0 = u1; u1 = t; }
    if (flip & R2D_FLIP_Y) { float t = v0; v0 = v1; v1 = t; }

    /* Local corners around the origin (source pixels), then scale + rotate. */
    float lx[4] = {-ox, sw - ox, sw - ox, -ox};
    float ly[4] = {-oy, -oy, sh - oy, sh - oy};
    float rad = rot_deg * R2D_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    float px[4], py[4];
    for (int i = 0; i < 4; ++i)
    {
        float x = lx[i] * scale_x, y = ly[i] * scale_y;
        px[i] = dx + x * c - y * s;
        py[i] = dy + x * s + y * c;
    }
    float u[4] = {u0, u1, u1, u0};
    float v[4] = {v0, v0, v1, v1};

    use_texture(tex.id);
    ensure(6);
    push(px[0], py[0], u[0], v[0], tint);
    push(px[1], py[1], u[1], v[1], tint);
    push(px[2], py[2], u[2], v[2], tint);
    push(px[0], py[0], u[0], v[0], tint);
    push(px[2], py[2], u[2], v[2], tint);
    push(px[3], py[3], u[3], v[3], tint);
}
