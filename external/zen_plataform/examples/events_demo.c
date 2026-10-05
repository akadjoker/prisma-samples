/*
 * events_demo.c - every event the platform raises, live.
 *
 * The right panel is the event stream as poll_event delivers it (mouse moves and touch
 * moves are merged into one line). The left canvas draws what the polled state says:
 * the pointer and its trail, touch points, and the gesture being recognised. The
 * "State" window shows the polled values.
 *
 * Try: type, move and click, scroll, resize or move the window, change focus, drop a
 * file on it, press F11 for fullscreen. No touchscreen? Tick "emulate touch with the
 * mouse": drag for a hold or a swipe, click twice for a double tap.
 * Platform log messages go to the stream too (Log level: debug shows them all).
 */
#include "platform.h"
#include "zen_ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOG_LINES 28
#define TRAIL 96

typedef struct
{
    char text[110];
    int kind; /* index into the colour table */
} Line;

enum { K_WINDOW, K_KEY, K_CHAR, K_MOUSE, K_TOUCH, K_GESTURE, K_LOG, K_COUNT };

typedef struct
{
    UiContext *ui;
    UiRect log_win, state_win;

    Line lines[LOG_LINES];
    int count;
    bool paused;
    bool debug_log;
    bool emulate_touch;

    int trail_x[TRAIL], trail_y[TRAIL], trail_n, trail_head;
    unsigned last_gesture;
    int wheel_total;
} Demo;

static const uint32_t g_kind_color[K_COUNT] = {
    0xFF8AB4F8, /* window  */
    0xFFA5D6A7, /* key     */
    0xFFFFE082, /* char    */
    0xFFEF9A9A, /* mouse   */
    0xFFCE93D8, /* touch   */
    0xFFFFAB91, /* gesture */
    0xFFB0BEC5, /* log     */
};

#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_LIKE(f, a) __attribute__((format(printf, f, a)))
#else
#define PRINTF_LIKE(f, a)
#endif
static void push_line(Demo *d, int kind, const char *fmt, ...) PRINTF_LIKE(3, 4);

static void add_line(Demo *d, int kind, const char *text, bool merge)
{
    if (d->paused)
        return;
    if (merge && d->count > 0 && d->lines[d->count - 1].kind == kind)
    {
        snprintf(d->lines[d->count - 1].text, sizeof d->lines[0].text, "%s", text);
        return;
    }
    if (d->count == LOG_LINES)
    {
        memmove(&d->lines[0], &d->lines[1], sizeof(Line) * (LOG_LINES - 1));
        d->count--;
    }
    Line *l = &d->lines[d->count++];
    l->kind = kind;
    snprintf(l->text, sizeof l->text, "%s", text);
}

static void push_line(Demo *d, int kind, const char *fmt, ...)
{
    char buf[110];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    add_line(d, kind, buf, false);
}

/* ---- names ---- */

static const char *key_name(int key, char *buf, size_t cap)
{
    if (key >= KEY_F1 && key <= KEY_F12)
        snprintf(buf, cap, "F%d", key - KEY_F1 + 1);
    else if (key >= KEY_SPACE && key <= KEY_GRAVE)
        snprintf(buf, cap, key == KEY_SPACE ? "SPACE" : "%c", key);
    else
        switch (key)
        {
        case KEY_ESCAPE: return "ESCAPE";
        case KEY_ENTER: return "ENTER";
        case KEY_TAB: return "TAB";
        case KEY_BACKSPACE: return "BACKSPACE";
        case KEY_INSERT: return "INSERT";
        case KEY_DELETE: return "DELETE";
        case KEY_RIGHT: return "RIGHT";
        case KEY_LEFT: return "LEFT";
        case KEY_DOWN: return "DOWN";
        case KEY_UP: return "UP";
        case KEY_PAGE_UP: return "PAGE_UP";
        case KEY_PAGE_DOWN: return "PAGE_DOWN";
        case KEY_HOME: return "HOME";
        case KEY_END: return "END";
        case KEY_LEFT_SHIFT: return "LEFT_SHIFT";
        case KEY_RIGHT_SHIFT: return "RIGHT_SHIFT";
        case KEY_LEFT_CONTROL: return "LEFT_CTRL";
        case KEY_RIGHT_CONTROL: return "RIGHT_CTRL";
        case KEY_LEFT_ALT: return "LEFT_ALT";
        case KEY_RIGHT_ALT: return "RIGHT_ALT";
        case KEY_LEFT_SUPER: return "LEFT_SUPER";
        case KEY_RIGHT_SUPER: return "RIGHT_SUPER";
        case KEY_CAPS_LOCK: return "CAPS_LOCK";
        default: snprintf(buf, cap, "key %d", key);
        }
    return buf;
}

