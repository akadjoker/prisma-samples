/*
 * backend_web.c - Emscripten backend. The browser owns the loop, so app_run
 * registers a main-loop callback and never returns. html5.h event callbacks fire
 * asynchronously; they queue normalized events into a pending buffer that
 * backend_pump_events drains into the core, the same shape as the fake backend.
 *
 * Geometry has two real spaces here: the canvas CSS size is screen coordinates,
 * the canvas drawing-buffer size is pixels, and devicePixelRatio bridges them.
 */
#include "core_internal.h"
#include "backend.h"
#include "error_internal.h"
#include "clipboard_mem.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include <stdlib.h>
#include <string.h>

#define WEB_PENDING_MAX 256
#define CANVAS_TARGET "#canvas"

typedef struct
{
    bool is_char;
    Event ev;
    uint32_t cp;
} WebItem;

struct BackendWindow
{
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE gl;
    Core *core;

    WebItem pending[WEB_PENDING_MAX];
    int pending_count;

    int css_w, css_h; /* screen coords */
    bool focused, visible;
    int cursor_mode;

    RenderMode render;
    uint32_t *px; /* pixel mode staging buffer (0xAARRGGBB) */
    int px_w, px_h;
};

/* The browser hands a single canvas, so a single backend window is in play. */
static BackendWindow *g_active;

static void enqueue_event(BackendWindow *b, const Event *ev)
{
    if (b->pending_count < WEB_PENDING_MAX)
    {
        b->pending[b->pending_count].is_char = false;
        b->pending[b->pending_count].ev = *ev;
        b->pending_count++;
    }
}

static void enqueue_char(BackendWindow *b, uint32_t cp)
{
    if (b->pending_count < WEB_PENDING_MAX)
    {
        b->pending[b->pending_count].is_char = true;
        b->pending[b->pending_count].cp = cp;
        b->pending_count++;
    }
}

/* ========================================================================== */
/*  DOM code -> KEY_ translation                                              */
/* ========================================================================== */

