/*
 * backend_fake.c - PHASE 1 backend. No window, no GL, no OS. It buffers injected
 * events and replays them into the core on pump, and simulates geometry/state so
 * the whole public API is exercisable from a headless test.
 */
#include "core_internal.h"
#include "backend.h"
#include "error_internal.h"
#include "backend_fake.h"
#include "clipboard_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_PENDING_MAX 256

typedef struct
{
    bool is_char;
    bool real_touch; /* goes through core_push_touch, like a desktop touch screen */
    Event ev;
    uint32_t cp;
} FakeItem;

struct BackendWindow
{
    FakeItem pending[FAKE_PENDING_MAX];
    int pending_count;

    int x, y, w, h, fb_w, fb_h;
    float content_scale;
    WindowMode mode;
    int cursor, mouse_mode;
    bool captured;
    bool text_input;
    int ime_rect[4];
    bool decorated;
    HitTestFunc hit_cb;
    PlatformWindow *hit_w;
    void *hit_user;
    PlatformCursor *cursor_image;
    bool vsync;
    bool flag[5];   /* indexed by WIN_FLAG_* */
    int swap_count; /* lets a test confirm the begin_frame -> frame -> swap cycle */

    RenderMode render;
    uint32_t *px;
    int px_w, px_h;
    int present_count;
};

/* ---- helpers shared with the injection API below ---- */

static void enqueue(BackendWindow *b, const FakeItem *it)
{
    if (b->pending_count < FAKE_PENDING_MAX)
        b->pending[b->pending_count++] = *it;
}

/* ========================================================================== */
/*  backend.h implementation                                                  */
/* ========================================================================== */

bool backend_init(void)
{
    return true;
}

void backend_shutdown(void)
{
}

BackendWindow *backend_create(const WindowConfig *cfg)
{
    BackendWindow *b = calloc(1, sizeof *b);
    if (!b)
        return NULL;
    b->x = (cfg->x == WINDOW_POS_CENTERED || cfg->x == WINDOW_POS_UNDEFINED) ? 0 : cfg->x;
    b->y = (cfg->y == WINDOW_POS_CENTERED || cfg->y == WINDOW_POS_UNDEFINED) ? 0 : cfg->y;
    b->w = cfg->width;
    b->h = cfg->height;
    b->content_scale = 1.0f;
    b->text_input = true;
    b->decorated = !cfg->undecorated && cfg->kind != WINDOW_KIND_POPUP && cfg->kind != WINDOW_KIND_TOOLTIP;
    b->fb_w = cfg->width;
    b->fb_h = cfg->height;
    b->mode = cfg->mode;
    b->render = cfg->render;
    b->flag[WIN_FLAG_FOCUSED] = true;
    b->flag[WIN_FLAG_VISIBLE] = true;
    return b;
}

void backend_destroy(BackendWindow *b)
{
    free(b->px);
    free(b);
}

void backend_pump_events(BackendWindow *b, Core *core)
{
    for (int i = 0; i < b->pending_count; i++)
    {
        FakeItem *it = &b->pending[i];
        if (it->is_char)
            core_push_char(core, it->cp);
        else if (it->real_touch)
            core_push_touch(core, it->ev.data.touch.phase, it->ev.data.touch.id, it->ev.data.touch.x,
                            it->ev.data.touch.y, it->ev.data.touch.pressure);
        else
            core_push_event(core, &it->ev);
    }
    b->pending_count = 0;
}

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
    return error_set("Vulkan is not available in the fake backend");
}

void backend_swap(BackendWindow *b)
{
    b->swap_count++;
}