static const char *mods_name(int mods, char *buf, size_t cap)
{
    snprintf(buf, cap, "%s%s%s%s", mods & KEYMOD_SHIFT ? "Shift+" : "", mods & KEYMOD_CTRL ? "Ctrl+" : "",
             mods & KEYMOD_ALT ? "Alt+" : "", mods & KEYMOD_SUPER ? "Super+" : "");
    return buf;
}

static const char *button_name(int b)
{
    return b == MOUSE_LEFT ? "left" : b == MOUSE_RIGHT ? "right" : b == MOUSE_MIDDLE ? "middle" : "other";
}

static const char *phase_name(TouchPhase p)
{
    return p == TOUCH_DOWN ? "down" : p == TOUCH_MOVE ? "move" : p == TOUCH_UP ? "up" : "cancel";
}

static const char *gesture_name(unsigned g)
{
    switch (g)
    {
    case GESTURE_TAP: return "TAP";
    case GESTURE_DOUBLETAP: return "DOUBLETAP";
    case GESTURE_HOLD: return "HOLD";
    case GESTURE_DRAG: return "DRAG";
    case GESTURE_SWIPE_RIGHT: return "SWIPE_RIGHT";
    case GESTURE_SWIPE_LEFT: return "SWIPE_LEFT";
    case GESTURE_SWIPE_UP: return "SWIPE_UP";
    case GESTURE_SWIPE_DOWN: return "SWIPE_DOWN";
    case GESTURE_PINCH_IN: return "PINCH_IN";
    case GESTURE_PINCH_OUT: return "PINCH_OUT";
    default: return "none";
    }
}