static int translate_code(const char *code)
{
    if (strncmp(code, "Key", 3) == 0 && code[3])
        return KEY_A + (code[3] - 'A');
    if (strncmp(code, "Digit", 5) == 0 && code[5])
        return KEY_ZERO + (code[5] - '0');
    if (strncmp(code, "Numpad", 6) == 0 && code[6] >= '0' && code[6] <= '9' && !code[7])
        return KEY_KP_0 + (code[6] - '0');
    if (code[0] == 'F' && code[1] >= '1' && code[1] <= '9')
    {
        int n = atoi(code + 1);
        if (n >= 1 && n <= 12)
            return KEY_F1 + (n - 1);
    }

    struct
    {
        const char *name;
        int key;
    } map[] = {
        {"Space", KEY_SPACE},
        {"Enter", KEY_ENTER},
        {"Escape", KEY_ESCAPE},
        {"Tab", KEY_TAB},
        {"Backspace", KEY_BACKSPACE},
        {"Delete", KEY_DELETE},
        {"Insert", KEY_INSERT},
        {"ArrowLeft", KEY_LEFT},
        {"ArrowRight", KEY_RIGHT},
        {"ArrowUp", KEY_UP},
        {"ArrowDown", KEY_DOWN},
        {"Home", KEY_HOME},
        {"End", KEY_END},
        {"PageUp", KEY_PAGE_UP},
        {"PageDown", KEY_PAGE_DOWN},
        {"Minus", KEY_MINUS},
        {"Equal", KEY_EQUAL},
        {"Comma", KEY_COMMA},
        {"Period", KEY_PERIOD},
        {"Slash", KEY_SLASH},
        {"Backslash", KEY_BACKSLASH},
        {"Semicolon", KEY_SEMICOLON},
        {"Quote", KEY_APOSTROPHE},
        {"Backquote", KEY_GRAVE},
        {"BracketLeft", KEY_LEFT_BRACKET},
        {"BracketRight", KEY_RIGHT_BRACKET},
        {"ShiftLeft", KEY_LEFT_SHIFT},
        {"ShiftRight", KEY_RIGHT_SHIFT},
        {"ControlLeft", KEY_LEFT_CONTROL},
        {"ControlRight", KEY_RIGHT_CONTROL},
        {"AltLeft", KEY_LEFT_ALT},
        {"AltRight", KEY_RIGHT_ALT},
        {"MetaLeft", KEY_LEFT_SUPER},
        {"MetaRight", KEY_RIGHT_SUPER},
        {"CapsLock", KEY_CAPS_LOCK},
        {"NumLock", KEY_NUM_LOCK},
        {"ScrollLock", KEY_SCROLL_LOCK},
        {"PrintScreen", KEY_PRINT_SCREEN},
        {"Pause", KEY_PAUSE},
        {"ContextMenu", KEY_MENU},
        {"NumpadDecimal", KEY_KP_DECIMAL},
        {"NumpadDivide", KEY_KP_DIVIDE},
        {"NumpadMultiply", KEY_KP_MULTIPLY},
        {"NumpadSubtract", KEY_KP_SUBTRACT},
        {"NumpadAdd", KEY_KP_ADD},
        {"NumpadEnter", KEY_KP_ENTER},
        {"NumpadEqual", KEY_KP_EQUAL},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (strcmp(code, map[i].name) == 0)
            return map[i].key;
    return KEY_NULL;
}

static int translate_kb_mods(const EmscriptenKeyboardEvent *e)
{
    int mods = 0;
    if (e->shiftKey)
        mods |= KEYMOD_SHIFT;
    if (e->ctrlKey)
        mods |= KEYMOD_CTRL;
    if (e->altKey)
        mods |= KEYMOD_ALT;
    if (e->metaKey)
        mods |= KEYMOD_SUPER;
    return mods;
}

/* Decode a single UTF-8 codepoint; returns 0 if the string is a named key. */
static uint32_t decode_one_codepoint(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    uint32_t cp;
    int len;
    if (p[0] < 0x80)
    {
        cp = p[0];
        len = 1;
    }
    else if ((p[0] >> 5) == 0x6)
    {
        cp = p[0] & 0x1F;
        len = 2;
    }
    else if ((p[0] >> 4) == 0xE)
    {
        cp = p[0] & 0x0F;
        len = 3;
    }
    else if ((p[0] >> 3) == 0x1E)
    {
        cp = p[0] & 0x07;
        len = 4;
    }
    else
        return 0;
    for (int i = 1; i < len; i++)
    {
        if ((p[i] & 0xC0) != 0x80)
            return 0;
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    return p[len] == '\0' ? cp : 0; /* trailing bytes mean a named key like "Tab" */
}

/* ========================================================================== */
/*  html5 event callbacks                                                     */
/* ========================================================================== */

static EM_BOOL on_key(int type, const EmscriptenKeyboardEvent *e, void *user)
{
    BackendWindow *b = user;
    bool down = type == EMSCRIPTEN_EVENT_KEYDOWN;

    Event ev = {.type = EVENT_KEY};
    ev.data.key.key = translate_code(e->code);
    ev.data.key.down = down;
    ev.data.key.repeat = e->repeat;
    ev.data.key.mods = translate_kb_mods(e);
    enqueue_event(b, &ev);

    if (down)
    {
        uint32_t cp = decode_one_codepoint(e->key);
        if (cp >= 0x20 && cp != 0x7F)
            enqueue_char(b, cp);
    }
    return EM_TRUE; /* preventDefault: keep Tab/arrows/Space from scrolling */
}

static EM_BOOL on_mouse(int type, const EmscriptenMouseEvent *e, void *user)
{
    BackendWindow *b = user;
    if (type == EMSCRIPTEN_EVENT_MOUSEMOVE)
    {
        Event ev = {.type = EVENT_MOUSE_MOVE};
        ev.data.mouse.x = (int)e->targetX;
        ev.data.mouse.y = (int)e->targetY;
        enqueue_event(b, &ev);
    }
    else
    {
        int btn = e->button == 0 ? MOUSE_LEFT : e->button == 1 ? MOUSE_MIDDLE
                                            : e->button == 2   ? MOUSE_RIGHT
                                                               : -1;
        if (btn < 0)
            return EM_FALSE;
        Event ev = {.type = EVENT_MOUSE_BUTTON};
        ev.data.mouse.button = btn;
        ev.data.mouse.down = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
        ev.data.mouse.x = (int)e->targetX;
        ev.data.mouse.y = (int)e->targetY;
        enqueue_event(b, &ev);
    }
    return EM_TRUE;
}

static EM_BOOL on_wheel(int type, const EmscriptenWheelEvent *e, void *user)
{
    (void)type;
    BackendWindow *b = user;
    Event ev = {.type = EVENT_MOUSE_WHEEL};
    ev.data.wheel.x = e->deltaX > 0 ? -1.0f : e->deltaX < 0 ? 1.0f
                                                            : 0.0f;
    ev.data.wheel.y = e->deltaY > 0 ? -1.0f : e->deltaY < 0 ? 1.0f
                                                            : 0.0f;
    enqueue_event(b, &ev);
    return EM_TRUE;
}

static EM_BOOL on_focus(int type, const EmscriptenFocusEvent *e, void *user)
{
    (void)e;
    BackendWindow *b = user;
    b->focused = type == EMSCRIPTEN_EVENT_FOCUS;
    Event ev = {.type = EVENT_WINDOW_FOCUS};
    ev.data.focus.gained = b->focused;
    enqueue_event(b, &ev);
    return EM_TRUE;
}

static EM_BOOL on_resize(int type, const EmscriptenUiEvent *e, void *user)
{
    (void)type;
    (void)e;
    BackendWindow *b = user;
    double css_w, css_h;
    emscripten_get_element_css_size(CANVAS_TARGET, &css_w, &css_h);
    double dpr = emscripten_get_device_pixel_ratio();
    b->css_w = (int)css_w;
    b->css_h = (int)css_h;
    emscripten_set_canvas_element_size(CANVAS_TARGET, (int)(css_w * dpr), (int)(css_h * dpr));

    Event r = {.type = EVENT_WINDOW_RESIZE};
    r.data.resize.w = b->css_w;
    r.data.resize.h = b->css_h;
    enqueue_event(b, &r);
    Event fb = {.type = EVENT_WINDOW_FB_RESIZE};
    fb.data.resize.w = (int)(css_w * dpr);
    fb.data.resize.h = (int)(css_h * dpr);
    enqueue_event(b, &fb);
    return EM_TRUE;
}

static EM_BOOL on_touch(int type, const EmscriptenTouchEvent *e, void *user)
{
    BackendWindow *b = user;
    TouchPhase phase = type == EMSCRIPTEN_EVENT_TOUCHSTART ? TOUCH_DOWN
                       : type == EMSCRIPTEN_EVENT_TOUCHMOVE
                           ? TOUCH_MOVE
                       : type == EMSCRIPTEN_EVENT_TOUCHEND ? TOUCH_UP
                                                           : TOUCH_CANCEL;
    for (int i = 0; i < e->numTouches; i++)
    {
        const EmscriptenTouchPoint *t = &e->touches[i];
        if (!t->isChanged)
            continue;
        Event ev = {.type = EVENT_TOUCH};
        ev.data.touch.id = t->identifier;
        ev.data.touch.x = (float)t->targetX;
        ev.data.touch.y = (float)t->targetY;
        ev.data.touch.phase = phase;
        enqueue_event(b, &ev);
    }
    return EM_TRUE;
}

/* ========================================================================== */
/*  init / create                                                             */
/* ========================================================================== */

bool backend_init(void)
{
    return true;
}

void backend_shutdown(void)
{
}

/* WebGL2 is GLES 3.0 and WebGL1 is GLES 2.0; nothing else exists in a browser. */
static bool web_gl_version(const GLConfig *gl, int *webgl_major)
{
    int major = gl->major ? gl->major : 3;
    int minor = gl->major ? gl->minor : 0;
    if (gl->profile != GL_PROFILE_DEFAULT && gl->profile != GL_PROFILE_ES)
        return error_set("only OpenGL ES contexts are available on the web");
    if ((major != 2 && major != 3) || minor > 0)
        return error_set("OpenGL ES %d.%d is not available on the web", major, minor);
    *webgl_major = major == 3 ? 2 : 1;
    return true;
}

BackendWindow *backend_create(const WindowConfig *cfg)
{
    BackendWindow *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->render = cfg->render;
    b->css_w = cfg->width > 0 ? cfg->width : 640;
    b->css_h = cfg->height > 0 ? cfg->height : 480;

    if (cfg->render == RENDER_PIXELS)
    {
        /* A 2D context, sized 1:1 with the canvas, presented via putImageData. */
        emscripten_set_element_css_size(CANVAS_TARGET, b->css_w, b->css_h);
        emscripten_set_canvas_element_size(CANVAS_TARGET, b->css_w, b->css_h);
        EM_ASM({ Module._pf_ctx = Module['canvas'].getContext('2d'); });
    }
    else
    {
        int webgl_major;
        if (!web_gl_version(&cfg->gl, &webgl_major))
        {
            free(b);
            return NULL;
        }

        EmscriptenWebGLContextAttributes attrs;
        emscripten_webgl_init_context_attributes(&attrs);
        attrs.majorVersion = webgl_major;
        attrs.minorVersion = 0;
        attrs.antialias = cfg->gl.msaa > 0;
        attrs.alpha = false;
        attrs.depth = true;
        attrs.stencil = true;

        b->gl = emscripten_webgl_create_context(CANVAS_TARGET, &attrs);
        if (b->gl <= 0)
        {
            error_set("cannot create a WebGL %d context", attrs.majorVersion);
            free(b);
            return NULL;
        }
        emscripten_webgl_make_context_current(b->gl);

        double dpr = emscripten_get_device_pixel_ratio();
        emscripten_set_element_css_size(CANVAS_TARGET, b->css_w, b->css_h);
        emscripten_set_canvas_element_size(CANVAS_TARGET, (int)(b->css_w * dpr), (int)(b->css_h * dpr));
    }
    b->focused = true;
    b->visible = true;
    b->cursor_mode = MOUSE_MODE_NORMAL;

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, b, EM_TRUE, on_key);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, b, EM_TRUE, on_key);
    emscripten_set_mousemove_callback(CANVAS_TARGET, b, EM_TRUE, on_mouse);
    emscripten_set_mousedown_callback(CANVAS_TARGET, b, EM_TRUE, on_mouse);
    emscripten_set_mouseup_callback(CANVAS_TARGET, b, EM_TRUE, on_mouse);
    emscripten_set_wheel_callback(CANVAS_TARGET, b, EM_TRUE, on_wheel);
    emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, b, EM_TRUE, on_focus);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, b, EM_TRUE, on_focus);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, b, EM_TRUE, on_resize);
    emscripten_set_touchstart_callback(CANVAS_TARGET, b, EM_TRUE, on_touch);
    emscripten_set_touchmove_callback(CANVAS_TARGET, b, EM_TRUE, on_touch);
    emscripten_set_touchend_callback(CANVAS_TARGET, b, EM_TRUE, on_touch);
    emscripten_set_touchcancel_callback(CANVAS_TARGET, b, EM_TRUE, on_touch);

    g_active = b;
    return b;
}

