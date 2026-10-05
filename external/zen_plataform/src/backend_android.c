/*
 * backend_android.c - NativeActivity backend via native_app_glue. The system owns
 * both the loop and the lifecycle: android_main is the real entry, it wires the
 * glue callbacks and then calls the consumer's main(). The EGL context survives
 * background, but the surface is destroyed on TERM_WINDOW and rebuilt on
 * INIT_WINDOW (the resume gotcha). Assets and the data dir route through the
 * os_backend hooks so asset_* works against the APK.
 */
#include "core_internal.h"
#include "backend.h"
#include "os_backend.h"
#include "error_internal.h"
#include "clipboard_mem.h"
#include "vulkan_internal.h"

#include <android_native_app_glue.h>
#include <android/keycodes.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_window.h>
#include <EGL/egl.h>

#include <stdlib.h>
#include <string.h>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "platform", __VA_ARGS__)

static struct android_app *g_app;

struct BackendWindow
{
    EGLDisplay dpy;
    EGLSurface surface;
    EGLContext ctx;
    EGLConfig config;
    Core *core;

    int width, height;
    bool has_surface, focused, visible;
    WindowMode mode;

    RenderMode render;
    uint32_t *px; /* pixel mode staging buffer (0xAARRGGBB) */
    int px_w, px_h;
};

/* ========================================================================== */
/*  keycode translation                                                       */
/* ========================================================================== */

static int translate_key(int32_t code)
{
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z)
        return KEY_A + (code - AKEYCODE_A);
    if (code >= AKEYCODE_0 && code <= AKEYCODE_9)
        return KEY_ZERO + (code - AKEYCODE_0);
    if (code >= AKEYCODE_F1 && code <= AKEYCODE_F12)
        return KEY_F1 + (code - AKEYCODE_F1);
    if (code >= AKEYCODE_NUMPAD_0 && code <= AKEYCODE_NUMPAD_9)
        return KEY_KP_0 + (code - AKEYCODE_NUMPAD_0);

    switch (code)
    {
    case AKEYCODE_NUMPAD_DOT:
        return KEY_KP_DECIMAL;
    case AKEYCODE_NUMPAD_DIVIDE:
        return KEY_KP_DIVIDE;
    case AKEYCODE_NUMPAD_MULTIPLY:
        return KEY_KP_MULTIPLY;
    case AKEYCODE_NUMPAD_SUBTRACT:
        return KEY_KP_SUBTRACT;
    case AKEYCODE_NUMPAD_ADD:
        return KEY_KP_ADD;
    case AKEYCODE_NUMPAD_ENTER:
        return KEY_KP_ENTER;
    case AKEYCODE_NUMPAD_EQUALS:
        return KEY_KP_EQUAL;
    case AKEYCODE_MENU:
        return KEY_MENU;
    case AKEYCODE_SCROLL_LOCK:
        return KEY_SCROLL_LOCK;
    case AKEYCODE_INSERT:
        return KEY_INSERT;
    case AKEYCODE_CAPS_LOCK:
        return KEY_CAPS_LOCK;
    case AKEYCODE_NUM_LOCK:
        return KEY_NUM_LOCK;
    case AKEYCODE_SYSRQ:
        return KEY_PRINT_SCREEN;
    case AKEYCODE_BREAK:
        return KEY_PAUSE;
    case AKEYCODE_META_LEFT:
        return KEY_LEFT_SUPER;
    case AKEYCODE_META_RIGHT:
        return KEY_RIGHT_SUPER;
    case AKEYCODE_SPACE:
        return KEY_SPACE;
    case AKEYCODE_ENTER:
        return KEY_ENTER;
    case AKEYCODE_DEL:
        return KEY_BACKSPACE;
    case AKEYCODE_FORWARD_DEL:
        return KEY_DELETE;
    case AKEYCODE_TAB:
        return KEY_TAB;
    case AKEYCODE_ESCAPE:
        return KEY_ESCAPE;
    case AKEYCODE_BACK:
        return KEY_ESCAPE; /* the system back button */
    case AKEYCODE_DPAD_LEFT:
        return KEY_LEFT;
    case AKEYCODE_DPAD_RIGHT:
        return KEY_RIGHT;
    case AKEYCODE_DPAD_UP:
        return KEY_UP;
    case AKEYCODE_DPAD_DOWN:
        return KEY_DOWN;
    case AKEYCODE_MOVE_HOME:
        return KEY_HOME;
    case AKEYCODE_MOVE_END:
        return KEY_END;
    case AKEYCODE_PAGE_UP:
        return KEY_PAGE_UP;
    case AKEYCODE_PAGE_DOWN:
        return KEY_PAGE_DOWN;
    case AKEYCODE_SHIFT_LEFT:
        return KEY_LEFT_SHIFT;
    case AKEYCODE_SHIFT_RIGHT:
        return KEY_RIGHT_SHIFT;
    case AKEYCODE_CTRL_LEFT:
        return KEY_LEFT_CONTROL;
    case AKEYCODE_CTRL_RIGHT:
        return KEY_RIGHT_CONTROL;
    case AKEYCODE_ALT_LEFT:
        return KEY_LEFT_ALT;
    case AKEYCODE_ALT_RIGHT:
        return KEY_RIGHT_ALT;
    case AKEYCODE_MINUS:
        return KEY_MINUS;
    case AKEYCODE_EQUALS:
        return KEY_EQUAL;
    case AKEYCODE_COMMA:
        return KEY_COMMA;
    case AKEYCODE_PERIOD:
        return KEY_PERIOD;
    case AKEYCODE_SLASH:
        return KEY_SLASH;
    case AKEYCODE_BACKSLASH:
        return KEY_BACKSLASH;
    case AKEYCODE_SEMICOLON:
        return KEY_SEMICOLON;
    case AKEYCODE_APOSTROPHE:
        return KEY_APOSTROPHE;
    case AKEYCODE_GRAVE:
        return KEY_GRAVE;
    case AKEYCODE_LEFT_BRACKET:
        return KEY_LEFT_BRACKET;
    case AKEYCODE_RIGHT_BRACKET:
        return KEY_RIGHT_BRACKET;
    default:
        return KEY_NULL;
    }
}

