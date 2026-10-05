/*
 * core.c - portable core. Owns the input state, the per-frame event list, the
 * key and char queues, and the touch slots. Knows nothing about the OS: every
 * platform detail goes through backend.h.
 */
#include "core_internal.h"
#include "error_internal.h"
#include "gamepad_internal.h"
#include "gesture_internal.h"
#include "backend.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/* ---- ring buffers ---- */

static void keycode_push(InputState *s, int key)
{
    int next = (s->kq_tail + 1) % KEYCODE_QUEUE_LEN;
    if (next == s->kq_head)
        return; /* full: drop the newest, keep order */
    s->keycode_q[s->kq_tail] = key;
    s->kq_tail = next;
}

static int keycode_pop(InputState *s)
{
    if (s->kq_head == s->kq_tail)
        return 0;
    int key = s->keycode_q[s->kq_head];
    s->kq_head = (s->kq_head + 1) % KEYCODE_QUEUE_LEN;
    return key;
}

static void char_push(InputState *s, uint32_t cp)
{
    int next = (s->cq_tail + 1) % CHAR_QUEUE_LEN;
    if (next == s->cq_head)
        return;
    s->char_q[s->cq_tail] = cp;
    s->cq_tail = next;
}

static uint32_t char_pop(InputState *s)
{
    if (s->cq_head == s->cq_tail)
        return 0;
    uint32_t cp = s->char_q[s->cq_head];
    s->cq_head = (s->cq_head + 1) % CHAR_QUEUE_LEN;
    return cp;
}

/* ---- touch slots, kept dense and matched by id ---- */

static int touch_find(InputState *s, int id)
{
    for (int i = 0; i < s->touch_count; i++)
        if (s->touch[i].id == id)
            return i;
    return -1;
}

static void touch_apply(InputState *s, int id, float x, float y, float pressure, TouchPhase phase)
{
    int i = touch_find(s, id);
    switch (phase)
    {
    case TOUCH_DOWN:
        if (i < 0 && s->touch_count < MAX_TOUCH_POINTS)
            i = s->touch_count++;
        if (i < 0)
            return;
        s->touch[i].id = id;
        s->touch[i].x = x;
        s->touch[i].y = y;
        s->touch[i].pressure = pressure;
        break;
    case TOUCH_MOVE:
        if (i < 0)
            return;
        s->touch[i].x = x;
        s->touch[i].y = y;
        s->touch[i].pressure = pressure;
        break;
    case TOUCH_UP:
    case TOUCH_CANCEL:
        if (i < 0)
            return;
        for (int j = i; j < s->touch_count - 1; j++)
            s->touch[j] = s->touch[j + 1];
        s->touch_count--;
        break;
    }
}


/* ---- gestures: fed from touch (and optionally the left mouse button) ---- */

static void gesture_feed_touch(Core *core, TouchPhase phase, int id, float x, float y)
{
    InputState *s = &core->in;
    GVec2 pos[MAX_TOUCH_POINTS];
    int n = 0;
    bool known = false;
    for (int i = 0; i < s->touch_count; i++)
    {
        bool me = s->touch[i].id == id;
        known |= me;
        pos[n++] = me ? (GVec2){x, y} : (GVec2){s->touch[i].x, s->touch[i].y};
    }
    /* A new finger is not in the slots yet; a lifted one still is. */
    if (!known && phase != TOUCH_UP && phase != TOUCH_CANCEL && n < MAX_TOUCH_POINTS)
        pos[n++] = (GVec2){x, y};

    GestureAction a = phase == TOUCH_DOWN   ? GESTURE_ACTION_DOWN
                      : phase == TOUCH_MOVE ? GESTURE_ACTION_MOVE
                      : phase == TOUCH_UP   ? GESTURE_ACTION_UP
                                            : GESTURE_ACTION_CANCEL;
    /* gesture size comes from the window through core_set_gesture_size */
    gesture_feed(&s->gesture, a, n, pos, s->gesture.width, s->gesture.height, time_seconds());
}