void backend_destroy(BackendWindow *b)
{
    if (!b)
        return;
    if (b->gl > 0)
        emscripten_webgl_destroy_context(b->gl);
    free(b->px);
    if (g_active == b)
        g_active = NULL;
    free(b);
}

void backend_pump_events(BackendWindow *b, Core *core)
{
    b->core = core;
    for (int i = 0; i < b->pending_count; i++)
    {
        WebItem *it = &b->pending[i];
        if (it->is_char)
            core_push_char(core, it->cp);
        else
            core_push_event(core, &it->ev);
    }
    b->pending_count = 0;
}

/* ========================================================================== */
/*  native handles and Vulkan: none on the web                                */
/* ========================================================================== */

void *backend_native_handle(BackendWindow *b, NativeHandleType type)
{
    (void)b;
    (void)type;
    return NULL;
}

const char *const *backend_vulkan_extensions(uint32_t *count)
{
    *count = 0;
    return NULL;
}

bool backend_vulkan_create_surface(BackendWindow *b, void *instance, const void *allocator, uint64_t *out_surface)
{
    (void)b;
    (void)instance;
    (void)allocator;
    (void)out_surface;
    return error_set("Vulkan is not available on the web");
}

void backend_swap(BackendWindow *b)
{
    (void)b; /* the browser presents the canvas after the frame callback returns */
}

