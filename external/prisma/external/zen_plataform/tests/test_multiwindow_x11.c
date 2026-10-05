/*
 * test_multiwindow_x11.c - two windows on one X connection. Events are injected with
 * XSendEvent straight at a window (found through window_native_handle), so nothing
 * touches the real keyboard or mouse. The point: an event for window B that window
 * A's pump meets first must still reach B, in B's own frame. Skips (77) without a
 * display.
 */
#include "platform.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
            g_pass++;                                              \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static Display *g_dpy;

static Window xwin(PlatformWindow *w)
{
    return (Window)(uintptr_t)window_native_handle(w, NATIVE_WINDOW);
}

static void send_key(Window win, KeySym sym, bool down)
{
    XKeyEvent k;
    memset(&k, 0, sizeof k);
    k.type = down ? KeyPress : KeyRelease;
    k.display = g_dpy;
    k.window = win;
    k.root = DefaultRootWindow(g_dpy);
    k.time = CurrentTime;
    k.x = k.y = 1;
    k.same_screen = True;
    k.keycode = XKeysymToKeycode(g_dpy, sym);
    XSendEvent(g_dpy, win, True, down ? KeyPressMask : KeyReleaseMask, (XEvent *)&k);
}

static void send_motion(Window win, int x, int y)
{
    XMotionEvent m;
    memset(&m, 0, sizeof m);
    m.type = MotionNotify;
    m.display = g_dpy;
    m.window = win;
    m.root = DefaultRootWindow(g_dpy);
    m.time = CurrentTime;
    m.x = x;
    m.y = y;
    m.is_hint = NotifyNormal;
    m.same_screen = True;
    XSendEvent(g_dpy, win, True, PointerMotionMask, (XEvent *)&m);
}

static void send_close(Window win)
{
    XClientMessageEvent c;
    memset(&c, 0, sizeof c);
    c.type = ClientMessage;
    c.display = g_dpy;
    c.window = win;
    c.message_type = XInternAtom(g_dpy, "WM_PROTOCOLS", False);
    c.format = 32;
    c.data.l[0] = (long)XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    c.data.l[1] = CurrentTime;
    XSendEvent(g_dpy, win, False, NoEventMask, (XEvent *)&c);
}

static void settle(PlatformWindow *a, PlatformWindow *b)
{
    for (int i = 0; i < 20; i++)
    {
        window_begin_frame(a);
        window_begin_frame(b);
        struct timespec ts = {0, 5000000};
        nanosleep(&ts, NULL);
    }
}

static int count_events(PlatformWindow *w, EventType type)
{
    int n = 0;
    Event e;
    while (poll_event(w, &e))
        n += e.type == type;
    return n;
}

int main(void)
{
    if (!platform_init())
    {
        printf("skip: no display\n");
        return 77;
    }
    WindowConfig cfg = {.title = "A", .width = 200, .height = 150, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    PlatformWindow *a = window_create(&cfg);
    cfg.title = "B";
    PlatformWindow *b = window_create(&cfg);
    if (!a || !b)
    {
        printf("skip: %s\n", platform_get_error());
        return 77;
    }
    g_dpy = window_native_handle(a, NATIVE_DISPLAY);
    CHECK(g_dpy != NULL && window_native_handle(b, NATIVE_DISPLAY) == g_dpy);
    CHECK(xwin(a) != xwin(b));
    /* These keys are injected with XSendEvent, which an input method does not take for
       real typing. A program that reads keys, not text, turns text input off, so no
       input method stands between it and the keys: do that here. */
    window_text_input_stop(a);
    window_text_input_stop(b);
    settle(a, b);

    /* A key aimed at B, read off the queue by A's pump first. */
    send_key(xwin(b), XK_a, true);
    XSync(g_dpy, False);
    window_begin_frame(a);
    CHECK(!key_down(a, KEY_A));
    window_begin_frame(b);
    CHECK(key_down(b, KEY_A));
    CHECK(key_pressed(b, KEY_A)); /* the edge survived the detour */
    CHECK(!key_down(a, KEY_A));
    window_begin_frame(b);
    CHECK(key_down(b, KEY_A) && !key_pressed(b, KEY_A));

    send_key(xwin(b), XK_a, false);
    XSync(g_dpy, False);
    window_begin_frame(a);
    window_begin_frame(b);
    CHECK(!key_down(b, KEY_A) && key_released(b, KEY_A));

    /* The same the other way round, and each window keeps its own pointer. */
    send_motion(xwin(a), 30, 40);
    send_motion(xwin(b), 70, 80);
    XSync(g_dpy, False);
    window_begin_frame(b); /* B's pump meets A's event */
    window_begin_frame(a);
    CHECK(mouse_x(a) == 30 && mouse_y(a) == 40);
    CHECK(mouse_x(b) == 70 && mouse_y(b) == 80);
    CHECK(count_events(a, EVENT_MOUSE_MOVE) >= 1);

    /* the frame's event list holds only that window's events */
    send_motion(xwin(b), 71, 81);
    XSync(g_dpy, False);
    window_begin_frame(a);
    CHECK(count_events(a, EVENT_MOUSE_MOVE) == 0);
    window_begin_frame(b);
    CHECK(count_events(b, EVENT_MOUSE_MOVE) == 1);

    /* closing B closes only B */
    send_close(xwin(b));
    XSync(g_dpy, False);
    window_begin_frame(a);
    CHECK(!window_should_close(a));
    window_begin_frame(b);
    CHECK(window_should_close(b));
    CHECK(!window_should_close(a));

    /* a window nobody pumps does not grow its list without bound */
    for (int i = 0; i < 6000; i++)
        send_motion(xwin(b), i % 100, 5);
    XSync(g_dpy, False);
    window_begin_frame(a);
    window_begin_frame(b);
    CHECK(mouse_y(b) == 5);

    /* events waiting for a window that is then destroyed are dropped quietly */
    send_key(xwin(b), XK_b, true);
    XSync(g_dpy, False);
    window_begin_frame(a);
    window_destroy(b);
    window_begin_frame(a);
    CHECK(!window_should_close(a));

    window_destroy(a);
    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