void backend_get_size(BackendWindow *b, int *w, int *h)
{
    if (w)
        *w = b->w;
    if (h)
        *h = b->h;
}
void backend_set_size(BackendWindow *b, int w, int h)
{
    b->w = w;
    b->h = h;
}
void backend_get_fb_size(BackendWindow *b, int *w, int *h)
{
    if (w)
        *w = b->fb_w;
    if (h)
        *h = b->fb_h;
}
void backend_get_pos(BackendWindow *b, int *x, int *y)
{
    if (x)
        *x = b->x;
    if (y)
        *y = b->y;
}
void backend_set_pos(BackendWindow *b, int x, int y)
{
    b->x = x;
    b->y = y;
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
float backend_content_scale(BackendWindow *b)
{
    return b->content_scale;
}

void backend_minimize(BackendWindow *b)
{
    b->flag[WIN_FLAG_MINIMIZED] = true;
}
void backend_maximize(BackendWindow *b)
{
    b->flag[WIN_FLAG_MAXIMIZED] = true;
}
void backend_restore(BackendWindow *b)
{
    b->flag[WIN_FLAG_MINIMIZED] = b->flag[WIN_FLAG_MAXIMIZED] = false;
}
void backend_show(BackendWindow *b)
{
    b->flag[WIN_FLAG_VISIBLE] = true;
}
void backend_hide(BackendWindow *b)
{
    b->flag[WIN_FLAG_VISIBLE] = false;
}
void backend_focus(BackendWindow *b)
{
    b->flag[WIN_FLAG_FOCUSED] = true;
}
void backend_request_attention(BackendWindow *b)
{
    (void)b;
}
bool backend_get_flag(BackendWindow *b, int flag)
{
    return (flag >= 0 && flag < 5) ? b->flag[flag] : false;
}
void backend_set_mode(BackendWindow *b, WindowMode mode, int monitor)
{
    (void)monitor;
    b->mode = mode;
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

void backend_make_current(BackendWindow *b)
{
    (void)b;
}
void backend_make_current_on(BackendWindow *target, BackendWindow *context)
{
    (void)target;
    (void)context;
}
void backend_set_vsync(BackendWindow *b, bool on)
{
    b->vsync = on;
}
void *backend_gl_proc_address(const char *name)
{
    (void)name;
    return NULL;
}

bool backend_lock_pixels(BackendWindow *b, Framebuffer *out)
{
    if (b->render != RENDER_PIXELS || !out)
        return false;
    if (!b->px || b->px_w != b->w || b->px_h != b->h)
    {
        free(b->px);
        b->px_w = b->w;
        b->px_h = b->h;
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
    b->present_count++;
}

void backend_set_mouse_pos(BackendWindow *b, int x, int y)
{
    (void)b;
    (void)x;
    (void)y;
}
void backend_set_cursor(BackendWindow *b, int cursor)
{
    b->cursor = cursor;
}
void backend_set_live_callback(BackendWindow *b, PlatformWindow *w, FrameCallback cb, void *user)
{
    (void)b, (void)w, (void)cb, (void)user;
}

static int g_fake_locks;
void fake_set_lock_state(int mask)
{
    g_fake_locks = mask;
}
int backend_lock_state(void)
{
    return g_fake_locks;
}

bool backend_mouse_capture(BackendWindow *b, bool on)
{
    b->captured = on;
    return on;
}

struct PlatformCursor
{
    int w, h, hot_x, hot_y;
    uint32_t first_pixel;
};

void backend_set_decorated(BackendWindow *b, bool on)
{
    b->decorated = on;
}

void backend_set_hit_test(BackendWindow *b, PlatformWindow *w, HitTestFunc fn, void *user)
{
    b->hit_cb = fn;
    b->hit_w = w;
    b->hit_user = user;
}

HitTestResult fake_hit_test(PlatformWindow *w, int x, int y)
{
    BackendWindow *b = w->b;
    return b->hit_cb ? b->hit_cb(b->hit_w, x, y, b->hit_user) : HIT_NORMAL;
}

bool fake_is_decorated(PlatformWindow *w)
{
    return w->b->decorated;
}

PlatformCursor *backend_cursor_create(const uint32_t *argb, int w, int h, int hot_x, int hot_y)
{
    PlatformCursor *c = malloc(sizeof *c);
    if (c)
        *c = (PlatformCursor){w, h, hot_x, hot_y, argb[0]};
    return c;
}

void backend_cursor_destroy(PlatformCursor *c)
{
    free(c);
}

void backend_set_cursor_image(BackendWindow *b, PlatformCursor *c)
{
    b->cursor_image = c;
}

PlatformCursor *fake_cursor_image(PlatformWindow *w)
{
    return w->b->cursor_image;
}

void backend_set_text_input(BackendWindow *b, bool on)
{
    b->text_input = on;
}

void backend_set_text_input_rect(BackendWindow *b, int x, int y, int w, int h)
{
    b->ime_rect[0] = x, b->ime_rect[1] = y, b->ime_rect[2] = w, b->ime_rect[3] = h;
}

bool fake_text_input_on(PlatformWindow *w)
{
    return w->b->text_input;
}

void fake_text_input_rect(PlatformWindow *w, int out[4])
{
    memcpy(out, w->b->ime_rect, sizeof w->b->ime_rect);
}

void fake_text_edit(PlatformWindow *w, const char *utf8, int cursor)
{
    Event e = {.type = EVENT_TEXT_EDIT};
    snprintf(e.data.edit.text, sizeof e.data.edit.text, "%s", utf8);
    e.data.edit.cursor = cursor;
    fake_inject_event(w, &e);
}

void backend_set_mouse_mode(BackendWindow *b, int mode)
{
    b->mouse_mode = mode;
}

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
    *out = (MonitorInfo){
        .index = 0,
        .name = "fake-0",
        .x = 0,
        .y = 0,
        .width = 1920,
        .height = 1080,
        .work_x = 0,
        .work_y = 0,
        .work_w = 1920,
        .work_h = 1040,
        .phys_width_mm = 510,
        .phys_height_mm = 290,
        .refresh_hz = 60,
        .content_scale = 1.0f,
        .primary = true,
    };
    return true;
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

/* ========================================================================== */
/*  Injection API (backend_fake.h) - test only                                */
/* ========================================================================== */

void fake_inject_event(PlatformWindow *w, const Event *ev)
{
    FakeItem it = {.is_char = false, .ev = *ev};
    enqueue(w->b, &it);
}

void fake_inject_char(PlatformWindow *w, uint32_t codepoint)
{
    FakeItem it = {.is_char = true, .cp = codepoint};
    enqueue(w->b, &it);
}

void fake_key(PlatformWindow *w, int key, bool down, bool repeat)
{
    Event e = {.type = EVENT_KEY};
    e.data.key.key = key;
    e.data.key.down = down;
    e.data.key.repeat = repeat;
    fake_inject_event(w, &e);
}

void fake_mouse_move(PlatformWindow *w, int x, int y)
{
    Event e = {.type = EVENT_MOUSE_MOVE};
    e.data.mouse.x = x;
    e.data.mouse.y = y;
    fake_inject_event(w, &e);
}

void fake_mouse_button(PlatformWindow *w, int button, bool down)
{
    Event e = {.type = EVENT_MOUSE_BUTTON};
    e.data.mouse.button = button;
    e.data.mouse.down = down;
    fake_inject_event(w, &e);
}

void fake_wheel(PlatformWindow *w, float x, float y)
{
    Event e = {.type = EVENT_MOUSE_WHEEL};
    e.data.wheel.x = x;
    e.data.wheel.y = y;
    fake_inject_event(w, &e);
}

void fake_real_touch(PlatformWindow *w, int id, float x, float y, TouchPhase phase)
{
    Event e = {.type = EVENT_TOUCH};
    e.data.touch.id = id;
    e.data.touch.x = x;
    e.data.touch.y = y;
    e.data.touch.pressure = phase == TOUCH_UP ? 0.0f : 1.0f;
    e.data.touch.phase = phase;
    FakeItem it = {.real_touch = true, .ev = e};
    enqueue(w->b, &it);
}

void fake_touch(PlatformWindow *w, int id, float x, float y, TouchPhase phase)
{
    Event e = {.type = EVENT_TOUCH};
    e.data.touch.id = id;
    e.data.touch.x = x;
    e.data.touch.y = y;
    e.data.touch.phase = phase;
    fake_inject_event(w, &e);
}

void fake_resize(PlatformWindow *w, int width, int height)
{
    Event e = {.type = EVENT_WINDOW_RESIZE};
    e.data.resize.w = width;
    e.data.resize.h = height;
    fake_inject_event(w, &e);
}

void fake_fb_resize(PlatformWindow *w, int width, int height)
{
    Event e = {.type = EVENT_WINDOW_FB_RESIZE};
    e.data.resize.w = width;
    e.data.resize.h = height;
    fake_inject_event(w, &e);
}

int fake_swap_count(PlatformWindow *w)
{
    return w->b->swap_count;
}