static void utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80)
        *out++ = (char)cp;
    else if (cp < 0x800)
    {
        *out++ = (char)(0xC0 | (cp >> 6));
        *out++ = (char)(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        *out++ = (char)(0xE0 | (cp >> 12));
        *out++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *out++ = (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        *out++ = (char)(0xF0 | (cp >> 18));
        *out++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *out++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *out++ = (char)(0x80 | (cp & 0x3F));
    }
    *out = '\0';
}

/* ---- event stream ---- */

static void log_event(Demo *d, const Event *e)
{
    char buf[110], kn[24], mn[24], ch[8];
    switch (e->type)
    {
    case EVENT_WINDOW_CLOSE:
        push_line(d, K_WINDOW, "WINDOW_CLOSE");
        break;
    case EVENT_WINDOW_RESIZE:
        push_line(d, K_WINDOW, "WINDOW_RESIZE %d x %d", e->data.resize.w, e->data.resize.h);
        break;
    case EVENT_WINDOW_FB_RESIZE:
        push_line(d, K_WINDOW, "WINDOW_FB_RESIZE %d x %d px", e->data.resize.w, e->data.resize.h);
        break;
    case EVENT_WINDOW_MOVE:
        push_line(d, K_WINDOW, "WINDOW_MOVE %d, %d", e->data.move.x, e->data.move.y);
        break;
    case EVENT_WINDOW_FOCUS:
        push_line(d, K_WINDOW, "WINDOW_FOCUS %s", e->data.focus.gained ? "gained" : "lost");
        break;
    case EVENT_WINDOW_MINIMIZE:
        push_line(d, K_WINDOW, "WINDOW_MINIMIZE");
        break;
    case EVENT_WINDOW_MAXIMIZE:
        push_line(d, K_WINDOW, "WINDOW_MAXIMIZE");
        break;
    case EVENT_WINDOW_RESTORE:
        push_line(d, K_WINDOW, "WINDOW_RESTORE");
        break;
    case EVENT_WINDOW_ENTER:
        push_line(d, K_WINDOW, "WINDOW_ENTER pointer %s", e->data.enter.entered ? "entered" : "left");
        break;
    case EVENT_WINDOW_SCALE_CHANGED:
        push_line(d, K_WINDOW, "WINDOW_SCALE_CHANGED %.2f", (double)e->data.scale.scale);
        break;
    case EVENT_WINDOW_DROP:
        for (int i = 0; i < e->data.drop.count && i < 3; i++)
            push_line(d, K_WINDOW, "WINDOW_DROP %s", e->data.drop.paths[i]);
        if (e->data.drop.count > 3)
            push_line(d, K_WINDOW, "WINDOW_DROP ... and %d more", e->data.drop.count - 3);
        break;
    case EVENT_WINDOW_SURFACE_LOST:
        push_line(d, K_WINDOW, "WINDOW_SURFACE_LOST");
        break;
    case EVENT_WINDOW_SURFACE_READY:
        push_line(d, K_WINDOW, "WINDOW_SURFACE_READY");
        break;
    case EVENT_GAMEPAD_CONNECTED:
        push_line(d, K_WINDOW, "GAMEPAD_CONNECTED %d  %s", e->data.device.index, gamepad_name(e->data.device.index));
        break;
    case EVENT_GAMEPAD_DISCONNECTED:
        push_line(d, K_WINDOW, "GAMEPAD_DISCONNECTED %d", e->data.device.index);
        break;
    case EVENT_JOYSTICK_CONNECTED:
    {
        int j = e->data.device.index;
        push_line(d, K_WINDOW, "JOYSTICK_CONNECTED %d  %s  (%d axes, %d buttons, %d hats)", j, joystick_name(j),
                  joystick_axis_count(j), joystick_button_count(j), joystick_hat_count(j));
        break;
    }
    case EVENT_JOYSTICK_DISCONNECTED:
        push_line(d, K_WINDOW, "JOYSTICK_DISCONNECTED %d", e->data.device.index);
        break;
    case EVENT_KEY:
        push_line(d, K_KEY, "KEY %s %s%s  %s scancode %d", key_name(e->data.key.key, kn, sizeof kn),
                  e->data.key.down ? "down" : "up", e->data.key.repeat ? " (repeat)" : "",
                  mods_name(e->data.key.mods, mn, sizeof mn), e->data.key.scancode);
        break;
    case EVENT_CHAR:
        utf8_encode(e->data.codepoint, ch);
        push_line(d, K_CHAR, "CHAR U+%04X '%s'", e->data.codepoint, e->data.codepoint < 32 ? "?" : ch);
        break;
    case EVENT_MOUSE_MOVE:
        snprintf(buf, sizeof buf, "MOUSE_MOVE %d, %d   delta %d, %d", e->data.mouse.x, e->data.mouse.y,
                 e->data.mouse.dx, e->data.mouse.dy);
        add_line(d, K_MOUSE, buf, d->count > 0 && strncmp(d->lines[d->count - 1].text, "MOUSE_MOVE", 10) == 0);
        break;
    case EVENT_MOUSE_BUTTON:
        push_line(d, K_MOUSE, "MOUSE_BUTTON %s %s at %d, %d", button_name(e->data.mouse.button),
                  e->data.mouse.down ? "down" : "up", e->data.mouse.x, e->data.mouse.y);
        break;
    case EVENT_MOUSE_WHEEL:
        d->wheel_total += (int)e->data.wheel.y;
        push_line(d, K_MOUSE, "MOUSE_WHEEL %.1f, %.1f", (double)e->data.wheel.x, (double)e->data.wheel.y);
        break;
    case EVENT_TOUCH:
    {
        const char *who = e->data.touch.id == TOUCH_ID_MOUSE ? "mouse" : "finger";
        snprintf(buf, sizeof buf, "TOUCH %s id %d %s  %.0f, %.0f", who, e->data.touch.id,
                 phase_name(e->data.touch.phase), (double)e->data.touch.x, (double)e->data.touch.y);
        add_line(d, K_TOUCH, buf,
                 e->data.touch.phase == TOUCH_MOVE && d->count > 0 && strstr(d->lines[d->count - 1].text, " move ") != NULL);
        break;
    }
    default:
        break;
    }
}

static void log_sink(LogLevel level, const char *msg, void *user)
{
    Demo *d = user;
    static const char *names[] = {"debug", "info", "warn", "error"};
    push_line(d, K_LOG, "LOG %s: %s", names[level], msg);
}

/* ---- canvas ---- */

static void draw_arrow(Framebuffer *fb, int x0, int y0, float dx, float dy, uint32_t color)
{
    int x1 = x0 + (int)dx, y1 = y0 + (int)dy;
    draw_line(fb, x0, y0, x1, y1, color, BLEND_NONE);
    draw_fill_circle(fb, x1, y1, 4, color, BLEND_NONE);
}

static void draw_canvas(Demo *d, PlatformWindow *w, Framebuffer *fb, UiRect area)
{
    draw_set_clip(area.x, area.y, area.w, area.h);
    draw_fill_rect(fb, area.x, area.y, area.w, area.h, 0xFF181818, BLEND_NONE);
    for (int x = area.x; x < area.x + area.w; x += 50)
        draw_line(fb, x, area.y, x, area.y + area.h, 0xFF222222, BLEND_NONE);
    for (int y = area.y; y < area.y + area.h; y += 50)
        draw_line(fb, area.x, y, area.x + area.w, y, 0xFF222222, BLEND_NONE);

    /* pointer trail */
    for (int i = 1; i < d->trail_n; i++)
    {
        int a = (d->trail_head + TRAIL - d->trail_n + i - 1) % TRAIL;
        int b = (d->trail_head + TRAIL - d->trail_n + i) % TRAIL;
        uint32_t shade = 40 + (uint32_t)(i * 180 / d->trail_n);
        draw_line(fb, d->trail_x[a], d->trail_y[a], d->trail_x[b], d->trail_y[b],
                  0xFF000000 | (shade << 16) | (shade / 2 << 8), BLEND_NONE);
    }

    /* pointer: a crosshair, filled while a button is down */
    int mx = mouse_x(w), my = mouse_y(w);
    bool down = mouse_button_down(w, MOUSE_LEFT) || mouse_button_down(w, MOUSE_RIGHT) || mouse_button_down(w, MOUSE_MIDDLE);
    draw_line(fb, mx - 14, my, mx + 14, my, 0xFFEF9A9A, BLEND_NONE);
    draw_line(fb, mx, my - 14, mx, my + 14, 0xFFEF9A9A, BLEND_NONE);
    if (down)
        draw_fill_circle(fb, mx, my, 8, 0xFFEF5350, BLEND_NONE);
    else
        draw_circle(fb, mx, my, 8, 0xFFEF9A9A, BLEND_NONE);

    /* touch points, one colour per slot */
    static const uint32_t slot[] = {0xFFCE93D8, 0xFF80DEEA, 0xFFFFF59D, 0xFFA5D6A7, 0xFFFFAB91};
    for (int i = 0; i < touch_count(w); i++)
    {
        float x, y;
        touch_position(w, i, &x, &y);
        draw_circle(fb, (int)x, (int)y, 28, slot[i % 5], BLEND_NONE);
        draw_fill_circle(fb, (int)x, (int)y, 6, slot[i % 5], BLEND_NONE);
    }

    /* the gesture's own vectors */
    unsigned g = gesture_detected(w);
    float vx, vy;
    if (g == GESTURE_DRAG || g == GESTURE_HOLD)
    {
        gesture_drag_vector(w, &vx, &vy);
        if (touch_count(w) > 0)
        {
            float sx, sy;
            touch_position(w, 0, &sx, &sy);
            draw_arrow(fb, (int)(sx - vx), (int)(sy - vy), vx, vy, 0xFFFFAB91);
        }
    }
    if ((g == GESTURE_PINCH_IN || g == GESTURE_PINCH_OUT) && touch_count(w) >= 2)
    {
        float ax, ay, bx, by;
        touch_position(w, 0, &ax, &ay);
        touch_position(w, 1, &bx, &by);
        draw_line(fb, (int)ax, (int)ay, (int)bx, (int)by, 0xFFFFAB91, BLEND_NONE);
    }
    draw_reset_clip();
    draw_rect(fb, area.x, area.y, area.w, area.h, 0xFF505050, BLEND_NONE);
}

/* ---- frame ---- */

static void frame(PlatformWindow *w, void *user)
{
    Demo *d = (Demo *)user;

    Event ev;
    while (poll_event(w, &ev))
        log_event(d, &ev);

    /* gestures are polled: report a change of the detected gesture */
    unsigned g = gesture_detected(w);
    if (g != d->last_gesture && g != GESTURE_NONE)
    {
        float vx, vy;
        gesture_drag_vector(w, &vx, &vy);
        if (g >= GESTURE_SWIPE_RIGHT && g <= GESTURE_SWIPE_DOWN)
            push_line(d, K_GESTURE, "GESTURE %s  angle %.0f", gesture_name(g), (double)gesture_drag_angle(w));
        else if (g == GESTURE_PINCH_IN || g == GESTURE_PINCH_OUT)
            push_line(d, K_GESTURE, "GESTURE %s  angle %.0f", gesture_name(g), (double)gesture_pinch_angle(w));
        else
            push_line(d, K_GESTURE, "GESTURE %s", gesture_name(g));
    }
    d->last_gesture = g;

    if (key_pressed(w, KEY_F11))
        window_set_mode(w, window_get_mode(w) == WINDOW_WINDOWED ? WINDOW_FULLSCREEN_BORDERLESS : WINDOW_WINDOWED, MONITOR_CURRENT);

    int mx = mouse_x(w), my = mouse_y(w);
    if (d->trail_n == 0 || d->trail_x[(d->trail_head + TRAIL - 1) % TRAIL] != mx || d->trail_y[(d->trail_head + TRAIL - 1) % TRAIL] != my)
    {
        d->trail_x[d->trail_head] = mx;
        d->trail_y[d->trail_head] = my;
        d->trail_head = (d->trail_head + 1) % TRAIL;
        if (d->trail_n < TRAIL)
            d->trail_n++;
    }

    Framebuffer fb;
    if (!window_lock_pixels(w, &fb))
        return;
    draw_clear(&fb, 0xFF121212);

    int ww, wh;
    window_get_size(w, &ww, &wh);
    UiRect area = zui_rect(12, 12, ww - 24 - 480, wh - 24);
    if (area.w > 8 && area.h > 8)
        draw_canvas(d, w, &fb, area);

    zui_begin(d->ui, w);

    if (zui_window_begin(d->ui, "Event stream", &d->log_win))
    {
        zui_row(d->ui, 20, 1);
        for (int i = 0; i < d->count; i++)
        {
            zui_push_color(d->ui, UI_COL_TEXT, g_kind_color[d->lines[i].kind]);
            zui_label(d->ui, d->lines[i].text);
            zui_pop_color(d->ui, 1);
        }
        zui_window_end(d->ui);
    }

    if (zui_window_begin(d->ui, "State", &d->state_win))
    {
        char line[160], keys[96] = "", kn[24], mn[24];
        int sw, sh, fw, fh, wx, wy, dx, dy;
        window_get_size(w, &sw, &sh);
        window_get_framebuffer_size(w, &fw, &fh);
        window_get_position(w, &wx, &wy);
        mouse_delta(w, &dx, &dy);

        zui_row(d->ui, 20, 1);
        snprintf(line, sizeof line, "window %d x %d at %d, %d   framebuffer %d x %d   scale %.2f", sw, sh, wx, wy, fw, fh,
                 (double)window_content_scale(w));
        zui_label(d->ui, line);
        snprintf(line, sizeof line, "focused %d  hovered %d  minimized %d  maximized %d  visible %d", window_is_focused(w),
                 window_is_hovered(w), window_is_minimized(w), window_is_maximized(w), window_is_visible(w));
        zui_label(d->ui, line);
        snprintf(line, sizeof line, "mouse %d, %d  delta %d, %d  wheel total %d  mods %s", mouse_x(w), mouse_y(w), dx, dy,
                 d->wheel_total, mods_name(key_mods(w), mn, sizeof mn));
        zui_label(d->ui, line);
        for (int k = 1; k < KEY_MAX; k++)
            if (key_down(w, k) && strlen(keys) + 14 < sizeof keys)
            {
                strcat(keys, key_name(k, kn, sizeof kn));
                strcat(keys, " ");
            }
        snprintf(line, sizeof line, "keys down: %s", keys);
        zui_label(d->ui, line);
        snprintf(line, sizeof line, "touches %d   gesture %s   hold %.2fs", touch_count(w), gesture_name(gesture_detected(w)),
                 (double)gesture_hold_duration(w));
        zui_label(d->ui, line);
        if (gamepad_connected(0))
        {
            snprintf(line, sizeof line, "gamepad 0: %s  left stick %.2f, %.2f", gamepad_name(0),
                     (double)gamepad_axis(0, GAMEPAD_AXIS_LEFT_X), (double)gamepad_axis(0, GAMEPAD_AXIS_LEFT_Y));
            zui_label(d->ui, line);
        }

        zui_row(d->ui, 24, 1);
        if (zui_checkbox(d->ui, "Emulate touch with the mouse", &d->emulate_touch))
            touch_set_mouse_emulation(w, d->emulate_touch);
        if (zui_checkbox(d->ui, "Log level: debug", &d->debug_log))
            log_set_level(d->debug_log ? LOGLEVEL_DEBUG : LOGLEVEL_INFO);
        zui_checkbox(d->ui, "Pause the stream", &d->paused);
        zui_row(d->ui, 26, 3);
        if (zui_button(d->ui, "Clear"))
            d->count = 0;
        if (zui_button(d->ui, "Log a warning"))
            log_warn("this came from log_warn in the demo");
        if (zui_button(d->ui, "Provoke an error"))
        {
            clipboard_get_data("image/x-never-offered", NULL); /* debug level shows the reason */
            log_error("platform_get_error() says: %s", platform_get_error()[0] ? platform_get_error() : "(nothing)");
        }
        zui_window_end(d->ui);
    }

    zui_end(d->ui);
    zui_render_software(&fb, zui_draw_list(d->ui));
    window_present_pixels(w);
}

int main(void)
{
    if (!platform_init())
        return 1;

    WindowConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "zen_platform events";
    cfg.width = 1280;
    cfg.height = 760;
    cfg.x = WINDOW_POS_CENTERED;
    cfg.y = WINDOW_POS_CENTERED;
    cfg.render = RENDER_PIXELS;
    cfg.resizable = true;

    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        fprintf(stderr, "window_create: %s\n", platform_get_error());
        platform_shutdown();
        return 1;
    }

    Demo d;
    memset(&d, 0, sizeof d);
    d.ui = zui_create();
    UiStyle s;
    zui_style_dark(&s);
    zui_set_style(d.ui, &s);
    d.log_win = zui_rect(cfg.width - 490, 12, 478, 640);
    d.state_win = zui_rect(24, cfg.height - 350, 700, 330);
    log_set_callback(log_sink, &d);
    push_line(&d, K_LOG, "events demo started: type, move, click, scroll, resize, drop a file");

    app_run(w, frame, &d);

    log_set_callback(NULL, NULL);
    zui_destroy(d.ui);
    window_destroy(w);
    platform_shutdown();
    return 0;
}