/* ========================================================================== */
/*  Backend -> core                                                           */
/* ========================================================================== */

void core_push_char(Core *core, uint32_t codepoint)
{
    if (core->in.text_input_off)
        return;
    /* Text is both an event, for poll_event, and an entry in the char queue; the
       EVENT_CHAR case of core_push_event does the queue. */
    Event e = {.type = EVENT_CHAR};
    e.data.codepoint = codepoint;
    core_push_event(core, &e);
}


/* Desktop has no touch device: with emulation on, the left button becomes one
   finger (TOUCH_ID_MOUSE) and goes through the same path as a real touch. */
static void mouse_touch_emit(Core *core, TouchPhase phase)
{
    InputState *s = &core->in;
    Event t = {.type = EVENT_TOUCH};
    t.data.touch.id = TOUCH_ID_MOUSE;
    t.data.touch.x = (float)s->mouse_x;
    t.data.touch.y = (float)s->mouse_y;
    t.data.touch.pressure = phase == TOUCH_UP ? 0.0f : 1.0f;
    t.data.touch.phase = phase;
    core_push_event(core, &t);
}

void core_push_event(Core *core, const Event *ev)
{
    InputState *s = &core->in;

    /* While a finger is down the system's mouse events are its echo: drop them. */
    if (s->real_touch > 0 && !s->synth && (ev->type == EVENT_MOUSE_MOVE || ev->type == EVENT_MOUSE_BUTTON))
        return;

    if (core->hook)
        core->hook(core->owner, ev, core->hook_user);

    if (s->fe_count < FRAME_EVENT_MAX)
        s->frame_events[s->fe_count++] = *ev;

    switch (ev->type)
    {
    case EVENT_KEY:
    {
        int k = ev->data.key.key;
        if (k > 0 && k < KEY_MAX)
        {
            s->key_down[k] = ev->data.key.down;
            if (ev->data.key.down && !ev->data.key.repeat)
                keycode_push(s, k);
        }
        break;
    }
    case EVENT_CHAR:
        char_push(s, ev->data.codepoint);
        break;
    case EVENT_TEXT_EDIT:
        snprintf(s->composition, sizeof s->composition, "%s", ev->data.edit.text);
        break;
    case EVENT_MOUSE_MOVE:
        s->mouse_x = ev->data.mouse.x;
        s->mouse_y = ev->data.mouse.y;
        if (s->mouse_touch && s->mouse_touch_down)
            mouse_touch_emit(core, TOUCH_MOVE);
        break;
    case EVENT_MOUSE_BUTTON:
    {
        int b = ev->data.mouse.button;
        if (b >= 0 && b < MOUSE_BUTTON_MAX)
            s->mouse_down[b] = ev->data.mouse.down;
        if (s->mouse_touch && b == MOUSE_LEFT)
        {
            if (ev->data.mouse.down && !s->mouse_touch_down && s->touch_count == 0)
            {
                s->mouse_touch_down = true;
                mouse_touch_emit(core, TOUCH_DOWN);
            }
            else if (!ev->data.mouse.down && s->mouse_touch_down)
            {
                s->mouse_touch_down = false;
                mouse_touch_emit(core, TOUCH_UP);
            }
        }
        break;
    }
    case EVENT_MOUSE_WHEEL:
        s->wheel_x += ev->data.wheel.x;
        s->wheel_y += ev->data.wheel.y;
        break;
    case EVENT_TOUCH:
        gesture_feed_touch(core, ev->data.touch.phase, ev->data.touch.id, ev->data.touch.x,
                           ev->data.touch.y);
        touch_apply(s, ev->data.touch.id, ev->data.touch.x, ev->data.touch.y,
                    ev->data.touch.pressure, ev->data.touch.phase);
        break;
    case EVENT_WINDOW_RESIZE:
        if (ev->data.resize.w > 0 && ev->data.resize.h > 0)
        {
            s->gesture.width = (float)ev->data.resize.w;
            s->gesture.height = (float)ev->data.resize.h;
        }
        break;
    case EVENT_WINDOW_FOCUS:
        if (!ev->data.focus.gained)
        {
            memset(s->key_down, 0, sizeof s->key_down);
            memset(s->mouse_down, 0, sizeof s->mouse_down);
        }
        break;
    case EVENT_WINDOW_CLOSE:
        s->close_request = true;
        break;
    default:
        break;
    }
}


