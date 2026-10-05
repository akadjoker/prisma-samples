#include "platform.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <string.h>

#define TITLE "test_input_x11"

static int g_pass, g_fail;
static Display *g_dpy;
static Window g_win;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
        {                                                          \
            g_pass++;                                              \
        }                                                          \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static Window find_window(const char *title)
{
    Window root = DefaultRootWindow(g_dpy);
    Window parent;
    Window *kids = NULL;
    unsigned count = 0;
    Window found = None;
    if (!XQueryTree(g_dpy, root, &root, &parent, &kids, &count))
        return None;
    for (unsigned i = 0; i < count && found == None; i++)
    {
        char *name = NULL;
        if (XFetchName(g_dpy, kids[i], &name) && name)
        {
            if (strcmp(name, title) == 0)
                found = kids[i];
            XFree(name);
        }
    }
    if (kids)
        XFree(kids);
    return found;
}

static void send_key(KeyCode code, bool press, unsigned state)
{
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xkey.type = press ? KeyPress : KeyRelease;
    ev.xkey.display = g_dpy;
    ev.xkey.window = g_win;
    ev.xkey.root = DefaultRootWindow(g_dpy);
    ev.xkey.subwindow = None;
    ev.xkey.time = CurrentTime;
    ev.xkey.same_screen = True;
    ev.xkey.keycode = code;
    ev.xkey.state = state;
    XSendEvent(g_dpy, g_win, False, press ? KeyPressMask : KeyReleaseMask, &ev);
    XSync(g_dpy, False);
}

static void send_focus_out(void)
{
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xfocus.type = FocusOut;
    ev.xfocus.display = g_dpy;
    ev.xfocus.window = g_win;
    ev.xfocus.mode = NotifyNormal;
    ev.xfocus.detail = NotifyNonlinear;
    XSendEvent(g_dpy, g_win, False, FocusChangeMask, &ev);
    XSync(g_dpy, False);
}

static bool wait_pressed(PlatformWindow *w, int key)
{
    for (int i = 0; i < 500; i++)
    {
        window_begin_frame(w);
        if (key_pressed(w, key))
            return true;
        time_sleep(2);
    }
    return false;
}

static bool wait_released(PlatformWindow *w, int key)
{
    for (int i = 0; i < 500; i++)
    {
        window_begin_frame(w);
        if (key_released(w, key))
            return true;
        time_sleep(2);
    }
    return false;
}

/* The modifier keys held, without the lock states the machine happens to be in. */
static int held_mods(PlatformWindow *w)
{
    return key_mods(w) & ~(KEYMOD_CAPS_LOCK | KEYMOD_NUM_LOCK);
}

