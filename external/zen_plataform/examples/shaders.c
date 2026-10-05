/* shaders.c - GL shader demo. Shows gl_loader usage with a rotating triangle.
 * Requires RENDER_GL window. Uses gl_proc_address() via gl_loader_load(). */
#include "platform.h"
#include <glad/gl.h>

#include <math.h>
#include <stdio.h>

static GLuint g_program;
static GLuint g_vao;
static float g_t;

/* ---- shader helper ---- */
static GLuint make_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        printf("shader compile error: %s\n", log);
    }
    return s;
}

static GLuint make_program(const char *vs, const char *fs)
{
    GLuint v = make_shader(GL_VERTEX_SHADER, vs);
    GLuint f = make_shader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        printf("program link error: %s\n", log);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

/* ---- shader sources ---- */
static const char *g_vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "uniform float uTime;\n"
    "void main() {\n"
    "    float s = sin(uTime), c = cos(uTime);\n"
    "    mat2 rot = mat2(c, -s, s, c);\n"
    "    vec2 p = rot * aPos.xy;\n"
    "    gl_Position = vec4(p, aPos.z * 0.5, 1.0);\n"
    "}\n";

static const char *g_fs =
    "#version 330 core\n"
    "out vec4 fragColor;\n"
    "uniform float uTime;\n"
    "void main() {\n"
    "    float r = sin(uTime) * 0.5 + 0.5;\n"
    "    float g = cos(uTime + 1.0) * 0.5 + 0.5;\n"
    "    float b = sin(uTime + 2.0) * 0.5 + 0.5;\n"
    "    fragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

/* ---- setup ---- */
static void setup(void)
{
    float verts[] = {
        0.0f,
        0.6f,
        0.0f,
        -0.5f,
        -0.4f,
        0.0f,
        0.5f,
        -0.4f,
        0.0f,
    };

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
    glEnableVertexAttribArray(0);

    g_program = make_program(g_vs, g_fs);

    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    printf("program: %u  vao: %u\n", g_program, g_vao);
}

/* ---- frame ---- */
static void frame(PlatformWindow *w, void *user)
{
    (void)user;
    Event e;
    while (poll_event(w, &e))
    {
    }

    g_t = (float)time_seconds();

    int fw, fh;
    window_get_framebuffer_size(w, &fw, &fh);
    glViewport(0, 0, fw, fh);

    glClearColor(0.10f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_program);
    GLint loc = glGetUniformLocation(g_program, "uTime");
    if (loc >= 0)
        glUniform1f(loc, g_t);

    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg = {
        .title = "shaders", .width = 800, .height = 600, .gl = {.major = 3, .minor = 3}, .x = WINDOW_POS_CENTERED, .y = WINDOW_POS_CENTERED, .render = RENDER_GL};
    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        fprintf(stderr, "window_create failed\n");
        return 1;
    }
    window_make_current(w);

    if (!gladLoadGL((GLADloadfunc)gl_proc_address))
    {
        fprintf(stderr, "gladLoadGL failed\n");
        return 1;
    }

    setup();
    key_set_exit(w, KEY_ESCAPE);
    app_run(w, frame, NULL);

    window_destroy(w);
    platform_shutdown();
    return 0;
}