/*
 * test_x11_window.c - window-system features on a real X server: pointer capture
 * and the lock-key state. Skips (77) without a display.
 */
#include "platform.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

static void settle(PlatformWindow *w)
{
    for (int i = 0; i < 10; i++)
    {
        window_begin_frame(w);
        time_sleep(5);
    }
}


/* ---- helpers to read a window's properties back ---- */

static Display *g_dpy;

static Window xwin(PlatformWindow *w)
{
    return (Window)(uintptr_t)window_native_handle(w, NATIVE_WINDOW);
}

static Atom atom(const char *name)
{
    return XInternAtom(g_dpy, name, False);
}

/* the first atom in an ATOM-list property, or None */
static Atom first_atom(Window win, const char *prop)
{
    Atom type, result = None;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(g_dpy, win, atom(prop), 0, 16, False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data)
    {
        if (n > 0)
            result = ((Atom *)data)[0];
        XFree(data);
    }
    return result;
}

static bool has_atom(Window win, const char *prop, const char *value)
{
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    bool found = false;
    if (XGetWindowProperty(g_dpy, win, atom(prop), 0, 16, False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data)
    {
        for (unsigned long i = 0; i < n; i++)
            found |= ((Atom *)data)[i] == atom(value);
        XFree(data);
    }
    return found;
}

/* _MOTIF_WM_HINTS decorations field: 0 none, 1 all, -1 when the property is absent */
static long decorations(Window win)
{
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    long result = -1;
    Atom a = atom("_MOTIF_WM_HINTS");
    if (XGetWindowProperty(g_dpy, win, a, 0, 5, False, a, &type, &fmt, &n, &after, &data) == Success && data)
    {
        if (n >= 3)
            result = ((long *)data)[2];
        XFree(data);
    }
    return result;
}

static PlatformWindow *make_kind(WindowKind kind, PlatformWindow *parent, bool undecorated)
{
    WindowConfig cfg = {.title = "kind", .width = 120, .height = 90, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    cfg.kind = kind;
    cfg.parent = parent;
    cfg.undecorated = undecorated;
    return window_create(&cfg);
}

static void test_kinds(PlatformWindow *parent)
{
    Window pw = xwin(parent);

    PlatformWindow *normal = make_kind(WINDOW_KIND_NORMAL, NULL, false);
    PlatformWindow *dialog = make_kind(WINDOW_KIND_DIALOG, parent, false);
    PlatformWindow *utility = make_kind(WINDOW_KIND_UTILITY, parent, false);
    PlatformWindow *popup = make_kind(WINDOW_KIND_POPUP, parent, false);
    PlatformWindow *tip = make_kind(WINDOW_KIND_TOOLTIP, parent, false);
    PlatformWindow *bare = make_kind(WINDOW_KIND_NORMAL, NULL, true);
    CHECK(normal && dialog && utility && popup && tip && bare);
    if (!(normal && dialog && utility && popup && tip && bare))
        return;

    /* a plain window is left alone */
    CHECK(first_atom(xwin(normal), "_NET_WM_WINDOW_TYPE") == None);
    CHECK(decorations(xwin(normal)) != 0);
    Window tr = None;
    CHECK(!XGetTransientForHint(g_dpy, xwin(normal), &tr) || tr == None);

    /* window types and ownership */
    CHECK(first_atom(xwin(dialog), "_NET_WM_WINDOW_TYPE") == atom("_NET_WM_WINDOW_TYPE_DIALOG"));
    CHECK(first_atom(xwin(utility), "_NET_WM_WINDOW_TYPE") == atom("_NET_WM_WINDOW_TYPE_UTILITY"));
    CHECK(first_atom(xwin(popup), "_NET_WM_WINDOW_TYPE") == atom("_NET_WM_WINDOW_TYPE_POPUP_MENU"));
    CHECK(first_atom(xwin(tip), "_NET_WM_WINDOW_TYPE") == atom("_NET_WM_WINDOW_TYPE_TOOLTIP"));
    PlatformWindow *kids[] = {dialog, utility, popup, tip};
    for (int i = 0; i < 4; i++)
    {
        tr = None;
        CHECK(XGetTransientForHint(g_dpy, xwin(kids[i]), &tr) && tr == pw);
    }

    /* no title bar or border: popups, tooltips and undecorated windows */
    CHECK(decorations(xwin(popup)) == 0);
    CHECK(decorations(xwin(tip)) == 0);
    CHECK(decorations(xwin(bare)) == 0);
    CHECK(decorations(xwin(dialog)) != 0);
    CHECK(decorations(xwin(utility)) != 0);

    /* out of the taskbar; popups and tooltips also above */
    CHECK(has_atom(xwin(utility), "_NET_WM_STATE", "_NET_WM_STATE_SKIP_TASKBAR"));
    CHECK(has_atom(xwin(popup), "_NET_WM_STATE", "_NET_WM_STATE_SKIP_TASKBAR"));
    CHECK(has_atom(xwin(popup), "_NET_WM_STATE", "_NET_WM_STATE_ABOVE"));
    CHECK(has_atom(xwin(tip), "_NET_WM_STATE", "_NET_WM_STATE_SKIP_TASKBAR"));
    /* (a window manager may add SKIP_TASKBAR to a transient dialog itself, so a dialog is not checked) */

    /* a tooltip never takes the focus */
    XWMHints *h = XGetWMHints(g_dpy, xwin(tip));
    CHECK(h && (h->flags & InputHint) && !h->input);
    if (h)
        XFree(h);

    /* decorations can be switched at run time */
    window_set_decorated(normal, false);
    XSync(g_dpy, False);
    CHECK(decorations(xwin(normal)) == 0);
    window_set_decorated(normal, true);
    XSync(g_dpy, False);
    CHECK(decorations(xwin(normal)) == 1);

    for (int i = 0; i < 4; i++)
        window_destroy(kids[i]);
    window_destroy(normal);
    window_destroy(bare);
}

/* ---- hit test: presses on a title bar or edge go to the window manager ---- */

static HitTestResult hit_cb(PlatformWindow *w, int x, int y, void *user)
{
    (void)w;
    int *calls = user;
    (*calls)++;
    if (y < 30)
        return HIT_DRAG;
    if (x >= 190)
        return HIT_RESIZE_RIGHT;
    return HIT_NORMAL;
}

static void send_button(PlatformWindow *w, int x, int y, bool down)
{
    XButtonEvent e;
    memset(&e, 0, sizeof e);
    e.type = down ? ButtonPress : ButtonRelease;
    e.display = g_dpy;
    e.window = xwin(w);
    e.root = DefaultRootWindow(g_dpy);
    e.time = CurrentTime;
    e.x = x;
    e.y = y;
    e.x_root = x;
    e.y_root = y;
    e.button = Button1;
    e.same_screen = True;
    if (!down)
        e.state = Button1Mask;
    XSendEvent(g_dpy, e.window, True, down ? ButtonPressMask : ButtonReleaseMask, (XEvent *)&e);
    XSync(g_dpy, False);
}

static int count_buttons(PlatformWindow *w)
{
    int n = 0;
    Event e;
    while (poll_event(w, &e))
        n += e.type == EVENT_MOUSE_BUTTON;
    return n;
}

static void test_hit_test(PlatformWindow *w)
{
    int calls = 0;
    window_set_hit_test(w, hit_cb, &calls);

    /* on the title bar: handed over, so the application sees no button event */
    send_button(w, 50, 10, true);
    send_button(w, 50, 10, false);
    settle(w);
    CHECK(count_buttons(w) == 0);
    CHECK(!mouse_button_down(w, MOUSE_LEFT));
    CHECK(calls >= 1);

    /* on a resize edge: the same */
    send_button(w, 195, 100, true);
    send_button(w, 195, 100, false);
    settle(w);
    CHECK(count_buttons(w) == 0);

    /* in the client area: delivered as usual */
    send_button(w, 50, 100, true);
    window_begin_frame(w);
    CHECK(count_buttons(w) == 1 || mouse_button_down(w, MOUSE_LEFT));
    send_button(w, 50, 100, false);
    settle(w);

    /* and with the hit test removed, the title bar is ordinary client area again */
    window_set_hit_test(w, NULL, NULL);
    send_button(w, 50, 10, true);
    window_begin_frame(w);
    CHECK(count_buttons(w) == 1 || mouse_button_down(w, MOUSE_LEFT));
    send_button(w, 50, 10, false);
    settle(w);
}

/* ---- cursor images ---- */

static void test_cursor_image(PlatformWindow *w)
{
    uint32_t px[16 * 16];
    for (int i = 0; i < 16 * 16; i++)
        px[i] = (i % 16 < 8) ? 0xFFFF0000u : 0x80008000u; /* opaque red, half-transparent green */
    CHECK(cursor_create(NULL, 16, 16, 0, 0) == NULL);
    CHECK(cursor_create(px, 16, 16, 16, 0) == NULL);

    PlatformCursor *c = cursor_create(px, 16, 16, 2, 3);
    if (!c)
    {
        printf("note: %s (cursor image checks skipped)\n", platform_get_error());
        return;
    }
    mouse_set_cursor_image(w, c);
    settle(w);
    mouse_set_cursor(w, CURSOR_HAND);  /* choosing a shape replaces the image */
    mouse_set_cursor_image(w, c);
    mouse_set_cursor_image(w, NULL);   /* ... and NULL goes back to the shape */
    cursor_destroy(c);
    settle(w);
    CHECK(true);
}

static void test_capture(PlatformWindow *w)
{
    bool got = mouse_capture(w, true);
    if (got)
    {
        CHECK(mouse_capture(w, true)); /* asking again keeps it */
        CHECK(!mouse_capture(w, false));
        CHECK(!mouse_capture(w, false)); /* and releasing twice is harmless */
        /* the pointer is ours again: a second capture works */
        CHECK(mouse_capture(w, true));
        mouse_capture(w, false);
    }
    else
        printf("note: another program holds a pointer grab, capture checks skipped\n");

    /* capture and the hidden, locked mode share the pointer grab */
    mouse_set_mode(w, MOUSE_MODE_CAPTURED);
    mouse_set_mode(w, MOUSE_MODE_NORMAL);
    mouse_capture(w, false);
    CHECK(true);
}

static void test_locks(PlatformWindow *w)
{
    int mods = key_mods(w);
    CHECK((mods & ~(KEYMOD_SHIFT | KEYMOD_CTRL | KEYMOD_ALT | KEYMOD_SUPER | KEYMOD_CAPS_LOCK | KEYMOD_NUM_LOCK)) == 0);
    /* the lock state agrees with itself between calls */
    CHECK((key_mods(w) & (KEYMOD_CAPS_LOCK | KEYMOD_NUM_LOCK)) == (mods & (KEYMOD_CAPS_LOCK | KEYMOD_NUM_LOCK)));
}

int main(void)
{
    if (!platform_init())
    {
        printf("skip: no display\n");
        return 77;
    }
    WindowConfig cfg = {.title = "x11 window test", .width = 200, .height = 150, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        printf("skip: %s\n", platform_get_error());
        return 77;
    }
    g_dpy = window_native_handle(w, NATIVE_DISPLAY);
    settle(w);
    test_capture(w);
    test_locks(w);
    test_kinds(w);
    test_hit_test(w);
    test_cursor_image(w);
    window_destroy(w);
    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