/* ========================================================================== */
/*  EGL                                                                       */
/* ========================================================================== */

#ifndef EGL_CONTEXT_MINOR_VERSION_KHR
#define EGL_CONTEXT_MINOR_VERSION_KHR 0x30FB
#endif
#ifndef EGL_CONTEXT_FLAGS_KHR
#define EGL_CONTEXT_FLAGS_KHR 0x30FC
#endif
#ifndef EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR
#define EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR 0x00000001
#endif

static bool egl_init_context(BackendWindow *b, const GLConfig *gl)
{
    if (gl->profile != GL_PROFILE_DEFAULT && gl->profile != GL_PROFILE_ES)
        return error_set("only OpenGL ES contexts are available on Android");
    int major = gl->major ? gl->major : 3;
    int minor = gl->major ? gl->minor : 0;
    if (major < 2 || major > 3)
        return error_set("OpenGL ES %d.%d is not available on Android", major, minor);

    b->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (b->dpy == EGL_NO_DISPLAY || !eglInitialize(b->dpy, NULL, NULL))
        return error_set("cannot initialize EGL");

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, major >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_SAMPLE_BUFFERS, gl->msaa > 0 ? 1 : 0,
        EGL_SAMPLES, gl->msaa,
        EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(b->dpy, attribs, &b->config, 1, &count) || count == 0)
        return error_set("no EGL configuration with RGBA8, depth 24, stencil 8 and %d samples", gl->msaa);

    EGLint ctx_attribs[9];
    int n = 0;
    ctx_attribs[n++] = EGL_CONTEXT_CLIENT_VERSION;
    ctx_attribs[n++] = major;
    if (minor > 0)
    {
        ctx_attribs[n++] = EGL_CONTEXT_MINOR_VERSION_KHR;
        ctx_attribs[n++] = minor;
    }
    if (gl->debug)
    {
        ctx_attribs[n++] = EGL_CONTEXT_FLAGS_KHR;
        ctx_attribs[n++] = EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR;
    }
    ctx_attribs[n] = EGL_NONE;

    b->ctx = eglCreateContext(b->dpy, b->config, EGL_NO_CONTEXT, ctx_attribs);
    if (b->ctx == EGL_NO_CONTEXT)
        return error_set("cannot create an OpenGL ES %d.%d context%s (EGL error 0x%x)", major, minor,
                         gl->debug ? " with debug" : "", (unsigned)eglGetError());
    return true;
}