static void test_translation(PlatformWindow *w)
{
    static const struct
    {
        KeySym sym;
        int key;
        const char *name;
    } table[] = {
        {XK_KP_0, KEY_KP_0, "KP_0"},
        {XK_KP_1, KEY_KP_1, "KP_1"},
        {XK_KP_2, KEY_KP_2, "KP_2"},
        {XK_KP_3, KEY_KP_3, "KP_3"},
        {XK_KP_4, KEY_KP_4, "KP_4"},
        {XK_KP_5, KEY_KP_5, "KP_5"},
        {XK_KP_6, KEY_KP_6, "KP_6"},
        {XK_KP_7, KEY_KP_7, "KP_7"},
        {XK_KP_8, KEY_KP_8, "KP_8"},
        {XK_KP_9, KEY_KP_9, "KP_9"},
        {XK_KP_Decimal, KEY_KP_DECIMAL, "KP_Decimal"},
        {XK_KP_Divide, KEY_KP_DIVIDE, "KP_Divide"},
        {XK_KP_Multiply, KEY_KP_MULTIPLY, "KP_Multiply"},
        {XK_KP_Subtract, KEY_KP_SUBTRACT, "KP_Subtract"},
        {XK_KP_Add, KEY_KP_ADD, "KP_Add"},
        {XK_KP_Enter, KEY_KP_ENTER, "KP_Enter"},
        {XK_KP_Equal, KEY_KP_EQUAL, "KP_Equal"},
        {XK_Menu, KEY_MENU, "Menu"},
        {XK_Scroll_Lock, KEY_SCROLL_LOCK, "Scroll_Lock"},
        {XK_a, KEY_A, "a"},
        {XK_Return, KEY_ENTER, "Return"},
        {XK_F5, KEY_F5, "F5"},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
    {
        KeyCode code = XKeysymToKeycode(g_dpy, table[i].sym);
        if (code == 0)
        {
            printf("FAIL no keycode for %s in this keymap\n", table[i].name);
            g_fail++;
            continue;
        }
        send_key(code, true, 0);
        bool pressed = wait_pressed(w, table[i].key);
        if (!pressed)
            printf("  %s did not arrive as key %d\n", table[i].name, table[i].key);
        CHECK(pressed);
        CHECK(key_down(w, table[i].key));
        send_key(code, false, 0);
        CHECK(wait_released(w, table[i].key));
    }
}

static void test_modifiers(PlatformWindow *w)
{
    KeyCode shift = XKeysymToKeycode(g_dpy, XK_Shift_L);
    KeyCode ctrl = XKeysymToKeycode(g_dpy, XK_Control_R);
    KeyCode a = XKeysymToKeycode(g_dpy, XK_a);

    CHECK(held_mods(w) == 0);
    send_key(shift, true, 0);
    CHECK(wait_pressed(w, KEY_LEFT_SHIFT));
    CHECK(held_mods(w) == KEYMOD_SHIFT);
    send_key(ctrl, true, ShiftMask);
    CHECK(wait_pressed(w, KEY_RIGHT_CONTROL));
    CHECK(held_mods(w) == (KEYMOD_SHIFT | KEYMOD_CTRL));

    send_key(a, true, ShiftMask | ControlMask);
    window_begin_frame(w);
    Event ev;
    int mods = -1;
    for (int i = 0; i < 500 && mods < 0; i++)
    {
        while (poll_event(w, &ev))
        {
            if (ev.type == EVENT_KEY && ev.data.key.key == KEY_A && ev.data.key.down)
                mods = ev.data.key.mods;
        }
        if (mods < 0)
        {
            time_sleep(2);
            window_begin_frame(w);
        }
    }
    CHECK(mods == (KEYMOD_SHIFT | KEYMOD_CTRL));

    send_key(a, false, ShiftMask | ControlMask);
    CHECK(wait_released(w, KEY_A));
    send_key(shift, false, ShiftMask | ControlMask);
    CHECK(wait_released(w, KEY_LEFT_SHIFT));
    CHECK(held_mods(w) == KEYMOD_CTRL);
    send_key(ctrl, false, ControlMask);
    CHECK(wait_released(w, KEY_RIGHT_CONTROL));
    CHECK(held_mods(w) == 0);
}

/* A press of a key that is already down is an auto-repeat: the key stays down, it
   is not a new press, and the event says so. */
static void test_repeat(PlatformWindow *w)
{
    KeyCode a = XKeysymToKeycode(g_dpy, XK_a);

    send_key(a, true, 0);
    CHECK(wait_pressed(w, KEY_A));
    Event ev;
    while (poll_event(w, &ev))
    {
    }

    send_key(a, true, 0);
    int repeats = 0;
    int presses = 0;
    for (int i = 0; i < 500 && repeats == 0 && presses == 0; i++)
    {
        window_begin_frame(w);
        CHECK(!key_pressed(w, KEY_A));
        while (poll_event(w, &ev))
        {
            if (ev.type != EVENT_KEY || ev.data.key.key != KEY_A || !ev.data.key.down)
                continue;
            if (ev.data.key.repeat)
                repeats++;
            else
                presses++;
        }
        if (repeats == 0 && presses == 0)
            time_sleep(2);
    }
    CHECK(repeats == 1);
    CHECK(presses == 0);
    CHECK(key_down(w, KEY_A));

    send_key(a, false, 0);
    CHECK(wait_released(w, KEY_A));
    CHECK(!key_down(w, KEY_A));
}

static void test_focus_loss(PlatformWindow *w)
{
    KeyCode alt = XKeysymToKeycode(g_dpy, XK_Alt_L);
    KeyCode a = XKeysymToKeycode(g_dpy, XK_a);

    send_key(alt, true, 0);
    CHECK(wait_pressed(w, KEY_LEFT_ALT));
    send_key(a, true, Mod1Mask);
    CHECK(wait_pressed(w, KEY_A));
    CHECK(held_mods(w) == KEYMOD_ALT);

    send_focus_out();
    CHECK(wait_released(w, KEY_LEFT_ALT));
    CHECK(!key_down(w, KEY_A));
    CHECK(!key_down(w, KEY_LEFT_ALT));
    CHECK(held_mods(w) == 0);
    CHECK(!window_is_focused(w));
}

static void test_cursors(PlatformWindow *w)
{
    for (int round = 0; round < 3; round++)
    {
        for (int c = -2; c < CURSOR_COUNT + 3; c++)
            mouse_set_cursor(w, c);
    }
    mouse_set_mode(w, MOUSE_MODE_HIDDEN);
    mouse_set_cursor(w, CURSOR_RESIZE_NWSE);
    mouse_set_mode(w, MOUSE_MODE_NORMAL);

    WindowConfig cfg = {.title = "test_input_x11_second", .width = 32, .height = 32, .render = RENDER_PIXELS};
    PlatformWindow *w2 = window_create(&cfg);
    CHECK(w2 != NULL);
    if (w2)
    {
        mouse_set_cursor(w2, CURSOR_RESIZE_ALL);
        window_destroy(w2);
    }
    mouse_set_cursor(w, CURSOR_RESIZE_ALL);
    mouse_set_cursor(w, CURSOR_RESIZE_NESW);
    window_begin_frame(w);
    CHECK(true);
}

int main(void)
{
    if (!platform_init())
    {
        printf("SKIP %s\n", platform_get_error());
        return 77;
    }
    CHECK(platform_get_error()[0] == '\0');
    g_dpy = XOpenDisplay(NULL);
    WindowConfig cfg = {.title = TITLE, .width = 64, .height = 64, .render = RENDER_PIXELS};
    PlatformWindow *w = window_create(&cfg);
    if (!g_dpy || !w)
    {
        printf("FAIL setup\n");
        return 1;
    }
    for (int i = 0; i < 50 && !g_win; i++)
    {
        window_begin_frame(w);
        g_win = find_window(TITLE);
        time_sleep(2);
    }
    CHECK(g_win != None);

    if (g_win)
    {
        test_translation(w);
        test_modifiers(w);
        test_repeat(w);
        test_focus_loss(w);
        test_cursors(w);
    }

    window_destroy(w);
    XCloseDisplay(g_dpy);
    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