void core_push_touch(Core *core, TouchPhase phase, int id, float x, float y, float pressure)
{
    InputState *s = &core->in;
    Event t = {.type = EVENT_TOUCH};
    t.data.touch.id = id;
    t.data.touch.x = x;
    t.data.touch.y = y;
    t.data.touch.pressure = pressure;
    t.data.touch.phase = phase;
    core_push_event(core, &t);

    bool down = phase == TOUCH_DOWN;
    bool up = phase == TOUCH_UP || phase == TOUCH_CANCEL;
    if (down)
        s->real_touch++;
    else if (up && s->real_touch > 0)
        s->real_touch--;

    /* the first finger is the mouse */
    s->synth = true;
    if (down && !s->primary_active)
    {
        s->primary_active = true;
        s->primary_touch = id;
        Event m = {.type = EVENT_MOUSE_MOVE};
        m.data.mouse.x = (int)x;
        m.data.mouse.y = (int)y;
        core_push_event(core, &m);
        Event b = {.type = EVENT_MOUSE_BUTTON};
        b.data.mouse.button = MOUSE_LEFT;
        b.data.mouse.down = true;
        b.data.mouse.x = (int)x;
        b.data.mouse.y = (int)y;
        core_push_event(core, &b);
    }
    else if (phase == TOUCH_MOVE && s->primary_active && s->primary_touch == id)
    {
        Event m = {.type = EVENT_MOUSE_MOVE};
        m.data.mouse.x = (int)x;
        m.data.mouse.y = (int)y;
        core_push_event(core, &m);
    }
    else if (up && s->primary_active && s->primary_touch == id)
    {
        s->primary_active = false;
        Event b = {.type = EVENT_MOUSE_BUTTON};
        b.data.mouse.button = MOUSE_LEFT;
        b.data.mouse.down = false;
        b.data.mouse.x = (int)x;
        b.data.mouse.y = (int)y;
        core_push_event(core, &b);
    }
    s->synth = false;
}

/* ========================================================================== */
/*  Lifecycle                                                                 */
/* ========================================================================== */

static uint64_t g_time_base; /* nanoseconds at platform_init */