/* ========================================================================== */
/*  loop                                                                      */
/* ========================================================================== */

typedef struct
{
    PlatformWindow *w;
    FrameCallback frame;
    void *user;
} LoopCtx;

static LoopCtx g_loop;

static void main_tick(void)
{
    window_begin_frame(g_loop.w);
    g_loop.frame(g_loop.w, g_loop.user);
    window_swap(g_loop.w);
    if (window_should_close(g_loop.w))
        emscripten_cancel_main_loop();
}

void backend_run(BackendWindow *b, PlatformWindow *w, FrameCallback frame, void *user)
{
    (void)b;
    g_loop.w = w;
    g_loop.frame = frame;
    g_loop.user = user;
    /* fps 0 = requestAnimationFrame; simulate_infinite_loop 1 = never returns */
    emscripten_set_main_loop(main_tick, 0, 1);
}

/* ========================================================================== */
/*  GL context                                                                */
/* ========================================================================== */

void backend_make_current(BackendWindow *b)
{
    if (b->render == RENDER_GL)
        emscripten_webgl_make_context_current(b->gl);
}

void backend_make_current_on(BackendWindow *target, BackendWindow *context)
{
    if (target == context)
        backend_make_current(context);
}

void backend_set_vsync(BackendWindow *b, bool on)
{
    (void)b;
    (void)on; /* presentation is tied to requestAnimationFrame; always vsynced */
}