static bool egl_init_surface(BackendWindow *b)
{
    if (!g_app->window || b->has_surface)
        return b->has_surface;

    EGLint format;
    eglGetConfigAttrib(b->dpy, b->config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(g_app->window, 0, 0, format);

    b->surface = eglCreateWindowSurface(b->dpy, b->config, g_app->window, NULL);
    if (b->surface == EGL_NO_SURFACE)
        return false;
    eglMakeCurrent(b->dpy, b->surface, b->surface, b->ctx);
    eglQuerySurface(b->dpy, b->surface, EGL_WIDTH, &b->width);
    eglQuerySurface(b->dpy, b->surface, EGL_HEIGHT, &b->height);
    b->has_surface = true;
    return true;
}

static void egl_term_surface(BackendWindow *b)
{
    if (!b->has_surface)
        return;
    eglMakeCurrent(b->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(b->dpy, b->surface);
    b->surface = EGL_NO_SURFACE;
    b->has_surface = false;
}

/* Pixel mode: no EGL, the framebuffer goes straight to the ANativeWindow. */
/* A Vulkan window only records the size: the swapchain owns the buffers, so the
   window's format and geometry are left alone. */
static bool vulkan_init_surface(BackendWindow *b)
{
    if (!g_app->window)
        return false;
    b->width = ANativeWindow_getWidth(g_app->window);
    b->height = ANativeWindow_getHeight(g_app->window);
    b->has_surface = true;
    return true;
}

static bool native_init_surface(BackendWindow *b)
{
    if (!g_app->window || b->has_surface)
        return b->has_surface;
    ANativeWindow_setBuffersGeometry(g_app->window, 0, 0, WINDOW_FORMAT_RGBA_8888);
    b->width = ANativeWindow_getWidth(g_app->window);
    b->height = ANativeWindow_getHeight(g_app->window);
    b->has_surface = true;
    return true;
}

/* ========================================================================== */
/*  glue callbacks                                                            */
/* ========================================================================== */

static void push_to_core(BackendWindow *b, const Event *e)
{
    if (b->core)
        core_push_event(b->core, e);
}

static void handle_cmd(struct android_app *app, int32_t cmd)
{
    BackendWindow *b = app->userData;
    if (!b)
        return;

    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
    {
        bool ok = b->render == RENDER_GL       ? egl_init_surface(b)
                  : b->render == RENDER_VULKAN ? vulkan_init_surface(b)
                                               : native_init_surface(b);
        if (ok)
        {
            b->visible = true;
            Event r = {.type = EVENT_WINDOW_RESIZE};
            r.data.resize.w = b->width;
            r.data.resize.h = b->height;
            push_to_core(b, &r);
            Event fb = {.type = EVENT_WINDOW_FB_RESIZE};
            fb.data.resize.w = b->width;
            fb.data.resize.h = b->height;
            push_to_core(b, &fb);
            /* Not for the first window: backend_create has no core yet, so nothing
               is pushed then (push_to_core drops it). */
            Event ready = {.type = EVENT_WINDOW_SURFACE_READY};
            push_to_core(b, &ready);
        }
        break;
    }
    case APP_CMD_TERM_WINDOW:
    {
        if (b->render == RENDER_GL)
            egl_term_surface(b);
        else
            b->has_surface = false;
        b->visible = false;
        Event lost = {.type = EVENT_WINDOW_SURFACE_LOST};
        push_to_core(b, &lost);
        break;
    }
    case APP_CMD_GAINED_FOCUS:
    case APP_CMD_LOST_FOCUS:
    {
        b->focused = cmd == APP_CMD_GAINED_FOCUS;
        Event e = {.type = EVENT_WINDOW_FOCUS};
        e.data.focus.gained = b->focused;
        push_to_core(b, &e);
        break;
    }
    case APP_CMD_WINDOW_RESIZED:
        if (b->has_surface)
        {
            eglQuerySurface(b->dpy, b->surface, EGL_WIDTH, &b->width);
            eglQuerySurface(b->dpy, b->surface, EGL_HEIGHT, &b->height);
            Event r = {.type = EVENT_WINDOW_RESIZE};
            r.data.resize.w = b->width;
            r.data.resize.h = b->height;
            push_to_core(b, &r);
            Event fb = {.type = EVENT_WINDOW_FB_RESIZE};
            fb.data.resize.w = b->width;
            fb.data.resize.h = b->height;
            push_to_core(b, &fb);
        }
        break;
    }
}

static int32_t handle_input(struct android_app *app, AInputEvent *ev)
{
    BackendWindow *b = app->userData;
    if (!b)
        return 0;

    int32_t type = AInputEvent_getType(ev);
    if (type == AINPUT_EVENT_TYPE_KEY)
    {
        int32_t action = AKeyEvent_getAction(ev);
        Event e = {.type = EVENT_KEY};
        e.data.key.key = translate_key(AKeyEvent_getKeyCode(ev));
        e.data.key.down = action == AKEY_EVENT_ACTION_DOWN;
        e.data.key.repeat = AKeyEvent_getRepeatCount(ev) > 0;
        push_to_core(b, &e);
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_MOTION)
    {
        int32_t action = AMotionEvent_getAction(ev);
        int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
        int32_t pidx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                       AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        size_t count = AMotionEvent_getPointerCount(ev);

        TouchPhase phase;
        bool single; /* whether the event concerns one pointer or all of them */
        switch (masked)
        {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            phase = TOUCH_DOWN;
            single = true;
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            phase = TOUCH_UP;
            single = true;
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            phase = TOUCH_MOVE;
            single = false;
            break;
        default:
            phase = TOUCH_CANCEL;
            single = false;
            break;
        }

        for (size_t i = 0; i < count; i++)
        {
            if (single && (int32_t)i != pidx)
                continue;
            Event e = {.type = EVENT_TOUCH};
            e.data.touch.id = AMotionEvent_getPointerId(ev, i);
            e.data.touch.x = AMotionEvent_getX(ev, i);
            e.data.touch.y = AMotionEvent_getY(ev, i);
            e.data.touch.pressure = AMotionEvent_getPressure(ev, i);
            e.data.touch.phase = phase;
            push_to_core(b, &e);
        }
        return 1;
    }
    return 0;
}

/* ========================================================================== */
/*  backend interface                                                         */
/* ========================================================================== */

bool backend_init(void)
{
    return g_app != NULL;
}

void backend_shutdown(void)
{
}

BackendWindow *backend_create(const WindowConfig *cfg)
{
    if (!g_app)
        return NULL;

    BackendWindow *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->render = cfg->render;
    g_app->userData = b;

    if (cfg->render == RENDER_GL && !egl_init_context(b, &cfg->gl))
    {
        free(b);
        g_app->userData = NULL;
        return NULL;
    }

    /* Block until the activity hands us a window (APP_CMD_INIT_WINDOW). */
    while (!b->has_surface && !g_app->destroyRequested)
    {
        int events;
        struct android_poll_source *source;
        if (ALooper_pollOnce(-1, NULL, &events, (void **)&source) >= 0 && source)
            source->process(g_app, source);
    }
    return b;
}

void backend_destroy(BackendWindow *b)
{
    if (!b)
        return;
    if (b->render == RENDER_GL)
    {
        egl_term_surface(b);
        if (b->ctx != EGL_NO_CONTEXT)
            eglDestroyContext(b->dpy, b->ctx);
        if (b->dpy != EGL_NO_DISPLAY)
            eglTerminate(b->dpy);
    }
    free(b->px);
    if (g_app)
        g_app->userData = NULL;
    free(b);
}

void backend_pump_events(BackendWindow *b, Core *core)
{
    b->core = core;
    /* Block when there is no surface (backgrounded) instead of spinning. */
    int timeout = b->has_surface ? 0 : -1;
    int events;
    struct android_poll_source *source;
    while (ALooper_pollOnce(timeout, NULL, &events, (void **)&source) >= 0)
    {
        if (source)
            source->process(g_app, source);
        timeout = 0;
        if (g_app->destroyRequested)
        {
            Event e = {.type = EVENT_WINDOW_CLOSE};
            push_to_core(b, &e);
            break;
        }
    }
}

/* ========================================================================== */
/*  native handles and Vulkan                                                 */
/* ========================================================================== */

void *backend_native_handle(BackendWindow *b, NativeHandleType type)
{
    switch (type)
    {
    case NATIVE_DISPLAY:
        return b->render == RENDER_GL ? (void *)b->dpy : NULL;
    case NATIVE_WINDOW:
        return b->has_surface && g_app ? (void *)g_app->window : NULL;
    case NATIVE_GL_CONTEXT:
        return b->render == RENDER_GL ? (void *)b->ctx : NULL;
    }
    return NULL;
}

const char *const *backend_vulkan_extensions(uint32_t *count)
{
    static const char *const ext[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
    *count = 2;
    return ext;
}

typedef struct
{
    int sType;
    const void *pNext;
    uint32_t flags;
    ANativeWindow *window;
} ZenVkAndroidSurfaceCreateInfo;

bool backend_vulkan_create_surface(BackendWindow *b, void *instance, const void *allocator, uint64_t *out_surface)
{
    if (!b->has_surface || !g_app || !g_app->window)
        return error_set("the Android window is not available (app in the background?)");
    ZenVkAndroidSurfaceCreateInfo info = {
        .sType = ZEN_VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = g_app->window,
    };
    return vulkan_call_create_surface(instance, "vkCreateAndroidSurfaceKHR", &info, allocator, out_surface);
}

void backend_swap(BackendWindow *b)
{
    if (b->render == RENDER_GL && b->has_surface)
        eglSwapBuffers(b->dpy, b->surface);
}

void backend_run(BackendWindow *b, PlatformWindow *w, FrameCallback frame, void *user)
{
    (void)b;
    while (!window_should_close(w))
    {
        window_begin_frame(w);
        frame(w, user);
        window_swap(w);
    }
}

void backend_make_current(BackendWindow *b)
{
    if (b->render == RENDER_GL && b->has_surface)
        eglMakeCurrent(b->dpy, b->surface, b->surface, b->ctx);
}

void backend_make_current_on(BackendWindow *target, BackendWindow *context)
{
    if (target == context)
        backend_make_current(context);
}

void backend_set_vsync(BackendWindow *b, bool on)
{
    if (b->render == RENDER_GL)
        eglSwapInterval(b->dpy, on ? 1 : 0);
}

bool backend_lock_pixels(BackendWindow *b, Framebuffer *out)
{
    if (b->render != RENDER_PIXELS || !b->has_surface || !out)
        return false;
    if (!b->px || b->px_w != b->width || b->px_h != b->height)
    {
        free(b->px);
        b->px_w = b->width;
        b->px_h = b->height;
        b->px = calloc((size_t)b->px_w * b->px_h, sizeof *b->px);
    }
    out->pixels = b->px;
    out->width = b->px_w;
    out->height = b->px_h;
    out->stride = b->px_w;
    return b->px != NULL;
}

void backend_present_pixels(BackendWindow *b)
{
    if (b->render != RENDER_PIXELS || !b->has_surface || !b->px || !g_app->window)
        return;
    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(g_app->window, &buf, NULL) != 0)
        return;
    /* Swizzle 0xAARRGGBB (BGRA in memory) into the RGBA8888 surface. */
    int rows = buf.height < b->px_h ? buf.height : b->px_h;
    int cols = buf.width < b->px_w ? buf.width : b->px_w;
    uint32_t *dst = buf.bits;
    for (int y = 0; y < rows; y++)
    {
        const uint32_t *s = b->px + (size_t)y * b->px_w;
        uint32_t *d = dst + (size_t)y * buf.stride;
        for (int x = 0; x < cols; x++)
        {
            uint32_t p = s[x];
            d[x] = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
        }
    }
    ANativeWindow_unlockAndPost(g_app->window);
}

void *backend_gl_proc_address(const char *name)
{
    return (void *)eglGetProcAddress(name);
}

void backend_get_size(BackendWindow *b, int *w, int *h)
{
    if (w)
        *w = b->width;
    if (h)
        *h = b->height;
}

void backend_set_size(BackendWindow *b, int w, int h)
{
    (void)b;
    (void)w;
    (void)h; /* the system owns the surface size */
}

void backend_get_fb_size(BackendWindow *b, int *w, int *h)
{
    if (w)
        *w = b->width;
    if (h)
        *h = b->height;
}

void backend_get_pos(BackendWindow *b, int *x, int *y)
{
    (void)b;
    if (x)
        *x = 0;
    if (y)
        *y = 0;
}

void backend_set_pos(BackendWindow *b, int x, int y)
{
    (void)b;
    (void)x;
    (void)y;
}

float backend_content_scale(BackendWindow *b)
{
    (void)b;
    if (g_app && g_app->config)
        return AConfiguration_getDensity(g_app->config) / 160.0f;
    return 1.0f;
}

void backend_set_title(BackendWindow *b, const char *title)
{
    (void)b;
    (void)title;
}
void backend_set_size_limits(BackendWindow *b, int minw, int minh, int maxw, int maxh)
{
    (void)b;
    (void)minw;
    (void)minh;
    (void)maxw;
    (void)maxh;
}

void backend_minimize(BackendWindow *b)
{
    (void)b;
}
void backend_maximize(BackendWindow *b)
{
    (void)b;
}
void backend_restore(BackendWindow *b)
{
    (void)b;
}
void backend_show(BackendWindow *b)
{
    (void)b;
}
void backend_hide(BackendWindow *b)
{
    (void)b;
}
void backend_focus(BackendWindow *b)
{
    (void)b;
}
void backend_request_attention(BackendWindow *b)
{
    (void)b;
}

bool backend_get_flag(BackendWindow *b, int flag)
{
    switch (flag)
    {
    case WIN_FLAG_FOCUSED:
        return b->focused;
    case WIN_FLAG_VISIBLE:
        return b->visible;
    default:
        return false;
    }
}

void backend_set_mode(BackendWindow *b, WindowMode mode, int monitor)
{
    (void)monitor;
    b->mode = mode; /* Android apps are effectively always fullscreen */
}

WindowMode backend_get_mode(BackendWindow *b)
{
    return b->mode;
}

void backend_set_icon(BackendWindow *b, int w, int h, const uint8_t *rgba)
{
    (void)b;
    (void)w;
    (void)h;
    (void)rgba;
}
void backend_set_opacity(BackendWindow *b, float a)
{
    (void)b;
    (void)a;
}
void backend_set_always_on_top(BackendWindow *b, bool on)
{
    (void)b;
    (void)on;
}

void backend_set_mouse_pos(BackendWindow *b, int x, int y)
{
    (void)b;
    (void)x;
    (void)y;
}
void backend_set_cursor(BackendWindow *b, int cursor)
{
    (void)b;
    (void)cursor;
}
void backend_set_live_callback(BackendWindow *b, PlatformWindow *w, FrameCallback cb, void *user)
{
    (void)b, (void)w, (void)cb, (void)user;
}

int backend_lock_state(void)
{
    return 0;
}

bool backend_mouse_capture(BackendWindow *b, bool on)
{
    (void)b, (void)on;
    return false;
}

void backend_set_decorated(BackendWindow *b, bool on)
{
    (void)b, (void)on;
}

void backend_set_hit_test(BackendWindow *b, PlatformWindow *w, HitTestFunc fn, void *user)
{
    (void)b, (void)w, (void)fn, (void)user;
}

PlatformCursor *backend_cursor_create(const uint32_t *argb, int w, int h, int hot_x, int hot_y)
{
    (void)argb, (void)w, (void)h, (void)hot_x, (void)hot_y;
    error_set("cursors from an image are not supported here");
    return NULL;
}

void backend_cursor_destroy(PlatformCursor *c)
{
    (void)c;
}

void backend_set_cursor_image(BackendWindow *b, PlatformCursor *c)
{
    (void)b, (void)c;
}

void backend_set_text_input(BackendWindow *b, bool on)
{
    (void)b, (void)on;
}

void backend_set_text_input_rect(BackendWindow *b, int x, int y, int w, int h)
{
    (void)b, (void)x, (void)y, (void)w, (void)h;
}

void backend_set_mouse_mode(BackendWindow *b, int mode)
{
    (void)b;
    (void)mode;
}

/* No system clipboard yet: the content stays inside the app. */
bool backend_clipboard_set(const ClipboardItem *items, int count)
{
    return clipmem_set(items, count);
}
bool backend_clipboard_has(const char *mime)
{
    return clipmem_has(mime);
}
void *backend_clipboard_get(const char *mime, size_t *size)
{
    return clipmem_get(mime, size);
}

bool backend_mouse_global_position(int *x, int *y)
{
    (void)x;
    (void)y;
    return false;
}

int backend_monitor_count(void)
{
    return 1;
}

bool backend_monitor_info(int index, MonitorInfo *out)
{
    if (index != 0 || !out)
        return false;
    BackendWindow *b = g_app ? g_app->userData : NULL;
    memset(out, 0, sizeof *out);
    out->index = 0;
    out->name = "display";
    out->width = b ? b->width : 0;
    out->height = b ? b->height : 0;
    out->work_w = out->width;
    out->work_h = out->height;
    out->refresh_hz = 60;
    out->content_scale = backend_content_scale(b);
    out->primary = true;
    return true;
}

/* ========================================================================== */
/*  os_backend hooks - assets come from the APK, data from internal storage   */
/* ========================================================================== */

AAssetManager *android_asset_manager(void)
{
    return (g_app && g_app->activity) ? g_app->activity->assetManager : NULL;
}

const char *os_backend_data_dir(void)
{
    return (g_app && g_app->activity) ? g_app->activity->internalDataPath : NULL;
}

/* ========================================================================== */
/*  entry point - the glue calls this; we wire callbacks and run user main()  */
/* ========================================================================== */

extern int main(void);

void android_main(struct android_app *app)
{
    g_app = app;
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    main();

    g_app = NULL;
}