static uint64_t now_nanos(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    uint64_t whole = (uint64_t)counter.QuadPart / (uint64_t)freq.QuadPart;
    uint64_t rest = (uint64_t)counter.QuadPart % (uint64_t)freq.QuadPart;
    return whole * 1000000000ull + rest * 1000000000ull / (uint64_t)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

bool platform_init(void)
{
    g_time_base = now_nanos();
    if (!backend_init())
        return false;
    gamepad_init();
    return true;
}

void platform_shutdown(void)
{
    gamepad_shutdown();
    backend_shutdown();
}

PlatformWindow *window_create(const WindowConfig *cfg)
{
    if (!cfg)
        return NULL;
    const GLConfig *gl = &cfg->gl;
    if (gl->profile < GL_PROFILE_DEFAULT || gl->profile > GL_PROFILE_ES || gl->major < 0 ||
        gl->minor < 0 || gl->msaa < 0)
    {
        error_set("invalid OpenGL configuration");
        return NULL;
    }
    PlatformWindow *w = calloc(1, sizeof *w);
    if (!w)
        return NULL;
    w->cfg = *cfg;
    if (w->cfg.monitor == MONITOR_MOUSE)
    {
        int under = monitor_from_mouse();
        w->cfg.monitor = under >= 0 ? under : MONITOR_CURRENT;
    }
    w->core.owner = w;
    gesture_state_init(&w->core.in.gesture);
    if (cfg->width > 0 && cfg->height > 0)
    {
        w->core.in.gesture.width = (float)cfg->width;
        w->core.in.gesture.height = (float)cfg->height;
    }
    w->b = backend_create(&w->cfg);
    if (!w->b)
    {
        free(w);
        return NULL;
    }
    return w;
}

void window_destroy(PlatformWindow *w)
{
    if (!w)
        return;
    backend_destroy(w->b);
    free(w);
}

bool window_should_close(PlatformWindow *w)
{
    return w->should_close;
}
void window_set_should_close(PlatformWindow *w, bool v)
{
    w->should_close = v;
}

void window_set_user_ptr(PlatformWindow *w, void *ptr)
{
    w->user = ptr;
}
void *window_get_user_ptr(PlatformWindow *w)
{
    return w->user;
}

/* Tell this window about devices that came or went since it last looked. A slot
   whose generation changed is a different device: the old one went, a new one came. */
static void device_events(Core *core)
{
    InputState *s = &core->in;
    for (int i = 0; i < GAMEPAD_MAX; i++)
    {
        unsigned now = gamepad_connected(i) ? gamepad_internal_generation(i) : 0;
        if (s->pad_seen[i] && s->pad_seen[i] != now)
        {
            Event e = {.type = EVENT_GAMEPAD_DISCONNECTED};
            e.data.device.index = i;
            core_push_event(core, &e);
        }
        if (now && s->pad_seen[i] != now)
        {
            Event e = {.type = EVENT_GAMEPAD_CONNECTED};
            e.data.device.index = i;
            core_push_event(core, &e);
        }
        s->pad_seen[i] = now;
    }
    for (int i = 0; i < JOYSTICK_MAX; i++)
    {
        unsigned now = joystick_connected(i) ? joystick_internal_generation(i) : 0;
        if (s->joy_seen[i] && s->joy_seen[i] != now)
        {
            Event e = {.type = EVENT_JOYSTICK_DISCONNECTED};
            e.data.device.index = i;
            core_push_event(core, &e);
        }
        if (now && s->joy_seen[i] != now)
        {
            Event e = {.type = EVENT_JOYSTICK_CONNECTED};
            e.data.device.index = i;
            core_push_event(core, &e);
        }
        s->joy_seen[i] = now;
    }
}

void window_begin_frame(PlatformWindow *w)
{
    InputState *s = &w->core.in;
    memcpy(s->key_prev, s->key_down, sizeof s->key_down);
    memcpy(s->mouse_prev, s->mouse_down, sizeof s->mouse_down);
    s->mouse_px = s->mouse_x;
    s->mouse_py = s->mouse_y;
    s->wheel_x = s->wheel_y = 0;
    s->fe_count = 0;
    s->fe_cursor = 0;
    /* keycode_q and char_q are not cleared here: the consumer drains them */

    gamepad_poll();
    device_events(&w->core);
    /* Before the new events: last frame's TAP turns into HOLD and a SWIPE ends, so
       a gesture born in this frame's events is still visible after this call. */
    gesture_update(&s->gesture, time_seconds());
    backend_pump_events(w->b, &w->core);

    if (s->close_request)
        window_set_should_close(w, true);
    if (s->exit_key && key_pressed(w, s->exit_key))
        window_set_should_close(w, true);
}

void window_swap(PlatformWindow *w)
{
    backend_swap(w->b);
}

void app_run(PlatformWindow *w, FrameCallback frame, void *user)
{
    backend_run(w->b, w, frame, user);
}

bool poll_event(PlatformWindow *w, Event *out)
{
    InputState *s = &w->core.in;
    if (s->fe_cursor >= s->fe_count)
        return false;
    *out = s->frame_events[s->fe_cursor++];
    return true;
}

/* ========================================================================== */
/*  OpenGL context                                                            */
/* ========================================================================== */

void window_make_current(PlatformWindow *w)
{
    backend_make_current(w->b);
}
void window_make_current_on(PlatformWindow *target, PlatformWindow *context)
{
    backend_make_current_on(target->b, context->b);
}
void window_set_event_hook(PlatformWindow *w, EventHook hook, void *user)
{
    w->core.hook = hook;
    w->core.hook_user = user;
}

void window_text_input_start(PlatformWindow *w)
{
    w->core.in.text_input_off = false;
    backend_set_text_input(w->b, true);
}

void window_text_input_stop(PlatformWindow *w)
{
    w->core.in.text_input_off = true;
    w->core.in.composition[0] = '\0';
    backend_set_text_input(w->b, false);
}

bool window_text_input_active(PlatformWindow *w)
{
    return !w->core.in.text_input_off;
}

void window_set_text_input_rect(PlatformWindow *w, int x, int y, int width, int height)
{
    backend_set_text_input_rect(w->b, x, y, width, height);
}

const char *window_text_composition(PlatformWindow *w)
{
    return w->core.in.composition;
}

void window_set_decorated(PlatformWindow *w, bool on)
{
    backend_set_decorated(w->b, on);
}

void window_set_hit_test(PlatformWindow *w, HitTestFunc fn, void *user)
{
    backend_set_hit_test(w->b, w, fn, user);
}

PlatformCursor *cursor_create(const uint32_t *argb, int width, int height, int hot_x, int hot_y)
{
    if (!argb || width < 1 || height < 1 || width > 256 || height > 256 || hot_x < 0 || hot_y < 0 ||
        hot_x >= width || hot_y >= height)
    {
        error_set("invalid cursor image");
        return NULL;
    }
    return backend_cursor_create(argb, width, height, hot_x, hot_y);
}

void cursor_destroy(PlatformCursor *cursor)
{
    if (cursor)
        backend_cursor_destroy(cursor);
}

void mouse_set_cursor_image(PlatformWindow *w, PlatformCursor *cursor)
{
    backend_set_cursor_image(w->b, cursor);
}

void window_set_live_callback(PlatformWindow *w, FrameCallback cb, void *user)
{
    backend_set_live_callback(w->b, w, cb, user);
}

bool mouse_capture(PlatformWindow *w, bool on)
{
    return backend_mouse_capture(w->b, on);
}

void *window_native_handle(PlatformWindow *w, NativeHandleType type)
{
    return w ? backend_native_handle(w->b, type) : NULL;
}

void window_set_vsync(PlatformWindow *w, bool on)
{
    backend_set_vsync(w->b, on);
}
void *gl_proc_address(const char *name)
{
    return backend_gl_proc_address(name);
}

bool window_lock_pixels(PlatformWindow *w, Framebuffer *out)
{
    return backend_lock_pixels(w->b, out);
}
void window_present_pixels(PlatformWindow *w)
{
    backend_present_pixels(w->b);
}

/* ========================================================================== */
/*  Geometry                                                                  */
/* ========================================================================== */

void window_get_size(PlatformWindow *w, int *width, int *height)
{
    backend_get_size(w->b, width, height);
}
void window_set_size(PlatformWindow *w, int width, int height)
{
    backend_set_size(w->b, width, height);
}
void window_get_framebuffer_size(PlatformWindow *w, int *width, int *height)
{
    backend_get_fb_size(w->b, width, height);
}
void window_get_position(PlatformWindow *w, int *x, int *y)
{
    backend_get_pos(w->b, x, y);
}
void window_set_position(PlatformWindow *w, int x, int y)
{
    backend_set_pos(w->b, x, y);
}
void window_set_title(PlatformWindow *w, const char *title)
{
    backend_set_title(w->b, title);
}
void window_set_size_limits(PlatformWindow *w, int minw, int minh, int maxw, int maxh)
{
    backend_set_size_limits(w->b, minw, minh, maxw, maxh);
}
float window_content_scale(PlatformWindow *w)
{
    return backend_content_scale(w->b);
}

void window_center_on_monitor(PlatformWindow *w, int monitor)
{
    MonitorInfo m;
    if (!monitor_get_info(monitor, &m))
        return;
    int ww, wh;
    window_get_size(w, &ww, &wh);
    window_set_position(w, m.x + (m.width - ww) / 2, m.y + (m.height - wh) / 2);
}

void window_set_monitor(PlatformWindow *w, int monitor)
{
    MonitorInfo m;
    if (!monitor_get_info(monitor, &m))
        return;
    window_set_position(w, m.x, m.y);
}

/* ========================================================================== */
/*  PlatformWindow state                                                              */
/* ========================================================================== */

void window_minimize(PlatformWindow *w)
{
    backend_minimize(w->b);
}
void window_maximize(PlatformWindow *w)
{
    backend_maximize(w->b);
}
void window_restore(PlatformWindow *w)
{
    backend_restore(w->b);
}
void window_show(PlatformWindow *w)
{
    backend_show(w->b);
}
void window_hide(PlatformWindow *w)
{
    backend_hide(w->b);
}
void window_focus(PlatformWindow *w)
{
    backend_focus(w->b);
}
void window_request_attention(PlatformWindow *w)
{
    backend_request_attention(w->b);
}

bool window_is_focused(PlatformWindow *w)
{
    return backend_get_flag(w->b, WIN_FLAG_FOCUSED);
}
bool window_is_minimized(PlatformWindow *w)
{
    return backend_get_flag(w->b, WIN_FLAG_MINIMIZED);
}
bool window_is_maximized(PlatformWindow *w)
{
    return backend_get_flag(w->b, WIN_FLAG_MAXIMIZED);
}
bool window_is_visible(PlatformWindow *w)
{
    return backend_get_flag(w->b, WIN_FLAG_VISIBLE);
}
bool window_is_hovered(PlatformWindow *w)
{
    return backend_get_flag(w->b, WIN_FLAG_HOVERED);
}

void window_set_mode(PlatformWindow *w, WindowMode mode, int monitor)
{
    backend_set_mode(w->b, mode, monitor);
}
WindowMode window_get_mode(PlatformWindow *w)
{
    return backend_get_mode(w->b);
}

void window_set_icon(PlatformWindow *w, int width, int height, const uint8_t *rgba)
{
    backend_set_icon(w->b, width, height, rgba);
}
void window_set_opacity(PlatformWindow *w, float alpha)
{
    backend_set_opacity(w->b, alpha);
}
void window_set_always_on_top(PlatformWindow *w, bool on)
{
    backend_set_always_on_top(w->b, on);
}

/* ========================================================================== */
/*  Monitors                                                                  */
/* ========================================================================== */

int monitor_count(void)
{
    return backend_monitor_count();
}
bool monitor_get_info(int index, MonitorInfo *out)
{
    return backend_monitor_info(index, out);
}

int monitor_from_point(int x, int y)
{
    int n = backend_monitor_count();
    for (int i = 0; i < n; i++)
    {
        MonitorInfo m;
        if (!backend_monitor_info(i, &m))
            continue;
        if (x >= m.x && x < m.x + m.width && y >= m.y && y < m.y + m.height)
            return i;
    }
    return -1;
}

bool mouse_global_position(int *x, int *y)
{
    int px = 0, py = 0;
    bool known = backend_mouse_global_position(&px, &py);
    if (x)
        *x = known ? px : 0;
    if (y)
        *y = known ? py : 0;
    return known;
}

int monitor_from_mouse(void)
{
    int x, y;
    if (!mouse_global_position(&x, &y))
        return -1;
    return monitor_from_point(x, y);
}

int monitor_from_window(PlatformWindow *w)
{
    int x, y, ww, wh;
    window_get_position(w, &x, &y);
    window_get_size(w, &ww, &wh);
    int m = monitor_from_point(x + ww / 2, y + wh / 2);
    return m < 0 ? 0 : m;
}

/* ========================================================================== */
/*  Keyboard                                                                  */
/* ========================================================================== */

int key_mods(PlatformWindow *w)
{
    const bool *k = w->core.in.key_down;
    int mods = 0;
    if (k[KEY_LEFT_SHIFT] || k[KEY_RIGHT_SHIFT])
        mods |= KEYMOD_SHIFT;
    if (k[KEY_LEFT_CONTROL] || k[KEY_RIGHT_CONTROL])
        mods |= KEYMOD_CTRL;
    if (k[KEY_LEFT_ALT] || k[KEY_RIGHT_ALT])
        mods |= KEYMOD_ALT;
    if (k[KEY_LEFT_SUPER] || k[KEY_RIGHT_SUPER])
        mods |= KEYMOD_SUPER;
    return mods | backend_lock_state();
}

bool key_down(PlatformWindow *w, int key)
{
    return key > 0 && key < KEY_MAX && w->core.in.key_down[key];
}
bool key_up(PlatformWindow *w, int key)
{
    return !key_down(w, key);
}
bool key_pressed(PlatformWindow *w, int key)
{
    return key > 0 && key < KEY_MAX && w->core.in.key_down[key] && !w->core.in.key_prev[key];
}
bool key_released(PlatformWindow *w, int key)
{
    return key > 0 && key < KEY_MAX && !w->core.in.key_down[key] && w->core.in.key_prev[key];
}

int key_get_pressed(PlatformWindow *w)
{
    return keycode_pop(&w->core.in);
}
uint32_t char_get_pressed(PlatformWindow *w)
{
    return char_pop(&w->core.in);
}

void key_set_exit(PlatformWindow *w, int key)
{
    w->core.in.exit_key = key;
}

/* ========================================================================== */
/*  Mouse                                                                     */
/* ========================================================================== */

bool mouse_button_down(PlatformWindow *w, int button)
{
    return button >= 0 && button < MOUSE_BUTTON_MAX && w->core.in.mouse_down[button];
}
bool mouse_button_up(PlatformWindow *w, int button)
{
    return !mouse_button_down(w, button);
}
bool mouse_button_pressed(PlatformWindow *w, int button)
{
    return button >= 0 && button < MOUSE_BUTTON_MAX &&
           w->core.in.mouse_down[button] && !w->core.in.mouse_prev[button];
}
bool mouse_button_released(PlatformWindow *w, int button)
{
    return button >= 0 && button < MOUSE_BUTTON_MAX &&
           !w->core.in.mouse_down[button] && w->core.in.mouse_prev[button];
}

int mouse_x(PlatformWindow *w)
{
    return w->core.in.mouse_x;
}
int mouse_y(PlatformWindow *w)
{
    return w->core.in.mouse_y;
}
void mouse_position(PlatformWindow *w, int *x, int *y)
{
    if (x)
        *x = w->core.in.mouse_x;
    if (y)
        *y = w->core.in.mouse_y;
}
void mouse_delta(PlatformWindow *w, int *dx, int *dy)
{
    if (dx)
        *dx = w->core.in.mouse_x - w->core.in.mouse_px;
    if (dy)
        *dy = w->core.in.mouse_y - w->core.in.mouse_py;
}
float mouse_wheel(PlatformWindow *w)
{
    return w->core.in.wheel_y;
}
void mouse_wheel_v(PlatformWindow *w, float *x, float *y)
{
    if (x)
        *x = w->core.in.wheel_x;
    if (y)
        *y = w->core.in.wheel_y;
}

void mouse_set_position(PlatformWindow *w, int x, int y)
{
    w->core.in.mouse_x = x;
    w->core.in.mouse_y = y;
    backend_set_mouse_pos(w->b, x, y);
}
void mouse_set_cursor(PlatformWindow *w, int cursor)
{
    backend_set_cursor(w->b, cursor);
}
void mouse_set_mode(PlatformWindow *w, int mode)
{
    backend_set_mouse_mode(w->b, mode);
}

/* ========================================================================== */
/*  Touch                                                                     */
/* ========================================================================== */

int touch_count(PlatformWindow *w)
{
    return w->core.in.touch_count;
}
int touch_x(PlatformWindow *w, int index)
{
    return (index >= 0 && index < w->core.in.touch_count) ? (int)w->core.in.touch[index].x : 0;
}
int touch_y(PlatformWindow *w, int index)
{
    return (index >= 0 && index < w->core.in.touch_count) ? (int)w->core.in.touch[index].y : 0;
}
void touch_position(PlatformWindow *w, int index, float *x, float *y)
{
    if (index < 0 || index >= w->core.in.touch_count)
    {
        if (x)
            *x = 0;
        if (y)
            *y = 0;
        return;
    }
    if (x)
        *x = w->core.in.touch[index].x;
    if (y)
        *y = w->core.in.touch[index].y;
}
/* ---- gestures ---- */

void gesture_set_enabled(PlatformWindow *w, unsigned flags)
{
    w->core.in.gesture.enabled = flags;
}
unsigned gesture_detected(PlatformWindow *w)
{
    return gesture_state_detected(&w->core.in.gesture);
}
bool gesture_is_detected(PlatformWindow *w, unsigned gesture)
{
    return gesture != GESTURE_NONE && gesture_detected(w) == gesture;
}
float gesture_hold_duration(PlatformWindow *w)
{
    return gesture_state_hold_duration(&w->core.in.gesture, time_seconds());
}
void gesture_drag_vector(PlatformWindow *w, float *x, float *y)
{
    if (x)
        *x = w->core.in.gesture.drag_vector.x;
    if (y)
        *y = w->core.in.gesture.drag_vector.y;
}
float gesture_drag_angle(PlatformWindow *w)
{
    return w->core.in.gesture.drag_angle;
}
void gesture_pinch_vector(PlatformWindow *w, float *x, float *y)
{
    if (x)
        *x = w->core.in.gesture.pinch_vector.x;
    if (y)
        *y = w->core.in.gesture.pinch_vector.y;
}
float gesture_pinch_angle(PlatformWindow *w)
{
    return w->core.in.gesture.pinch_angle;
}
void touch_set_mouse_emulation(PlatformWindow *w, bool on)
{
    InputState *s = &w->core.in;
    if (!on && s->mouse_touch_down)
    {
        s->mouse_touch_down = false;
        mouse_touch_emit(&w->core, TOUCH_UP);
    }
    s->mouse_touch = on;
}

int touch_id(PlatformWindow *w, int index)
{
    return (index >= 0 && index < w->core.in.touch_count) ? w->core.in.touch[index].id : -1;
}

/* ========================================================================== */
/*  Time                                                                      */
/* ========================================================================== */

uint64_t time_nanos(void)
{
    return now_nanos() - g_time_base;
}
double time_seconds(void)
{
    return (double)time_nanos() / 1e9;
}

void time_sleep(uint32_t milliseconds)
{
#if defined(_WIN32)
    /* A high-resolution timer of its own per call: one shared timer would be reset
       by a second thread sleeping at the same time, and the first would wait for ever. */
    HANDLE timer = CreateWaitableTimerExW(NULL, NULL, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    if (timer)
    {
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)milliseconds * 10000;
        bool waited = SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE) &&
                      WaitForSingleObject(timer, INFINITE) == WAIT_OBJECT_0;
        CloseHandle(timer);
        if (waited)
            return;
    }
    Sleep(milliseconds);
#else
    struct timespec ts = {(time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
    {
    }
#endif
}