void *backend_gl_proc_address(const char *name)
{
    return emscripten_webgl_get_proc_address(name);
}

bool backend_lock_pixels(BackendWindow *b, Framebuffer *out)
{
    if (b->render != RENDER_PIXELS || !out)
        return false;
    if (!b->px || b->px_w != b->css_w || b->px_h != b->css_h)
    {
        free(b->px);
        b->px_w = b->css_w;
        b->px_h = b->css_h;
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
    if (b->render != RENDER_PIXELS || !b->px)
        return;
    /* Swizzle 0xAARRGGBB -> RGBA bytes and blit through the 2D context. */
    EM_ASM(
        {
            var ctx = Module._pf_ctx;
            if (!ctx)
                return;
            var w = $1;
            var h = $2;
            var img = ctx.createImageData(w, h);
            var d = img.data;
            var src = $0 >> 2;
            for (var i = 0; i < w * h; i++)
            {
                var p = HEAPU32[src + i];
                d[i * 4 + 0] = (p >> 16) & 0xff;
                d[i * 4 + 1] = (p >> 8) & 0xff;
                d[i * 4 + 2] = p & 0xff;
                d[i * 4 + 3] = (p >> 24) & 0xff;
            }
            ctx.putImageData(img, 0, 0);
        },
        b->px, b->px_w, b->px_h);
}

/* ========================================================================== */
/*  geometry                                                                  */
/* ========================================================================== */

void backend_get_size(BackendWindow *b, int *w, int *h)
{
    if (w)
        *w = b->css_w;
    if (h)
        *h = b->css_h;
}

void backend_set_size(BackendWindow *b, int w, int h)
{
    b->css_w = w;
    b->css_h = h;
    double dpr = emscripten_get_device_pixel_ratio();
    emscripten_set_element_css_size(CANVAS_TARGET, w, h);
    emscripten_set_canvas_element_size(CANVAS_TARGET, (int)(w * dpr), (int)(h * dpr));
}

void backend_get_fb_size(BackendWindow *b, int *w, int *h)
{
    (void)b;
    emscripten_get_canvas_element_size(CANVAS_TARGET, w, h);
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
    (void)y; /* a canvas has no window position */
}

float backend_content_scale(BackendWindow *b)
{
    (void)b;
    return (float)emscripten_get_device_pixel_ratio();
}

void backend_set_title(BackendWindow *b, const char *title)
{
    (void)b;
    if (title)
        emscripten_set_window_title(title);
}

void backend_set_size_limits(BackendWindow *b, int minw, int minh, int maxw, int maxh)
{
    (void)b;
    (void)minw;
    (void)minh;
    (void)maxw;
    (void)maxh;
}

/* ========================================================================== */
/*  window state (mostly inert on a canvas)                                   */
/* ========================================================================== */

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
    b->visible = true;
}
void backend_hide(BackendWindow *b)
{
    b->visible = false;
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
    if (mode == WINDOW_WINDOWED)
        emscripten_exit_fullscreen();
    else
        emscripten_request_fullscreen(CANVAS_TARGET, EM_TRUE);
}

WindowMode backend_get_mode(BackendWindow *b)
{
    EmscriptenFullscreenChangeEvent fs;
    if (emscripten_get_fullscreen_status(&fs) == EMSCRIPTEN_RESULT_SUCCESS && fs.isFullscreen)
        return WINDOW_FULLSCREEN;
    (void)b;
    return WINDOW_WINDOWED;
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

/* ========================================================================== */
/*  mouse                                                                     */
/* ========================================================================== */

void backend_set_mouse_pos(BackendWindow *b, int x, int y)
{
    (void)b;
    (void)x;
    (void)y; /* the browser does not allow warping the pointer */
}

void backend_set_cursor(BackendWindow *b, int cursor)
{
    (void)b;
    const char *css;
    switch (cursor)
    {
    case CURSOR_IBEAM:
        css = "text";
        break;
    case CURSOR_CROSSHAIR:
        css = "crosshair";
        break;
    case CURSOR_HAND:
        css = "pointer";
        break;
    case CURSOR_RESIZE_EW:
        css = "ew-resize";
        break;
    case CURSOR_RESIZE_NS:
        css = "ns-resize";
        break;
    case CURSOR_RESIZE_NWSE:
        css = "nwse-resize";
        break;
    case CURSOR_RESIZE_NESW:
        css = "nesw-resize";
        break;
    case CURSOR_RESIZE_ALL:
        css = "move";
        break;
    case CURSOR_NOT_ALLOWED:
        css = "not-allowed";
        break;
    default:
        css = "default";
        break;
    }
    EM_ASM({ if (Module['canvas']) Module['canvas'].style.cursor = UTF8ToString($0); }, css);
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
    b->cursor_mode = mode;
    if (mode == MOUSE_MODE_CAPTURED)
        emscripten_request_pointerlock(CANVAS_TARGET, EM_TRUE);
    else
        emscripten_exit_pointerlock();
    EM_ASM({ if (Module['canvas']) Module['canvas'].style.cursor = ($0 == 0) ? 'default' : 'none'; }, mode == MOUSE_MODE_NORMAL ? 0 : 1);
}

/* ========================================================================== */
/*  clipboard                                                                 */
/* ========================================================================== */

/* The async, permission-gated browser clipboard cannot be read synchronously
   from wasm, so the content round-trips within the page. Text writes are mirrored
   out to the system clipboard best-effort. */
bool backend_clipboard_set(const ClipboardItem *items, int count)
{
    if (!clipmem_set(items, count))
        return false;
    for (int i = 0; i < count; i++)
        if (clip_mime_equal(items[i].mime, CLIPBOARD_TEXT))
        {
            const ClipMemItem *t = clipmem_find(CLIPBOARD_TEXT); /* NUL-terminated copy */
            if (t)
                EM_ASM({ if (navigator.clipboard) navigator.clipboard.writeText(UTF8ToString($0)); }, t->data);
        }
    return true;
}
bool backend_clipboard_has(const char *mime)
{
    return clipmem_has(mime);
}
void *backend_clipboard_get(const char *mime, size_t *size)
{
    return clipmem_get(mime, size);
}

/* ========================================================================== */
/*  monitors (one screen)                                                     */
/* ========================================================================== */

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
    int sw = 0, sh = 0;
    emscripten_get_screen_size(&sw, &sh);
    memset(out, 0, sizeof *out);
    out->index = 0;
    out->name = "screen";
    out->width = sw;
    out->height = sh;
    out->work_w = sw;
    out->work_h = sh;
    out->refresh_hz = 60;
    out->content_scale = (float)emscripten_get_device_pixel_ratio();
    out->primary = true;
    return true;
}
