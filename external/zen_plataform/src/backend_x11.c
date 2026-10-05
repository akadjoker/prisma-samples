/*
 * backend_x11.c - X11 + GLX backend. Creates the window and GL context, pumps the
 * Xlib event queue, and normalizes everything into core_push_event / core_push_char.
 * No input logic lives here; that is the core's job.
 *
 * Naming note: the public opaque type is PlatformWindow precisely so Xlib's
 * 'Window' (an XID) can be used here without a clash. In this file 'Window' is
 * always Xlib's; our per-window handle is BackendWindow.
 */
#include "core_internal.h"
#include "backend.h"
#include "error_internal.h"
#include "vulkan_internal.h"
#include "mime_util.h"
#include "preedit.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
/* XInput 2.2 gives real touch events. Only the header is needed at build time: libXi
   itself is opened at run time, so nothing new is linked. Without the header the
   backend simply has no touch input. */
#if defined(__has_include) && !defined(ZEN_NO_XI2)
#if __has_include(<X11/extensions/XInput2.h>)
#include <X11/extensions/XInput2.h>
#define ZEN_HAVE_XI2 1
#endif
#endif
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xresource.h>
#include <GL/glx.h>

#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display *, GLXFBConfig, GLXContext, Bool, const int *);
typedef void (*glXSwapIntervalEXTProc)(Display *, GLXDrawable, int);
typedef int (*glXSwapIntervalMESAProc)(unsigned int);
typedef int (*glXSwapIntervalSGIProc)(int);

#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#ifndef GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB
#define GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#endif
#ifndef GLX_CONTEXT_ES2_PROFILE_BIT_EXT
#define GLX_CONTEXT_ES2_PROFILE_BIT_EXT 0x00000004
#endif
#ifndef GLX_CONTEXT_FLAGS_ARB
#define GLX_CONTEXT_FLAGS_ARB 0x2094
#endif
#ifndef GLX_CONTEXT_DEBUG_BIT_ARB
#define GLX_CONTEXT_DEBUG_BIT_ARB 0x00000001
#endif

/* ---- global connection ---- */

/* ---- clipboard state types ---- */
#define CLIP_MAX 8
#define CLIP_INCR_MAX 4
#define CLIP_CHUNK_MAX 262144       /* above this a paste goes by INCR */
#define CLIP_TIMEOUT_MS 1000        /* wait for another client, per step */
#define CLIP_READ_LIMIT (512u << 20) /* refuse pastes larger than this */

typedef struct
{
    char *mime;
    Atom atom; /* the selection target for this type */
    uint8_t *data;
    size_t size;
} ClipEntry;

typedef struct
{
    bool active;
    Window requestor;
    Atom property, target;
    const uint8_t *data; /* points into a ClipEntry; transfers are aborted when the entries go */
    size_t size, offset;
} IncrOut;

static struct
{
    Display *dpy;
    int screen;
    Window root;
    XContext ctx; /* xid -> BackendWindow* */
    XIM xim;
    bool detectable_repeat; /* a held key sends presses only, no synthetic releases */

    Atom WM_PROTOCOLS, WM_DELETE_WINDOW, NET_WM_NAME, UTF8_STRING;
    Atom NET_WM_STATE, NET_WM_STATE_FULLSCREEN, NET_WM_STATE_MAXIMIZED_VERT,
        NET_WM_STATE_MAXIMIZED_HORZ, NET_WM_STATE_HIDDEN, NET_WM_STATE_ABOVE,
        NET_WM_STATE_DEMANDS_ATTENTION, NET_ACTIVE_WINDOW, NET_WM_ICON,
        NET_WM_WINDOW_OPACITY, MOTIF_WM_HINTS;
    Atom CLIPBOARD, TARGETS, XSEL_DATA, INCR, TEXT_PLAIN, TEXT_PLAIN_UTF8, TEXT;

    glXCreateContextAttribsARBProc create_context;
    bool has_es_profile;
    glXSwapIntervalEXTProc swap_ext;
    glXSwapIntervalMESAProc swap_mesa;
    glXSwapIntervalSGIProc swap_sgi;

    Cursor cursors[CURSOR_COUNT]; /* created on first use, shared by every window */

    Window helper; /* unmapped window that owns the CLIPBOARD selection */
    int xi_opcode;   /* XInputExtension, when XInput 2.2 touch is available */
    bool xi_touch;
    int (*xi_select)(Display *, Window, void *mask, int count); /* XISelectEvents */
    ClipEntry clip[CLIP_MAX]; /* what we offer while we own the selection */
    int clip_count;
    IncrOut incr[CLIP_INCR_MAX]; /* large pastes being sent in chunks */
} g;

static void clip_free_entries(void);

struct BackendWindow
{
    Window win;
    GLXContext glc;
    Colormap colormap;
    XIC xic;
    /* input method: composing text shown by the application (XIM preedit callbacks) */
    Preedit preedit;
    bool preedit_dirty;           /* an EVENT_TEXT_EDIT is due at the end of the pump */
    bool text_input;              /* window_text_input_start/stop */
    XIMCallback cb_start, cb_done, cb_draw, cb_caret;
    Core *core; /* set at the start of each pump so handlers can reach the core */
    struct PendingX *pending; /* events other windows' pumps read off the shared queue for this one */
    int pending_n, pending_cap;

    WindowMode mode;
    int width, height;
    int pos_x, pos_y;
    int saved_x, saved_y, saved_w, saved_h; /* restore from fullscreen */
    bool focused, minimized, maximized, visible, hovered;

    int cursor_mode; /* MOUSE_MODE_* */
    Cursor active_cursor;
    int cursor_idx;                /* the CURSOR_* shape chosen with mouse_set_cursor */
    HitTestFunc hit_cb;            /* custom title bar / edges */
    PlatformWindow *hit_w;
    void *hit_user;
    bool hit_press;                /* a press was handed to the window manager */
    int hit_cursor;                /* resize cursor showing from the hit test, or -1 */
    Cursor hidden_cursor;
    bool grabbed;  /* MOUSE_MODE_CAPTURED holds the pointer */
    bool captured; /* mouse_capture(true) holds it */

    RenderMode render;
    GC gc;         /* pixel mode: blits the XImage to the window */
    XImage *image; /* pixel mode: the CPU framebuffer */
};

/* ========================================================================== */
/*  keysym + modifier translation                                             */
/* ========================================================================== */

static int translate_keysym(KeySym ks)
{
    if (ks >= XK_a && ks <= XK_z)
        return KEY_A + (int)(ks - XK_a);
    if (ks >= XK_A && ks <= XK_Z)
        return KEY_A + (int)(ks - XK_A);
    if (ks >= XK_0 && ks <= XK_9)
        return KEY_ZERO + (int)(ks - XK_0);
    if (ks >= XK_F1 && ks <= XK_F12)
        return KEY_F1 + (int)(ks - XK_F1);
    if (ks >= XK_KP_0 && ks <= XK_KP_9)
        return KEY_KP_0 + (int)(ks - XK_KP_0);

    switch (ks)
    {
    case XK_space:
        return KEY_SPACE;
    case XK_apostrophe:
        return KEY_APOSTROPHE;
    case XK_comma:
        return KEY_COMMA;
    case XK_minus:
        return KEY_MINUS;
    case XK_period:
        return KEY_PERIOD;
    case XK_slash:
        return KEY_SLASH;
    case XK_semicolon:
        return KEY_SEMICOLON;
    case XK_equal:
        return KEY_EQUAL;
    case XK_bracketleft:
        return KEY_LEFT_BRACKET;
    case XK_backslash:
        return KEY_BACKSLASH;
    case XK_bracketright:
        return KEY_RIGHT_BRACKET;
    case XK_grave:
        return KEY_GRAVE;
    case XK_Escape:
        return KEY_ESCAPE;
    case XK_Return:
        return KEY_ENTER;
    case XK_Tab:
        return KEY_TAB;
    case XK_BackSpace:
        return KEY_BACKSPACE;
    case XK_Insert:
        return KEY_INSERT;
    case XK_Delete:
        return KEY_DELETE;
    case XK_Right:
        return KEY_RIGHT;
    case XK_Left:
        return KEY_LEFT;
    case XK_Down:
        return KEY_DOWN;
    case XK_Up:
        return KEY_UP;
    case XK_Page_Up:
        return KEY_PAGE_UP;
    case XK_Page_Down:
        return KEY_PAGE_DOWN;
    case XK_Home:
        return KEY_HOME;
    case XK_End:
        return KEY_END;
    case XK_Caps_Lock:
        return KEY_CAPS_LOCK;
    case XK_Num_Lock:
        return KEY_NUM_LOCK;
    case XK_Print:
        return KEY_PRINT_SCREEN;
    case XK_Pause:
        return KEY_PAUSE;
    case XK_Scroll_Lock:
        return KEY_SCROLL_LOCK;
    case XK_Menu:
        return KEY_MENU;
    case XK_KP_Insert:
        return KEY_KP_0;
    case XK_KP_End:
        return KEY_KP_1;
    case XK_KP_Down:
        return KEY_KP_2;
    case XK_KP_Page_Down:
        return KEY_KP_3;
    case XK_KP_Left:
        return KEY_KP_4;
    case XK_KP_Begin:
        return KEY_KP_5;
    case XK_KP_Right:
        return KEY_KP_6;
    case XK_KP_Home:
        return KEY_KP_7;
    case XK_KP_Up:
        return KEY_KP_8;
    case XK_KP_Page_Up:
        return KEY_KP_9;
    case XK_KP_Delete:
    case XK_KP_Decimal:
    case XK_KP_Separator:
        return KEY_KP_DECIMAL;
    case XK_KP_Divide:
        return KEY_KP_DIVIDE;
    case XK_KP_Multiply:
        return KEY_KP_MULTIPLY;
    case XK_KP_Subtract:
        return KEY_KP_SUBTRACT;
    case XK_KP_Add:
        return KEY_KP_ADD;
    case XK_KP_Enter:
        return KEY_KP_ENTER;
    case XK_KP_Equal:
        return KEY_KP_EQUAL;
    case XK_Shift_L:
        return KEY_LEFT_SHIFT;
    case XK_Control_L:
        return KEY_LEFT_CONTROL;
    case XK_Alt_L:
        return KEY_LEFT_ALT;
    case XK_Super_L:
        return KEY_LEFT_SUPER;
    case XK_Shift_R:
        return KEY_RIGHT_SHIFT;
    case XK_Control_R:
        return KEY_RIGHT_CONTROL;
    case XK_Alt_R:
        return KEY_RIGHT_ALT;
    case XK_Super_R:
        return KEY_RIGHT_SUPER;
    default:
        return KEY_NULL;
    }
}

static int translate_mods(unsigned int state)
{
    int mods = 0;
    if (state & ShiftMask)
        mods |= KEYMOD_SHIFT;
    if (state & ControlMask)
        mods |= KEYMOD_CTRL;
    if (state & Mod1Mask)
        mods |= KEYMOD_ALT;
    if (state & Mod4Mask)
        mods |= KEYMOD_SUPER;
    return mods;
}

/* ========================================================================== */
/*  _NET_WM_STATE helpers                                                     */
/* ========================================================================== */

static void send_wm_state(BackendWindow *b, int action, Atom a, Atom bb)
{
    XEvent e = {0};
    e.type = ClientMessage;
    e.xclient.window = b->win;
    e.xclient.message_type = g.NET_WM_STATE;
    e.xclient.format = 32;
    e.xclient.data.l[0] = action; /* 0 remove, 1 add, 2 toggle */
    e.xclient.data.l[1] = (long)a;
    e.xclient.data.l[2] = (long)bb;
    e.xclient.data.l[3] = 1;
    XSendEvent(g.dpy, g.root, False,
               SubstructureNotifyMask | SubstructureRedirectMask, &e);
}

static void refresh_wm_state(BackendWindow *b)
{
    Atom type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(g.dpy, b->win, g.NET_WM_STATE, 0, 64, False, XA_ATOM,
                           &type, &fmt, &n, &after, &data) != Success ||
        !data)
        return;

    Atom *states = (Atom *)data;
    bool maxv = false, maxh = false, hidden = false;
    for (unsigned long i = 0; i < n; i++)
    {
        if (states[i] == g.NET_WM_STATE_MAXIMIZED_VERT)
            maxv = true;
        else if (states[i] == g.NET_WM_STATE_MAXIMIZED_HORZ)
            maxh = true;
        else if (states[i] == g.NET_WM_STATE_HIDDEN)
            hidden = true;
    }
    b->maximized = maxv && maxh;
    b->minimized = hidden;
    XFree(data);
}

/* ========================================================================== */
/*  GL context                                                                */
/* ========================================================================== */

static bool glx_has_extension(const char *name)
{
    const char *list = glXQueryExtensionsString(g.dpy, g.screen);
    size_t n = strlen(name);
    for (const char *p = list; p && *p;)
    {
        const char *end = strchr(p, ' ');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, name, n) == 0)
            return true;
        p = end ? end + 1 : p + len;
    }
    return false;
}

static GLXFBConfig choose_fbconfig(const GLConfig *gl)
{
    int attribs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24, GLX_STENCIL_SIZE, 8,
        GLX_DOUBLEBUFFER, True,
        GLX_SAMPLE_BUFFERS, gl->msaa > 0 ? 1 : 0,
        GLX_SAMPLES, gl->msaa > 0 ? gl->msaa : 0,
        None};

    int count = 0;
    GLXFBConfig *configs = glXChooseFBConfig(g.dpy, g.screen, attribs, &count);
    if (!configs || count == 0)
    {
        if (configs)
            XFree(configs);
        error_set("no framebuffer configuration with RGBA8, depth 24, stencil 8 and %d samples", gl->msaa);
        return NULL;
    }
    GLXFBConfig chosen = configs[0];
    XFree(configs);
    return chosen;
}

static bool g_context_failed;

static int context_error_handler(Display *dpy, XErrorEvent *ev)
{
    (void)dpy;
    (void)ev;
    g_context_failed = true;
    return 0;
}

static GLXContext create_context(GLXFBConfig fb, const GLConfig *gl)
{
    GLProfile profile = gl->profile == GL_PROFILE_DEFAULT ? GL_PROFILE_CORE : gl->profile;
    int major = gl->major;
    int minor = gl->minor;
    if (major == 0)
    {
        major = 3;
        minor = profile == GL_PROFILE_ES ? 0 : 3;
    }

    const char *name = "core";
    int mask = GLX_CONTEXT_CORE_PROFILE_BIT_ARB;
    if (profile == GL_PROFILE_COMPAT)
    {
        name = "compatibility";
        mask = GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
    }
    else if (profile == GL_PROFILE_ES)
    {
        name = "ES";
        mask = GLX_CONTEXT_ES2_PROFILE_BIT_EXT;
        if (!g.has_es_profile)
        {
            error_set("GLX_EXT_create_context_es2_profile is not supported");
            return NULL;
        }
    }
    if (!g.create_context)
    {
        error_set("GLX_ARB_create_context is not supported");
        return NULL;
    }

    int attribs[9];
    int n = 0;
    attribs[n++] = GLX_CONTEXT_MAJOR_VERSION_ARB;
    attribs[n++] = major;
    attribs[n++] = GLX_CONTEXT_MINOR_VERSION_ARB;
    attribs[n++] = minor;
    attribs[n++] = GLX_CONTEXT_PROFILE_MASK_ARB;
    attribs[n++] = mask;
    if (gl->debug)
    {
        attribs[n++] = GLX_CONTEXT_FLAGS_ARB;
        attribs[n++] = GLX_CONTEXT_DEBUG_BIT_ARB;
    }
    attribs[n] = None;

    g_context_failed = false;
    XErrorHandler previous = XSetErrorHandler(context_error_handler);
    GLXContext c = g.create_context(g.dpy, fb, NULL, True, attribs);
    XSync(g.dpy, False);
    XSetErrorHandler(previous);

    if (g_context_failed || !c)
    {
        if (c)
            glXDestroyContext(g.dpy, c);
        error_set("cannot create an OpenGL %s %d.%d context%s", name, major, minor, gl->debug ? " with debug" : "");
        return NULL;
    }
    return c;
}

/* ========================================================================== */
/*  init / shutdown                                                           */
/* ========================================================================== */

bool backend_init(void)
{
    g.dpy = XOpenDisplay(NULL);
    if (!g.dpy)
        return error_set("cannot open the X display");
    g.screen = DefaultScreen(g.dpy);
    g.root = RootWindow(g.dpy, g.screen);
    g.ctx = XUniqueContext();

    /* Without this a held key arrives as release/press pairs, and an input method
       that takes the events apart makes the key read as up between them. */
    Bool detectable = False;
    XkbSetDetectableAutoRepeat(g.dpy, True, &detectable);
    g.detectable_repeat = detectable == True;

    g.WM_PROTOCOLS = XInternAtom(g.dpy, "WM_PROTOCOLS", False);
    g.WM_DELETE_WINDOW = XInternAtom(g.dpy, "WM_DELETE_WINDOW", False);
    g.NET_WM_NAME = XInternAtom(g.dpy, "_NET_WM_NAME", False);
    g.UTF8_STRING = XInternAtom(g.dpy, "UTF8_STRING", False);
    g.NET_WM_STATE = XInternAtom(g.dpy, "_NET_WM_STATE", False);
    g.NET_WM_STATE_FULLSCREEN = XInternAtom(g.dpy, "_NET_WM_STATE_FULLSCREEN", False);
    g.NET_WM_STATE_MAXIMIZED_VERT = XInternAtom(g.dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    g.NET_WM_STATE_MAXIMIZED_HORZ = XInternAtom(g.dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    g.NET_WM_STATE_HIDDEN = XInternAtom(g.dpy, "_NET_WM_STATE_HIDDEN", False);
    g.NET_WM_STATE_ABOVE = XInternAtom(g.dpy, "_NET_WM_STATE_ABOVE", False);
    g.NET_WM_STATE_DEMANDS_ATTENTION = XInternAtom(g.dpy, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
    g.NET_ACTIVE_WINDOW = XInternAtom(g.dpy, "_NET_ACTIVE_WINDOW", False);
    g.NET_WM_ICON = XInternAtom(g.dpy, "_NET_WM_ICON", False);
    g.NET_WM_WINDOW_OPACITY = XInternAtom(g.dpy, "_NET_WM_WINDOW_OPACITY", False);
    g.MOTIF_WM_HINTS = XInternAtom(g.dpy, "_MOTIF_WM_HINTS", False);
    g.CLIPBOARD = XInternAtom(g.dpy, "CLIPBOARD", False);
    g.TARGETS = XInternAtom(g.dpy, "TARGETS", False);
    g.XSEL_DATA = XInternAtom(g.dpy, "PLATFORM_CLIPBOARD", False);
    g.INCR = XInternAtom(g.dpy, "INCR", False);
    g.TEXT_PLAIN = XInternAtom(g.dpy, "text/plain", False);
    g.TEXT_PLAIN_UTF8 = XInternAtom(g.dpy, "text/plain;charset=utf-8", False);
    g.TEXT = XInternAtom(g.dpy, "TEXT", False);

    /* A 1x1 unmapped window owns the CLIPBOARD selection and answers conversion
       requests, so clipboard ownership outlives any single visible window. */
    g.helper = XCreateSimpleWindow(g.dpy, g.root, -10, -10, 1, 1, 0, 0, 0);
    XSelectInput(g.dpy, g.helper, PropertyChangeMask);

#if ZEN_HAVE_XI2
    {
        int ev, err;
        if (XQueryExtension(g.dpy, "XInputExtension", &g.xi_opcode, &ev, &err))
        {
            SharedLibrary *xi = library_open("libXi.so.6");
            int (*query)(Display *, int *, int *) = NULL;
            if (xi)
            {
                *(void **)&query = library_symbol(xi, "XIQueryVersion");
                *(void **)&g.xi_select = library_symbol(xi, "XISelectEvents");
            }
            int major = 2, minor = 2;
            if (query && g.xi_select && query(g.dpy, &major, &minor) == Success && (major > 2 || minor >= 2))
            {
                g.xi_touch = true;
                log_debug("X11: touch input through XInput %d.%d", major, minor);
            }
        }
    }
#endif

    /* An input method needs the locale's character set and its modifiers, or XOpenIM
       can only give the plain built-in one. Only LC_CTYPE is changed: number
       formatting stays as the application had it. */
    setlocale(LC_CTYPE, "");
    if (XSupportsLocale())
        XSetLocaleModifiers("");
    g.xim = XOpenIM(g.dpy, NULL, NULL, NULL);

    g.create_context = (glXCreateContextAttribsARBProc)glXGetProcAddressARB((const GLubyte *)"glXCreateContextAttribsARB");
    g.has_es_profile = glx_has_extension("GLX_EXT_create_context_es2_profile");
    g.swap_ext = (glXSwapIntervalEXTProc)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalEXT");
    g.swap_mesa = (glXSwapIntervalMESAProc)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalMESA");
    g.swap_sgi = (glXSwapIntervalSGIProc)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalSGI");
    return true;
}

void backend_shutdown(void)
{
    clip_free_entries();
    for (int i = 0; i < CURSOR_COUNT; i++)
    {
        if (g.cursors[i])
            XFreeCursor(g.dpy, g.cursors[i]);
    }
    if (g.helper)
        XDestroyWindow(g.dpy, g.helper);
    if (g.xim)
        XCloseIM(g.xim);
    if (g.dpy)
        XCloseDisplay(g.dpy);
    memset(&g, 0, sizeof g);
}

/* ========================================================================== */
/*  create / destroy                                                          */
/* ========================================================================== */

static void set_title(BackendWindow *b, const char *title)
{
    if (!title)
        title = "";
    XStoreName(g.dpy, b->win, title);
    XChangeProperty(g.dpy, b->win, g.NET_WM_NAME, g.UTF8_STRING, 8,
                    PropModeReplace, (const unsigned char *)title, (int)strlen(title));
}

/* ---- input method: on-the-spot composition through the XIM preedit callbacks ---- */

static int pre_start(XIC ic, XPointer client, XPointer call)
{
    (void)ic, (void)client, (void)call;
    return -1; /* no limit on the composition length */
}

static void pre_done(XIC ic, XPointer client, XPointer call)
{
    (void)ic, (void)call;
    BackendWindow *b = (BackendWindow *)client;
    preedit_clear(&b->preedit);
    b->preedit_dirty = true;
}

static void pre_draw(XIC ic, XPointer client, XPointer call)
{
    (void)ic;
    BackendWindow *b = (BackendWindow *)client;
    const XIMPreeditDrawCallbackStruct *d = (const XIMPreeditDrawCallbackStruct *)call;
    uint32_t ins[PREEDIT_CAP];
    int n = 0;
    if (d->text)
    {
        const XIMText *t = d->text;
        if (t->encoding_is_wchar && t->string.wide_char)
        {
            for (unsigned i = 0; i < t->length && n < PREEDIT_CAP; i++)
                ins[n++] = (uint32_t)t->string.wide_char[i];
        }
        else if (t->string.multi_byte)
            n = preedit_from_utf8(t->string.multi_byte, (int)strlen(t->string.multi_byte), ins, PREEDIT_CAP);
    }
    preedit_draw(&b->preedit, d->caret, d->chg_first, d->chg_length, ins, n);
    b->preedit_dirty = true;
}

static void pre_caret(XIC ic, XPointer client, XPointer call)
{
    (void)ic, (void)client, (void)call;
}

/* Whether the input method can hand the composition to the application. */
static bool im_has_callbacks(void)
{
    XIMStyles *styles = NULL;
    if (!g.xim || XGetIMValues(g.xim, XNQueryInputStyle, &styles, NULL) != NULL || !styles)
        return false;
    bool ok = false;
    for (unsigned i = 0; i < styles->count_styles; i++)
        ok |= styles->supported_styles[i] == (XIMPreeditCallbacks | XIMStatusNothing);
    XFree(styles);
    return ok;
}

static XIC create_ic(BackendWindow *b)
{
    if (!g.xim)
        return NULL;
    /* ZEN_NO_IME=1 keeps the input method out of the application's drawing, should one
       of them misbehave: it then composes in its own window. */
    if (!getenv("ZEN_NO_IME") && im_has_callbacks())
    {
        b->cb_start = (XIMCallback){(XPointer)b, (XIMProc)(void (*)(void))pre_start};
        b->cb_done = (XIMCallback){(XPointer)b, (XIMProc)(void (*)(void))pre_done};
        b->cb_draw = (XIMCallback){(XPointer)b, (XIMProc)(void (*)(void))pre_draw};
        b->cb_caret = (XIMCallback){(XPointer)b, (XIMProc)(void (*)(void))pre_caret};
        XVaNestedList list = XVaCreateNestedList(0, XNPreeditStartCallback, &b->cb_start, XNPreeditDoneCallback,
                                                 &b->cb_done, XNPreeditDrawCallback, &b->cb_draw,
                                                 XNPreeditCaretCallback, &b->cb_caret, NULL);
        XIC ic = XCreateIC(g.xim, XNInputStyle, XIMPreeditCallbacks | XIMStatusNothing, XNClientWindow, b->win,
                           XNFocusWindow, b->win, XNPreeditAttributes, list, NULL);
        XFree(list);
        if (ic)
        {
            log_debug("X11: the input method composes through preedit callbacks");
            return ic;
        }
    }
    return XCreateIC(g.xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, b->win, XNFocusWindow,
                     b->win, NULL);
}

/* The composition changed during this pump: tell the application once. */
static void flush_preedit(BackendWindow *b)
{
    if (!b->preedit_dirty || !b->core)
        return;
    b->preedit_dirty = false;
    Event e = {.type = EVENT_TEXT_EDIT};
    preedit_to_utf8(&b->preedit, e.data.edit.text, (int)sizeof e.data.edit.text, &e.data.edit.cursor);
    core_push_event(b->core, &e);
}

void backend_set_text_input(BackendWindow *b, bool on)
{
    b->text_input = on;
    if (!b->xic)
        return;
    if (on && b->focused)
        XSetICFocus(b->xic);
    else
    {
        XUnsetICFocus(b->xic);
        if (!on)
        {
            char *left = Xutf8ResetIC(b->xic); /* drop what the method was composing */
            if (left)
                XFree(left);
            preedit_clear(&b->preedit);
            b->preedit_dirty = false;
        }
    }
}

void backend_set_text_input_rect(BackendWindow *b, int x, int y, int w, int h)
{
    (void)w;
    if (!b->xic)
        return;
    /* the spot is where the candidate list goes: just under the caret */
    XPoint spot = {(short)x, (short)(y + h)};
    XVaNestedList list = XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
    XSetICValues(b->xic, XNPreeditAttributes, list, NULL); /* some methods ignore it */
    XFree(list);
}

/* ---- decorations and window kinds ---- */

static void apply_decorations(BackendWindow *b, bool decorated)
{
    /* _MOTIF_WM_HINTS: flags = MWM_HINTS_DECORATIONS, decorations = all or none */
    long hints[5] = {2, 0, decorated ? 1 : 0, 0, 0};
    XChangeProperty(g.dpy, b->win, g.MOTIF_WM_HINTS, g.MOTIF_WM_HINTS, 32, PropModeReplace,
                    (unsigned char *)hints, 5);
}

static void apply_window_kind(BackendWindow *b, const WindowConfig *cfg)
{
    WindowKind k = cfg->kind;
    if (cfg->undecorated || k == WINDOW_KIND_POPUP || k == WINDOW_KIND_TOOLTIP)
        apply_decorations(b, false);

    if (cfg->parent && cfg->parent->b)
        XSetTransientForHint(g.dpy, b->win, cfg->parent->b->win);

    const char *type = k == WINDOW_KIND_DIALOG    ? "_NET_WM_WINDOW_TYPE_DIALOG"
                       : k == WINDOW_KIND_UTILITY ? "_NET_WM_WINDOW_TYPE_UTILITY"
                       : k == WINDOW_KIND_POPUP   ? "_NET_WM_WINDOW_TYPE_POPUP_MENU"
                       : k == WINDOW_KIND_TOOLTIP ? "_NET_WM_WINDOW_TYPE_TOOLTIP"
                                                  : NULL;
    if (type)
    {
        Atom t = XInternAtom(g.dpy, type, False);
        XChangeProperty(g.dpy, b->win, XInternAtom(g.dpy, "_NET_WM_WINDOW_TYPE", False), XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)&t, 1);
    }
    if (k == WINDOW_KIND_UTILITY || k == WINDOW_KIND_POPUP || k == WINDOW_KIND_TOOLTIP)
    {
        Atom state[2];
        int n = 0;
        state[n++] = XInternAtom(g.dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
        if (k != WINDOW_KIND_UTILITY)
            state[n++] = g.NET_WM_STATE_ABOVE;
        XChangeProperty(g.dpy, b->win, g.NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *)state, n);
    }
    if (k == WINDOW_KIND_TOOLTIP)
    {
        XWMHints *h = XAllocWMHints(); /* a tooltip must never take the focus */
        if (h)
        {
            h->flags = InputHint;
            h->input = False;
            XSetWMHints(g.dpy, b->win, h);
            XFree(h);
        }
    }
}

void backend_set_decorated(BackendWindow *b, bool on)
{
    apply_decorations(b, on);
    XFlush(g.dpy);
}

BackendWindow *backend_create(const WindowConfig *cfg)
{
    /* Pixel and Vulkan windows take the default TrueColor visual (no GLX involved);
       GL windows take the visual of a chosen framebuffer config. */
    GLXFBConfig fb = NULL;
    XVisualInfo vinfo;
    XVisualInfo *vi;
    if (cfg->render != RENDER_GL)
    {
        vi = &vinfo;
        if (!XMatchVisualInfo(g.dpy, g.screen, DefaultDepth(g.dpy, g.screen), TrueColor, vi))
        {
            error_set("no TrueColor visual");
            return NULL;
        }
    }
    else
    {
        fb = choose_fbconfig(&cfg->gl);
        if (!fb)
            return NULL;
        vi = glXGetVisualFromFBConfig(g.dpy, fb);
        if (!vi)
        {
            error_set("the framebuffer configuration has no visual");
            return NULL;
        }
    }

    BackendWindow *b = calloc(1, sizeof *b);
    if (!b)
    {
        if (cfg->render == RENDER_GL)
            XFree(vi);
        return NULL;
    }
    b->render = cfg->render;

    b->colormap = XCreateColormap(g.dpy, g.root, vi->visual, AllocNone);

    XSetWindowAttributes swa = {0};
    swa.colormap = b->colormap;
    swa.background_pixel = 0;
    swa.border_pixel = 0;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask | FocusChangeMask | EnterWindowMask |
                     LeaveWindowMask | PropertyChangeMask;

    int x = (cfg->x == WINDOW_POS_CENTERED || cfg->x == WINDOW_POS_UNDEFINED) ? 0 : cfg->x;
    int y = (cfg->y == WINDOW_POS_CENTERED || cfg->y == WINDOW_POS_UNDEFINED) ? 0 : cfg->y;
    int w = cfg->width > 0 ? cfg->width : 640;
    int h = cfg->height > 0 ? cfg->height : 480;

    b->win = XCreateWindow(g.dpy, g.root, x, y, (unsigned)w, (unsigned)h, 0,
                           vi->depth, InputOutput, vi->visual,
                           CWColormap | CWEventMask | CWBackPixel | CWBorderPixel, &swa);
    if (cfg->render == RENDER_GL)
        XFree(vi);
    if (!b->win)
    {
        free(b);
        return NULL;
    }

    b->width = w;
    b->height = h;
    b->pos_x = x;
    b->pos_y = y;
    b->mode = cfg->mode;
    b->cursor_mode = MOUSE_MODE_NORMAL;
    b->hit_cursor = -1;

    XSaveContext(g.dpy, b->win, g.ctx, (XPointer)b);
    XSetWMProtocols(g.dpy, b->win, &g.WM_DELETE_WINDOW, 1);
    set_title(b, cfg->title);

    XClassHint *cls = XAllocClassHint();
    if (cls)
    {
        cls->res_name = (char *)(cfg->title ? cfg->title : "platform");
        cls->res_class = (char *)"Platform";
        XSetClassHint(g.dpy, b->win, cls);
        XFree(cls);
    }

    XSizeHints *hints = XAllocSizeHints();
    if (hints)
    {
        if (!cfg->resizable)
        {
            hints->flags |= PMinSize | PMaxSize;
            hints->min_width = hints->max_width = w;
            hints->min_height = hints->max_height = h;
        }
        if (cfg->x != WINDOW_POS_UNDEFINED && cfg->y != WINDOW_POS_UNDEFINED)
        {
            hints->flags |= PPosition;
            hints->x = x;
            hints->y = y;
        }
        XSetWMNormalHints(g.dpy, b->win, hints);
        XFree(hints);
    }

    b->text_input = true;
    b->xic = create_ic(b);

    if (cfg->render == RENDER_PIXELS)
    {
        b->gc = XCreateGC(g.dpy, b->win, 0, NULL);
    }
    else if (cfg->render == RENDER_GL)
    {
        b->glc = create_context(fb, &cfg->gl);
        if (!b->glc)
        {
            backend_destroy(b);
            return NULL;
        }
        glXMakeCurrent(g.dpy, b->win, b->glc);
    }

    apply_window_kind(b, cfg);
#if ZEN_HAVE_XI2
    if (g.xi_touch)
    {
        unsigned char bits[XIMaskLen(XI_LASTEVENT)];
        memset(bits, 0, sizeof bits);
        XISetMask(bits, XI_TouchBegin);
        XISetMask(bits, XI_TouchUpdate);
        XISetMask(bits, XI_TouchEnd);
        XIEventMask mask = {.deviceid = XIAllMasterDevices, .mask_len = (int)sizeof bits, .mask = bits};
        g.xi_select(g.dpy, b->win, &mask, 1);
    }
#endif

    XMapWindow(g.dpy, b->win);

    /* Honour cfg->monitor when centering or going fullscreen.
       monitor < 0 means MONITOR_CURRENT – resolve to whichever monitor
       contains the window now (after XMapWindow placed it). */
    int target_monitor = cfg->monitor;
    if (target_monitor < 0)
    {
        /* Find which monitor the window landed on. */
        int nmon = backend_monitor_count();
        for (int i = 0; i < nmon; i++)
        {
            MonitorInfo mi;
            if (backend_monitor_info(i, &mi))
            {
                if (b->pos_x >= mi.x && b->pos_x < mi.x + mi.width &&
                    b->pos_y >= mi.y && b->pos_y < mi.y + mi.height)
                {
                    target_monitor = i;
                    break;
                }
            }
        }
        if (target_monitor < 0)
            target_monitor = 0;
    }

    if (cfg->x == WINDOW_POS_CENTERED || cfg->y == WINDOW_POS_CENTERED)
    {
        MonitorInfo m;
        if (backend_monitor_info(target_monitor, &m))
        {
            int cx = cfg->x == WINDOW_POS_CENTERED ? m.x + (m.width - w) / 2 : x;
            int cy = cfg->y == WINDOW_POS_CENTERED ? m.y + (m.height - h) / 2 : y;
            XMoveWindow(g.dpy, b->win, cx, cy);
            b->pos_x = cx;
            b->pos_y = cy;
        }
    }
    if (cfg->vsync && b->render == RENDER_GL)
        backend_set_vsync(b, true);
    if (cfg->mode != WINDOW_WINDOWED)
        backend_set_mode(b, cfg->mode, target_monitor);

    XFlush(g.dpy);
    return b;
}

void backend_destroy(BackendWindow *b)
{
    if (!b)
        return;
    if (b->grabbed || b->captured)
        XUngrabPointer(g.dpy, CurrentTime);
    if (b->xic)
        XDestroyIC(b->xic);
    if (b->image)
        XDestroyImage(b->image);
    if (b->gc)
        XFreeGC(g.dpy, b->gc);
    if (b->render == RENDER_GL)
    {
        /* Leave another window's context current, unless it draws to this window. */
        if (glXGetCurrentContext() == b->glc || glXGetCurrentDrawable() == b->win)
            glXMakeCurrent(g.dpy, None, NULL);
        if (b->glc)
            glXDestroyContext(g.dpy, b->glc);
    }
    XDeleteContext(g.dpy, b->win, g.ctx);
    free(b->pending);
    if (b->hidden_cursor)
        XFreeCursor(g.dpy, b->hidden_cursor);
    XDestroyWindow(g.dpy, b->win);
    XFreeColormap(g.dpy, b->colormap);
    free(b);
}

/* ========================================================================== */
/*  event pump                                                                */
/* ========================================================================== */

static void push(BackendWindow *b, Event *e)
{
    core_push_event(b->core, e);
}

static void handle_key(BackendWindow *b, XEvent *ev, bool down)
{
    KeySym ks = XLookupKeysym(&ev->xkey, 0);
    Event e = {.type = EVENT_KEY};
    int key = translate_keysym(ks);
    e.data.key.key = key;
    e.data.key.scancode = (int)ev->xkey.keycode;
    e.data.key.down = down;
    e.data.key.repeat = down && key > 0 && key < KEY_MAX && b->core->in.key_down[key];
    e.data.key.mods = translate_mods(ev->xkey.state);
    push(b, &e);

    if (!down)
        return;

    /* text: feed the IM and decode UTF-8 into codepoints */
    if (b->xic)
    {
        char buf[64];
        KeySym sym;
        Status status;
        int n = Xutf8LookupString(b->xic, &ev->xkey, buf, sizeof buf - 1, &sym, &status);
        if (n > 0)
        {
            buf[n] = '\0';
            for (const unsigned char *p = (unsigned char *)buf; *p;)
            {
                uint32_t cp;
                int len;
                if (*p < 0x80)
                {
                    cp = *p;
                    len = 1;
                }
                else if ((*p >> 5) == 0x6)
                {
                    cp = *p & 0x1F;
                    len = 2;
                }
                else if ((*p >> 4) == 0xE)
                {
                    cp = *p & 0x0F;
                    len = 3;
                }
                else if ((*p >> 3) == 0x1E)
                {
                    cp = *p & 0x07;
                    len = 4;
                }
                else
                {
                    p++;
                    continue;
                }
                for (int i = 1; i < len && p[i]; i++)
                    cp = (cp << 6) | (p[i] & 0x3F);
                if (cp >= 0x20 && cp != 0x7F)
                    core_push_char(b->core, cp);
                p += len;
            }
        }
    }
}

static void handle_button(BackendWindow *b, XEvent *ev, bool down)
{
    unsigned int btn = ev->xbutton.button;
    if (btn == Button4 || btn == Button5 || btn == 6 || btn == 7)
    {
        if (!down)
            return;
        Event e = {.type = EVENT_MOUSE_WHEEL};
        if (btn == Button4)
            e.data.wheel.y = 1.0f;
        else if (btn == Button5)
            e.data.wheel.y = -1.0f;
        else if (btn == 6)
            e.data.wheel.x = -1.0f;
        else
            e.data.wheel.x = 1.0f;
        push(b, &e);
        return;
    }

    int mb;
    switch (btn)
    {
    case Button1:
        mb = MOUSE_LEFT;
        break;
    case Button2:
        mb = MOUSE_MIDDLE;
        break;
    case Button3:
        mb = MOUSE_RIGHT;
        break;
    case 8:
        mb = MOUSE_X1;
        break;
    case 9:
        mb = MOUSE_X2;
        break;
    default:
        return;
    }
    Event e = {.type = EVENT_MOUSE_BUTTON};
    e.data.mouse.button = mb;
    e.data.mouse.down = down;
    e.data.mouse.x = ev->xbutton.x;
    e.data.mouse.y = ev->xbutton.y;
    push(b, &e);
}

/* ---- hit test: a custom title bar and resize edges ---- */

static unsigned int cursor_shape(int cursor);

static Cursor shape_cursor(int cursor)
{
    if (cursor < 0 || cursor >= CURSOR_COUNT)
        cursor = CURSOR_DEFAULT;
    if (!g.cursors[cursor])
        g.cursors[cursor] = XCreateFontCursor(g.dpy, cursor_shape(cursor));
    return g.cursors[cursor];
}

/* _NET_WM_MOVERESIZE directions, in HitTestResult order after HIT_DRAG. */
static int hit_direction(HitTestResult r)
{
    switch (r)
    {
    case HIT_RESIZE_TOPLEFT: return 0;
    case HIT_RESIZE_TOP: return 1;
    case HIT_RESIZE_TOPRIGHT: return 2;
    case HIT_RESIZE_RIGHT: return 3;
    case HIT_RESIZE_BOTTOMRIGHT: return 4;
    case HIT_RESIZE_BOTTOM: return 5;
    case HIT_RESIZE_BOTTOMLEFT: return 6;
    case HIT_RESIZE_LEFT: return 7;
    default: return 8; /* _NET_WM_MOVERESIZE_MOVE */
    }
}

static int hit_cursor_for(HitTestResult r)
{
    switch (r)
    {
    case HIT_RESIZE_TOPLEFT:
    case HIT_RESIZE_BOTTOMRIGHT: return CURSOR_RESIZE_NWSE;
    case HIT_RESIZE_TOPRIGHT:
    case HIT_RESIZE_BOTTOMLEFT: return CURSOR_RESIZE_NESW;
    case HIT_RESIZE_TOP:
    case HIT_RESIZE_BOTTOM: return CURSOR_RESIZE_NS;
    case HIT_RESIZE_LEFT:
    case HIT_RESIZE_RIGHT: return CURSOR_RESIZE_EW;
    default: return -1;
    }
}

/* Returns true when the press was taken by the window manager. A press that came
   from XSendEvent is consumed too but not forwarded: it is not a real button
   press, and the window manager would wait for a release that never comes. */
static bool hit_press(BackendWindow *b, const XEvent *ev)
{
    if (!b->hit_cb || ev->xbutton.button != Button1)
        return false;
    HitTestResult r = b->hit_cb(b->hit_w, ev->xbutton.x, ev->xbutton.y, b->hit_user);
    if (r == HIT_NORMAL)
        return false;
    b->hit_press = true;
    if (ev->xbutton.send_event)
        return true;

    XUngrabPointer(g.dpy, CurrentTime);
    XEvent m;
    memset(&m, 0, sizeof m);
    m.xclient.type = ClientMessage;
    m.xclient.window = b->win;
    m.xclient.message_type = XInternAtom(g.dpy, "_NET_WM_MOVERESIZE", False);
    m.xclient.format = 32;
    m.xclient.data.l[0] = ev->xbutton.x_root;
    m.xclient.data.l[1] = ev->xbutton.y_root;
    m.xclient.data.l[2] = hit_direction(r);
    m.xclient.data.l[3] = Button1;
    m.xclient.data.l[4] = 1; /* a normal application */
    XSendEvent(g.dpy, g.root, False, SubstructureNotifyMask | SubstructureRedirectMask, &m);
    XFlush(g.dpy);
    return true;
}

/* The pointer over a resize edge shows the matching resize cursor. */
static void hit_motion(BackendWindow *b, int x, int y)
{
    if (!b->hit_cb || b->cursor_mode != MOUSE_MODE_NORMAL)
        return;
    int want = hit_cursor_for(b->hit_cb(b->hit_w, x, y, b->hit_user));
    if (want == b->hit_cursor)
        return;
    b->hit_cursor = want;
    XDefineCursor(g.dpy, b->win, want >= 0 ? shape_cursor(want) : (b->active_cursor ? b->active_cursor : None));
    XFlush(g.dpy);
}

void backend_set_hit_test(BackendWindow *b, PlatformWindow *w, HitTestFunc fn, void *user)
{
    b->hit_cb = fn;
    b->hit_w = w;
    b->hit_user = user;
    if (!fn && b->hit_cursor >= 0)
    {
        b->hit_cursor = -1;
        XDefineCursor(g.dpy, b->win, b->active_cursor ? b->active_cursor : None);
        XFlush(g.dpy);
    }
}

static void process_event(BackendWindow *b, XEvent *ev)
{
    switch (ev->type)
    {
    case ClientMessage:
        if ((Atom)ev->xclient.data.l[0] == g.WM_DELETE_WINDOW)
        {
            Event e = {.type = EVENT_WINDOW_CLOSE};
            push(b, &e);
        }
        break;

    case ConfigureNotify:
    {
        int nw = ev->xconfigure.width, nh = ev->xconfigure.height;
        if (nw != b->width || nh != b->height)
        {
            b->width = nw;
            b->height = nh;
            Event r = {.type = EVENT_WINDOW_RESIZE};
            r.data.resize.w = nw;
            r.data.resize.h = nh;
            push(b, &r);
            Event fbr = {.type = EVENT_WINDOW_FB_RESIZE};
            fbr.data.resize.w = nw; /* X11 has no per-window scaling: fb == size */
            fbr.data.resize.h = nh;
            push(b, &fbr);
        }
        if (ev->xconfigure.x != b->pos_x || ev->xconfigure.y != b->pos_y)
        {
            b->pos_x = ev->xconfigure.x;
            b->pos_y = ev->xconfigure.y;
            Event m = {.type = EVENT_WINDOW_MOVE};
            m.data.move.x = b->pos_x;
            m.data.move.y = b->pos_y;
            push(b, &m);
        }
        break;
    }

    case KeyPress:
        handle_key(b, ev, true);
        break;
    case KeyRelease:
        /* swallow the synthetic release of an auto-repeat pair */
        if (!g.detectable_repeat && XEventsQueued(g.dpy, QueuedAfterReading))
        {
            XEvent next;
            XPeekEvent(g.dpy, &next);
            if (next.type == KeyPress && next.xkey.time == ev->xkey.time &&
                next.xkey.keycode == ev->xkey.keycode)
            {
                XNextEvent(g.dpy, &next);
                Event e = {.type = EVENT_KEY};
                e.data.key.key = translate_keysym(XLookupKeysym(&next.xkey, 0));
                e.data.key.scancode = (int)next.xkey.keycode;
                e.data.key.down = true;
                e.data.key.repeat = true;
                e.data.key.mods = translate_mods(next.xkey.state);
                push(b, &e);
                break;
            }
        }
        handle_key(b, ev, false);
        break;

    case ButtonPress:
        if (hit_press(b, ev))
            break;
        handle_button(b, ev, true);
        break;
    case ButtonRelease:
        if (b->hit_press && ev->xbutton.button == Button1)
        {
            b->hit_press = false; /* the release of a press the window manager took */
            break;
        }
        handle_button(b, ev, false);
        break;

    case MotionNotify:
    {
        hit_motion(b, ev->xmotion.x, ev->xmotion.y);
        Event e = {.type = EVENT_MOUSE_MOVE};
        e.data.mouse.x = ev->xmotion.x;
        e.data.mouse.y = ev->xmotion.y;
        push(b, &e);
        break;
    }

    case EnterNotify:
    case LeaveNotify:
    {
        b->hovered = ev->type == EnterNotify;
        Event e = {.type = EVENT_WINDOW_ENTER};
        e.data.enter.entered = b->hovered;
        push(b, &e);
        break;
    }

    case FocusIn:
    case FocusOut:
    {
        b->focused = ev->type == FocusIn;
        if (b->xic)
            (b->focused && b->text_input ? XSetICFocus : XUnsetICFocus)(b->xic);
        Event e = {.type = EVENT_WINDOW_FOCUS};
        e.data.focus.gained = b->focused;
        push(b, &e);
        break;
    }

    case MapNotify:
        b->visible = true;
        break;
    case UnmapNotify:
        b->visible = false;
        break;
    case PropertyNotify:
        if (ev->xproperty.atom == g.NET_WM_STATE)
            refresh_wm_state(b);
        break;
    }
}

/* ---- clipboard, serving side: we own the CLIPBOARD selection ---- */

static bool clip_is_text_target(Atom t)
{
    return t == g.UTF8_STRING || t == XA_STRING || t == g.TEXT_PLAIN || t == g.TEXT_PLAIN_UTF8 || t == g.TEXT;
}

static const ClipEntry *clip_find_target(Atom target)
{
    for (int i = 0; i < g.clip_count; i++)
    {
        const ClipEntry *e = &g.clip[i];
        if (clip_mime_equal(e->mime, CLIPBOARD_TEXT) ? clip_is_text_target(target) : e->atom == target)
            return e;
    }
    return NULL;
}

/* X errors from a requestor that died mid-transfer must not kill the process. */
static int clip_swallow_error(Display *d, XErrorEvent *e)
{
    (void)d;
    (void)e;
    return 0;
}

static void clip_incr_abort_all(void)
{
    XErrorHandler previous = XSetErrorHandler(clip_swallow_error);
    for (int i = 0; i < CLIP_INCR_MAX; i++)
        if (g.incr[i].active)
        {
            XSelectInput(g.dpy, g.incr[i].requestor, NoEventMask);
            g.incr[i].active = false;
        }
    XSync(g.dpy, False);
    XSetErrorHandler(previous);
}

static void clip_free_entries(void)
{
    clip_incr_abort_all();
    for (int i = 0; i < g.clip_count; i++)
    {
        free(g.clip[i].mime);
        free(g.clip[i].data);
    }
    g.clip_count = 0;
}

/* Send the next chunk of an INCR transfer when the requestor has read the last one
   (it deletes the property). A zero-length chunk ends the transfer. Returns true
   if the event belonged to a transfer. */
static bool clip_incr_property_event(const XPropertyEvent *pe)
{
    for (int i = 0; i < CLIP_INCR_MAX; i++)
    {
        IncrOut *t = &g.incr[i];
        if (!t->active || pe->window != t->requestor || pe->atom != t->property)
            continue;
        if (pe->state == PropertyDelete)
        {
            size_t left = t->size - t->offset;
            size_t n = left > CLIP_CHUNK_MAX ? CLIP_CHUNK_MAX : left;
            XErrorHandler previous = XSetErrorHandler(clip_swallow_error);
            XChangeProperty(g.dpy, t->requestor, t->property, t->target, 8, PropModeReplace,
                            t->data + t->offset, (int)n);
            t->offset += n;
            if (n == 0)
            {
                XSelectInput(g.dpy, t->requestor, NoEventMask);
                t->active = false;
            }
            XSync(g.dpy, False);
            XSetErrorHandler(previous);
        }
        return true;
    }
    return false;
}

/* Answer a request for our CLIPBOARD selection (we are the owner). */
static void answer_selection_request(XSelectionRequestEvent *req)
{
    XSelectionEvent resp = {0};
    resp.type = SelectionNotify;
    resp.display = req->display;
    resp.requestor = req->requestor;
    resp.selection = req->selection;
    resp.target = req->target;
    resp.time = req->time;
    resp.property = None;

    Atom prop = req->property != None ? req->property : req->target; /* old clients */
    XErrorHandler previous = XSetErrorHandler(clip_swallow_error);

    if (req->selection != g.CLIPBOARD)
    {
        /* not ours */
    }
    else if (req->target == g.TARGETS)
    {
        Atom targets[1 + CLIP_MAX * 4];
        int n = 0;
        targets[n++] = g.TARGETS;
        for (int i = 0; i < g.clip_count; i++)
        {
            if (clip_mime_equal(g.clip[i].mime, CLIPBOARD_TEXT))
            {
                targets[n++] = g.UTF8_STRING;
                targets[n++] = g.TEXT_PLAIN_UTF8;
                targets[n++] = g.TEXT_PLAIN;
                targets[n++] = XA_STRING;
            }
            else
                targets[n++] = g.clip[i].atom;
        }
        XChangeProperty(g.dpy, req->requestor, prop, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)targets, n);
        resp.property = prop;
    }
    else
    {
        const ClipEntry *e = clip_find_target(req->target);
        if (e && e->size <= CLIP_CHUNK_MAX)
        {
            XChangeProperty(g.dpy, req->requestor, prop, req->target, 8, PropModeReplace,
                            e->data, (int)e->size);
            resp.property = prop;
        }
        else if (e)
        {
            /* Too big for one property: announce INCR and send chunks as the requestor
               consumes them (clip_incr_property_event). */
            for (int i = 0; i < CLIP_INCR_MAX; i++)
            {
                if (g.incr[i].active)
                    continue;
                g.incr[i] = (IncrOut){true, req->requestor, prop, req->target, e->data, e->size, 0};
                XSelectInput(g.dpy, req->requestor, PropertyChangeMask);
                long total = (long)e->size;
                XChangeProperty(g.dpy, req->requestor, prop, g.INCR, 32, PropModeReplace,
                                (unsigned char *)&total, 1);
                resp.property = prop;
                break;
            }
        }
    }
    XSendEvent(g.dpy, req->requestor, True, NoEventMask, (XEvent *)&resp);
    XSync(g.dpy, False);
    XSetErrorHandler(previous);
}

/* A queued event for a window: an Xlib event or a finger from XInput 2. */
typedef struct PendingX
{
    bool is_touch;
    XEvent ev;
    TouchPhase phase;
    int touch_id;
    float x, y;
} PendingX;

/* All windows share one X connection and one event queue, so a window's pump meets
   events that belong to the others. Each such event is kept on its owner's pending
   list and processed in the owner's own pump: that keeps a window's per-frame input
   (edges, the frame's event list) in step with its own begin_frame. */
#define PENDING_MAX 4096

static void defer_item(BackendWindow *owner, const PendingX *item)
{
    if (owner->pending_n == owner->pending_cap)
    {
        if (owner->pending_cap >= PENDING_MAX)
            return; /* a window nobody pumps: drop rather than grow without bound */
        int cap = owner->pending_cap ? owner->pending_cap * 2 : 64;
        PendingX *grown = realloc(owner->pending, (size_t)cap * sizeof *grown);
        if (!grown)
            return;
        owner->pending = grown;
        owner->pending_cap = cap;
    }
    owner->pending[owner->pending_n++] = *item;
}

static void defer_event(BackendWindow *owner, const XEvent *ev)
{
    PendingX item;
    memset(&item, 0, sizeof item);
    item.ev = *ev;
    defer_item(owner, &item);
}

static void process_item(BackendWindow *b, const PendingX *it)
{
    if (it->is_touch)
        core_push_touch(b->core, it->phase, it->touch_id, it->x, it->y, it->phase == TOUCH_UP ? 0.0f : 1.0f);
    else
    {
        XEvent ev = it->ev; /* process_event may touch it */
        process_event(b, &ev);
    }
}

#if ZEN_HAVE_XI2
/* One XInput 2 touch event: raise it for the window it landed on. */
static void handle_xi_touch(BackendWindow *b, const XIDeviceEvent *de)
{
    PendingX it;
    memset(&it, 0, sizeof it);
    it.is_touch = true;
    it.touch_id = (int)de->detail;
    it.x = (float)de->event_x;
    it.y = (float)de->event_y;
    it.phase = de->evtype == XI_TouchBegin ? TOUCH_DOWN : de->evtype == XI_TouchUpdate ? TOUCH_MOVE : TOUCH_UP;

    if (de->event == b->win)
    {
        process_item(b, &it);
        return;
    }
    XPointer found = NULL;
    if (XFindContext(g.dpy, de->event, g.ctx, &found) == 0 && found)
        defer_item((BackendWindow *)found, &it);
}
#endif

void backend_pump_events(BackendWindow *b, Core *core)
{
    b->core = core;

    /* Older than anything still in the queue, so first. */
    for (int i = 0; i < b->pending_n; i++)
        process_item(b, &b->pending[i]);
    b->pending_n = 0;
    flush_preedit(b);

    while (XPending(g.dpy))
    {
        XEvent ev;
        XNextEvent(g.dpy, &ev);
        if (XFilterEvent(&ev, None))
            continue;
        if (ev.type == SelectionRequest)
        {
            answer_selection_request(&ev.xselectionrequest);
            continue;
        }
        if (ev.type == SelectionClear)
        {
            if (ev.xselectionclear.selection == g.CLIPBOARD)
                clip_free_entries();
            continue;
        }
        if (ev.type == PropertyNotify && clip_incr_property_event(&ev.xproperty))
            continue;
#if ZEN_HAVE_XI2
        if (ev.type == GenericEvent && g.xi_touch && ev.xcookie.extension == g.xi_opcode)
        {
            if (XGetEventData(g.dpy, &ev.xcookie))
            {
                const XIDeviceEvent *de = ev.xcookie.data;
                if (de->evtype == XI_TouchBegin || de->evtype == XI_TouchUpdate || de->evtype == XI_TouchEnd)
                    handle_xi_touch(b, de);
                XFreeEventData(g.dpy, &ev.xcookie);
            }
            continue;
        }
#endif

        if (ev.xany.window == b->win)
        {
            process_event(b, &ev);
            continue;
        }
        XPointer found = NULL;
        if (XFindContext(g.dpy, ev.xany.window, g.ctx, &found) == 0 && found)
            defer_event((BackendWindow *)found, &ev);
        /* anything else (the clipboard helper, a window already destroyed) is dropped */
    }
    flush_preedit(b); /* the input method may have changed the composition while filtering */
}


/* ========================================================================== */
/*  native handles and Vulkan                                                 */
/* ========================================================================== */

void *backend_native_handle(BackendWindow *b, NativeHandleType type)
{
    switch (type)
    {
    case NATIVE_DISPLAY:
        return g.dpy;
    case NATIVE_WINDOW:
        return (void *)(uintptr_t)b->win;
    case NATIVE_GL_CONTEXT:
        return b->glc;
    }
    return NULL;
}

const char *const *backend_vulkan_extensions(uint32_t *count)
{
    static const char *const ext[] = {"VK_KHR_surface", "VK_KHR_xlib_surface"};
    *count = 2;
    return ext;
}

typedef struct
{
    int sType;
    const void *pNext;
    uint32_t flags;
    Display *dpy;
    Window window;
} ZenVkXlibSurfaceCreateInfo;

bool backend_vulkan_create_surface(BackendWindow *b, void *instance, const void *allocator, uint64_t *out_surface)
{
    ZenVkXlibSurfaceCreateInfo info = {
        .sType = ZEN_VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = g.dpy,
        .window = b->win,
    };
    return vulkan_call_create_surface(instance, "vkCreateXlibSurfaceKHR", &info, allocator, out_surface);
}

void backend_swap(BackendWindow *b)
{
    if (b->render == RENDER_GL)
        glXSwapBuffers(g.dpy, b->win);
}

/* ========================================================================== */
/*  GL context glue                                                           */
/* ========================================================================== */

void backend_make_current(BackendWindow *b)
{
    if (b->render == RENDER_GL)
        glXMakeCurrent(g.dpy, b->win, b->glc);
}

void backend_make_current_on(BackendWindow *target, BackendWindow *context)
{
    if (target->render == RENDER_GL && context->render == RENDER_GL)
        glXMakeCurrent(g.dpy, target->win, context->glc);
}

void backend_set_vsync(BackendWindow *b, bool on)
{
    int interval = on ? 1 : 0;
    if (g.swap_ext)
        g.swap_ext(g.dpy, b->win, interval);
    else if (g.swap_mesa)
        g.swap_mesa((unsigned)interval);
    else if (g.swap_sgi)
        g.swap_sgi(interval);
}

void *backend_gl_proc_address(const char *name)
{
    return (void *)glXGetProcAddressARB((const GLubyte *)name);
}

/* ========================================================================== */
/*  pixel surface (XImage, 32-bit)                                            */
/* ========================================================================== */

bool backend_lock_pixels(BackendWindow *b, Framebuffer *out)
{
    if (b->render != RENDER_PIXELS || !out)
        return false;

    if (!b->image || b->image->width != b->width || b->image->height != b->height)
    {
        if (b->image)
            XDestroyImage(b->image);
        int depth = DefaultDepth(g.dpy, g.screen);
        Visual *visual = DefaultVisual(g.dpy, g.screen);
        uint32_t *data = malloc((size_t)b->width * b->height * 4);
        if (!data)
            return false;
        b->image = XCreateImage(g.dpy, visual, depth, ZPixmap, 0, (char *)data,
                                b->width, b->height, 32, 0);
        if (!b->image)
        {
            free(data);
            return false;
        }
    }

    out->pixels = (uint32_t *)b->image->data;
    out->width = b->image->width;
    out->height = b->image->height;
    out->stride = b->image->bytes_per_line / 4;
    return true;
}

void backend_present_pixels(BackendWindow *b)
{
    if (b->render != RENDER_PIXELS || !b->image)
        return;
    XPutImage(g.dpy, b->win, b->gc, b->image, 0, 0, 0, 0,
              (unsigned)b->image->width, (unsigned)b->image->height);
    XFlush(g.dpy);
}

/* ========================================================================== */
/*  geometry                                                                  */
/* ========================================================================== */

void backend_get_size(BackendWindow *b, int *w, int *h)
{
    if (w)
        *w = b->width;
    if (h)
        *h = b->height;
}

void backend_set_size(BackendWindow *b, int w, int h)
{
    XResizeWindow(g.dpy, b->win, (unsigned)w, (unsigned)h);
}

void backend_get_fb_size(BackendWindow *b, int *w, int *h)
{
    if (w)
        *w = b->width; /* no per-window scaling on X11 */
    if (h)
        *h = b->height;
}

void backend_get_pos(BackendWindow *b, int *x, int *y)
{
    Window child;
    int rx, ry;
    XTranslateCoordinates(g.dpy, b->win, g.root, 0, 0, &rx, &ry, &child);
    if (x)
        *x = rx;
    if (y)
        *y = ry;
}

void backend_set_pos(BackendWindow *b, int x, int y)
{
    XMoveWindow(g.dpy, b->win, x, y);
}

float backend_content_scale(BackendWindow *b)
{
    (void)b;
    return 1.0f;
}

void backend_set_title(BackendWindow *b, const char *title)
{
    set_title(b, title);
}

void backend_set_size_limits(BackendWindow *b, int minw, int minh, int maxw, int maxh)
{
    XSizeHints *hints = XAllocSizeHints();
    if (!hints)
        return;

    hints->flags = 0;

    if (minw > 0 || minh > 0)
    {
        hints->flags |= PMinSize;
        hints->min_width = minw > 0 ? minw : 1;
        hints->min_height = minh > 0 ? minh : 1;
    }

    if (maxw > 0 || maxh > 0)
    {
        hints->flags |= PMaxSize;
        hints->max_width = maxw > 0 ? maxw : 32767;
        hints->max_height = maxh > 0 ? maxh : 32767;
    }

    XSetWMNormalHints(g.dpy, b->win, hints);
    XFree(hints);
}

/* ========================================================================== */
/*  window state                                                              */
/* ========================================================================== */

void backend_minimize(BackendWindow *b)
{
    XIconifyWindow(g.dpy, b->win, g.screen);
}

void backend_maximize(BackendWindow *b)
{
    send_wm_state(b, 1, g.NET_WM_STATE_MAXIMIZED_VERT, g.NET_WM_STATE_MAXIMIZED_HORZ);
}

void backend_restore(BackendWindow *b)
{
    send_wm_state(b, 0, g.NET_WM_STATE_MAXIMIZED_VERT, g.NET_WM_STATE_MAXIMIZED_HORZ);
    XMapWindow(g.dpy, b->win);
}

void backend_show(BackendWindow *b)
{
    XMapWindow(g.dpy, b->win);
}
void backend_hide(BackendWindow *b)
{
    XUnmapWindow(g.dpy, b->win);
}

void backend_focus(BackendWindow *b)
{
    XRaiseWindow(g.dpy, b->win);
    XSetInputFocus(g.dpy, b->win, RevertToParent, CurrentTime);
}

void backend_request_attention(BackendWindow *b)
{
    send_wm_state(b, 1, g.NET_WM_STATE_DEMANDS_ATTENTION, 0);
}

bool backend_get_flag(BackendWindow *b, int flag)
{
    switch (flag)
    {
    case WIN_FLAG_FOCUSED:
        return b->focused;
    case WIN_FLAG_MINIMIZED:
        return b->minimized;
    case WIN_FLAG_MAXIMIZED:
        return b->maximized;
    case WIN_FLAG_VISIBLE:
        return b->visible;
    case WIN_FLAG_HOVERED:
        return b->hovered;
    default:
        return false;
    }
}

void backend_set_mode(BackendWindow *b, WindowMode mode, int monitor)
{
    bool fs = mode != WINDOW_WINDOWED;

    if (fs && monitor >= 0)
    {
        /* Move to the target monitor before going fullscreen so the WM
           fullscreens on the correct head. */
        MonitorInfo m;
        if (backend_monitor_info(monitor, &m))
        {
            /* Place it roughly centred so it's clearly on that monitor. */
            int cx = m.x + (m.width - b->width) / 2;
            int cy = m.y + (m.height - b->height) / 2;
            XMoveWindow(g.dpy, b->win, cx, cy);
            XFlush(g.dpy);
        }
    }

    send_wm_state(b, fs ? 1 : 0, g.NET_WM_STATE_FULLSCREEN, 0);
    b->mode = mode;
}

WindowMode backend_get_mode(BackendWindow *b)
{
    return b->mode;
}

void backend_set_icon(BackendWindow *b, int w, int h, const uint8_t *rgba)
{
    if (!rgba || w <= 0 || h <= 0)
    {
        XDeleteProperty(g.dpy, b->win, g.NET_WM_ICON);
        return;
    }
    /* _NET_WM_ICON is width, height, then w*h ARGB longs. */
    int n = 2 + w * h;
    long *buf = malloc((size_t)n * sizeof(long));
    if (!buf)
        return;
    buf[0] = w;
    buf[1] = h;
    for (int i = 0; i < w * h; i++)
    {
        const uint8_t *p = rgba + i * 4;
        buf[2 + i] = ((long)p[3] << 24) | ((long)p[0] << 16) | ((long)p[1] << 8) | (long)p[2];
    }
    XChangeProperty(g.dpy, b->win, g.NET_WM_ICON, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)buf, n);
    free(buf);
}

void backend_set_opacity(BackendWindow *b, float a)
{
    if (a < 0.0f)
        a = 0.0f;
    if (a > 1.0f)
        a = 1.0f;
    uint32_t value = (uint32_t)((double)a * 4294967295.0);
    XChangeProperty(g.dpy, b->win, g.NET_WM_WINDOW_OPACITY, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&value, 1);
}

void backend_set_always_on_top(BackendWindow *b, bool on)
{
    send_wm_state(b, on ? 1 : 0, g.NET_WM_STATE_ABOVE, 0);
}

/* ========================================================================== */
/*  mouse                                                                     */
/* ========================================================================== */

void backend_set_mouse_pos(BackendWindow *b, int x, int y)
{
    XWarpPointer(g.dpy, None, b->win, 0, 0, 0, 0, x, y);
    XFlush(g.dpy);
}

static Cursor make_hidden_cursor(BackendWindow *b)
{
    if (b->hidden_cursor)
        return b->hidden_cursor;
    char data[1] = {0};
    Pixmap pm = XCreateBitmapFromData(g.dpy, b->win, data, 1, 1);
    XColor c = {0};
    b->hidden_cursor = XCreatePixmapCursor(g.dpy, pm, pm, &c, &c, 0, 0);
    XFreePixmap(g.dpy, pm);
    return b->hidden_cursor;
}

static unsigned int cursor_shape(int cursor)
{
    switch (cursor)
    {
    case CURSOR_IBEAM:
        return XC_xterm;
    case CURSOR_CROSSHAIR:
        return XC_crosshair;
    case CURSOR_HAND:
        return XC_hand2;
    case CURSOR_RESIZE_EW:
        return XC_sb_h_double_arrow;
    case CURSOR_RESIZE_NS:
        return XC_sb_v_double_arrow;
    case CURSOR_RESIZE_NWSE:
        return XC_top_left_corner;
    case CURSOR_RESIZE_NESW:
        return XC_top_right_corner;
    case CURSOR_RESIZE_ALL:
        return XC_fleur;
    case CURSOR_NOT_ALLOWED:
        return XC_X_cursor;
    default:
        return XC_left_ptr;
    }
}

void backend_set_cursor(BackendWindow *b, int cursor)
{
    if (cursor < 0 || cursor >= CURSOR_COUNT)
        cursor = CURSOR_DEFAULT;
    b->cursor_idx = cursor;
    b->active_cursor = shape_cursor(cursor);
    if (b->cursor_mode == MOUSE_MODE_NORMAL && b->hit_cursor < 0)
    {
        XDefineCursor(g.dpy, b->win, b->active_cursor);
        XFlush(g.dpy);
    }
}

/* ---- cursors from an image (libXcursor, found at run time) ---- */

struct PlatformCursor
{
    Cursor cursor;
};

typedef struct
{
    uint32_t version, size, width, height, xhot, yhot, delay;
    uint32_t *pixels;
} ZenXcursorImage;

static struct
{
    bool tried;
    ZenXcursorImage *(*image_create)(int, int);
    void (*image_destroy)(ZenXcursorImage *);
    Cursor (*image_load)(Display *, const ZenXcursorImage *);
} g_xcursor;

static bool xcursor_load(void)
{
    if (!g_xcursor.tried)
    {
        g_xcursor.tried = true;
        SharedLibrary *lib = library_open("libXcursor.so.1");
        if (lib)
        {
            *(void **)&g_xcursor.image_create = library_symbol(lib, "XcursorImageCreate");
            *(void **)&g_xcursor.image_destroy = library_symbol(lib, "XcursorImageDestroy");
            *(void **)&g_xcursor.image_load = library_symbol(lib, "XcursorImageLoadCursor");
        }
    }
    return g_xcursor.image_create && g_xcursor.image_destroy && g_xcursor.image_load;
}

PlatformCursor *backend_cursor_create(const uint32_t *argb, int w, int h, int hot_x, int hot_y)
{
    if (!xcursor_load())
    {
        error_set("cursors from an image need libXcursor, which is not installed");
        return NULL;
    }
    ZenXcursorImage *img = g_xcursor.image_create(w, h);
    if (!img)
        return NULL;
    img->xhot = (uint32_t)hot_x;
    img->yhot = (uint32_t)hot_y;
    for (int i = 0; i < w * h; i++) /* Xcursor wants premultiplied alpha */
    {
        uint32_t p = argb[i], a = p >> 24;
        uint32_t r = ((p >> 16) & 0xFF) * a / 255, gr = ((p >> 8) & 0xFF) * a / 255, bl = (p & 0xFF) * a / 255;
        img->pixels[i] = (a << 24) | (r << 16) | (gr << 8) | bl;
    }
    Cursor c = g_xcursor.image_load(g.dpy, img);
    g_xcursor.image_destroy(img);
    if (!c)
    {
        error_set("cannot create the cursor");
        return NULL;
    }
    PlatformCursor *pc = malloc(sizeof *pc);
    if (!pc)
    {
        XFreeCursor(g.dpy, c);
        return NULL;
    }
    pc->cursor = c;
    return pc;
}

void backend_cursor_destroy(PlatformCursor *c)
{
    XFreeCursor(g.dpy, c->cursor);
    free(c);
}

void backend_set_cursor_image(BackendWindow *b, PlatformCursor *c)
{
    if (!c)
    {
        backend_set_cursor(b, b->cursor_idx);
        return;
    }
    b->active_cursor = c->cursor;
    if (b->cursor_mode == MOUSE_MODE_NORMAL && b->hit_cursor < 0)
    {
        XDefineCursor(g.dpy, b->win, b->active_cursor);
        XFlush(g.dpy);
    }
}

void backend_set_mouse_mode(BackendWindow *b, int mode)
{
    b->cursor_mode = mode;
    if (mode == MOUSE_MODE_NORMAL)
    {
        if (b->grabbed)
        {
            if (!b->captured)
                XUngrabPointer(g.dpy, CurrentTime);
            b->grabbed = false;
        }
        XDefineCursor(g.dpy, b->win, b->active_cursor ? b->active_cursor : None);
    }
    else
    {
        XDefineCursor(g.dpy, b->win, make_hidden_cursor(b));
        if (mode == MOUSE_MODE_CAPTURED && !b->grabbed)
        {
            XGrabPointer(g.dpy, b->win, True,
                         ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                         GrabModeAsync, GrabModeAsync, b->win, None, CurrentTime);
            b->grabbed = true;
        }
    }
    XFlush(g.dpy);
}

void backend_set_live_callback(BackendWindow *b, PlatformWindow *w, FrameCallback cb, void *user)
{
    (void)b, (void)w, (void)cb, (void)user; /* nothing blocks the loop on X11 */
}

int backend_lock_state(void)
{
    XkbStateRec st;
    if (XkbGetState(g.dpy, XkbUseCoreKbd, &st) != Success)
        return 0;
    return ((st.locked_mods & LockMask) ? KEYMOD_CAPS_LOCK : 0) | ((st.locked_mods & Mod2Mask) ? KEYMOD_NUM_LOCK : 0);
}

bool backend_mouse_capture(BackendWindow *b, bool on)
{
    if (on)
    {
        if (b->grabbed || b->captured)
        {
            b->captured = true;
            return true;
        }
        int r = XGrabPointer(g.dpy, b->win, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                             GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        b->captured = r == GrabSuccess;
        XFlush(g.dpy);
        return b->captured;
    }
    if (b->captured && !b->grabbed)
        XUngrabPointer(g.dpy, CurrentTime);
    b->captured = false;
    XFlush(g.dpy);
    return false;
}

/* ========================================================================== */
/*  clipboard (CLIPBOARD selection)                                           */
/* ========================================================================== */

bool backend_clipboard_set(const ClipboardItem *items, int count)
{
    clip_free_entries();
    if (count > CLIP_MAX)
        count = CLIP_MAX;
    for (int i = 0; i < count; i++)
    {
        ClipEntry *e = &g.clip[g.clip_count];
        e->mime = strdup(items[i].mime);
        e->data = malloc(items[i].size + 1);
        if (!e->mime || !e->data)
        {
            free(e->mime);
            free(e->data);
            clip_free_entries();
            return error_set("out of memory");
        }
        e->atom = XInternAtom(g.dpy, items[i].mime, False);
        if (items[i].size)
            memcpy(e->data, items[i].data, items[i].size);
        e->data[items[i].size] = 0;
        e->size = items[i].size;
        g.clip_count++;
    }

    if (g.clip_count == 0)
    {
        if (XGetSelectionOwner(g.dpy, g.CLIPBOARD) == g.helper)
            XSetSelectionOwner(g.dpy, g.CLIPBOARD, None, CurrentTime);
        XFlush(g.dpy);
        return true;
    }
    XSetSelectionOwner(g.dpy, g.CLIPBOARD, g.helper, CurrentTime);
    XFlush(g.dpy);
    if (XGetSelectionOwner(g.dpy, g.CLIPBOARD) != g.helper)
    {
        clip_free_entries();
        return error_set("cannot take ownership of the clipboard");
    }
    return true;
}

static const ClipEntry *clip_find_mime(const char *mime)
{
    for (int i = 0; i < g.clip_count; i++)
        if (clip_mime_equal(g.clip[i].mime, mime))
            return &g.clip[i];
    return NULL;
}

static bool clip_owned(void)
{
    return XGetSelectionOwner(g.dpy, g.CLIPBOARD) == g.helper;
}

/* Wait for an event of `type` on the helper window, up to the timeout. */
static bool clip_wait_event(int type, XEvent *ev)
{
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;)
    {
        XFlush(g.dpy);
        if (XCheckTypedWindowEvent(g.dpy, g.helper, type, ev))
            return true;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000;
        if (ms >= CLIP_TIMEOUT_MS)
            return false;
        int fd = ConnectionNumber(g.dpy);
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv = {0, 5000};
        select(fd + 1, &fds, NULL, NULL, &tv);
        XPending(g.dpy); /* moves what arrived into the queue */
    }
}

/* Read XSEL_DATA off the helper window (deleting it). Bytes come back in a malloc'd
   buffer with a NUL after them; *type and *format describe the property. Format 32
   items are Xlib longs in memory, so a list of atoms is size / sizeof(long) items. */
static bool clip_read_property(Atom *type, int *format, uint8_t **out, size_t *size)
{
    unsigned long n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(g.dpy, g.helper, g.XSEL_DATA, 0, (long)(CLIP_READ_LIMIT / 4), True,
                           AnyPropertyType, type, format, &n, &after, &data) != Success)
        return false;
    if (after > 0 || *type == None)
    {
        if (data)
            XFree(data);
        return false;
    }
    size_t unit = *format == 32 ? sizeof(long) : (size_t)*format / 8;
    size_t bytes = (size_t)n * unit;
    *out = malloc(bytes + 1);
    if (*out)
    {
        if (bytes)
            memcpy(*out, data, bytes);
        (*out)[bytes] = 0;
        *size = bytes;
    }
    if (data)
        XFree(data);
    return *out != NULL;
}

/* Ask the owner for `target` and collect the answer, following INCR. */
static bool clip_fetch(Atom target, Atom *type, uint8_t **out, size_t *size)
{
    XEvent ev;
    while (XCheckTypedWindowEvent(g.dpy, g.helper, SelectionNotify, &ev))
    {
    }
    while (XCheckTypedWindowEvent(g.dpy, g.helper, PropertyNotify, &ev))
    {
    }
    XDeleteProperty(g.dpy, g.helper, g.XSEL_DATA);
    XConvertSelection(g.dpy, g.CLIPBOARD, target, g.XSEL_DATA, g.helper, CurrentTime);

    if (!clip_wait_event(SelectionNotify, &ev) || ev.xselection.property == None)
        return false;

    int format;
    if (!clip_read_property(type, &format, out, size))
        return false;
    if (*type != g.INCR)
        return true;

    /* INCR: the property we just read (and so deleted) announced the total size. The
       owner now sends chunks, each signalled by a NewValue on the property; an empty
       chunk ends it. */
    free(*out);
    *out = NULL;
    *size = 0;
    uint8_t *all = malloc(1);
    size_t have = 0;
    if (!all)
        return false;
    for (;;)
    {
        do
        {
            if (!clip_wait_event(PropertyNotify, &ev))
            {
                free(all);
                return false;
            }
        } while (ev.xproperty.atom != g.XSEL_DATA || ev.xproperty.state != PropertyNewValue);

        /* The owner's own announcement of INCR also raised a NewValue that is still in
           the queue: by now the property is gone, which tells it from a real chunk (a
           chunk, even the empty last one, has a type). Skip it and keep waiting. */
        {
            Atom probe_type;
            int probe_format;
            unsigned long probe_n, probe_after;
            unsigned char *probe = NULL;
            XGetWindowProperty(g.dpy, g.helper, g.XSEL_DATA, 0, 0, False, AnyPropertyType, &probe_type,
                               &probe_format, &probe_n, &probe_after, &probe);
            if (probe)
                XFree(probe);
            if (probe_type == None)
                continue;
        }
        Atom chunk_type;
        uint8_t *chunk = NULL;
        size_t n = 0;
        if (!clip_read_property(&chunk_type, &format, &chunk, &n))
        {
            free(all);
            return false;
        }
        if (n == 0)
        {
            free(chunk);
            break;
        }
        if (have + n > CLIP_READ_LIMIT)
        {
            free(chunk);
            free(all);
            return false;
        }
        uint8_t *grown = realloc(all, have + n + 1);
        if (!grown)
        {
            free(chunk);
            free(all);
            return false;
        }
        all = grown;
        memcpy(all + have, chunk, n);
        have += n;
        free(chunk);
    }
    all[have] = 0;
    *out = all;
    *size = have;
    *type = XA_STRING; /* the real type is the one we asked for; callers know it */
    return true;
}

bool backend_clipboard_has(const char *mime)
{
    if (clip_owned())
        return clip_find_mime(mime) != NULL;
    if (XGetSelectionOwner(g.dpy, g.CLIPBOARD) == None)
        return false;

    Atom type;
    uint8_t *list = NULL;
    size_t size = 0;
    if (!clip_fetch(g.TARGETS, &type, &list, &size))
        return false;
    bool text = clip_mime_equal(mime, CLIPBOARD_TEXT);
    Atom want = text ? None : XInternAtom(g.dpy, mime, False);
    bool found = false;
    for (size_t i = 0; i + sizeof(Atom) <= size && !found; i += sizeof(Atom))
    {
        Atom a;
        memcpy(&a, list + i, sizeof a);
        found = text ? clip_is_text_target(a) : a == want;
    }
    free(list);
    return found;
}

void *backend_clipboard_get(const char *mime, size_t *size)
{
    if (clip_owned())
    {
        const ClipEntry *e = clip_find_mime(mime);
        if (!e)
            return NULL;
        uint8_t *copy = malloc(e->size + 1);
        if (!copy)
            return NULL;
        memcpy(copy, e->data, e->size + 1);
        *size = e->size;
        return copy;
    }
    if (XGetSelectionOwner(g.dpy, g.CLIPBOARD) == None)
        return NULL;

    Atom type;
    uint8_t *data = NULL;
    size_t n = 0;
    if (clip_mime_equal(mime, CLIPBOARD_TEXT))
    {
        if (!clip_fetch(g.UTF8_STRING, &type, &data, &n))
        {
            /* Latin-1 only owners: STRING, widened to UTF-8. */
            uint8_t *latin = NULL;
            size_t ln = 0;
            if (!clip_fetch(XA_STRING, &type, &latin, &ln))
                return NULL;
            data = malloc(ln * 2 + 1);
            if (!data)
            {
                free(latin);
                return NULL;
            }
            for (size_t i = 0; i < ln; i++)
            {
                if (latin[i] < 0x80)
                    data[n++] = latin[i];
                else
                {
                    data[n++] = (uint8_t)(0xC0 | (latin[i] >> 6));
                    data[n++] = (uint8_t)(0x80 | (latin[i] & 0x3F));
                }
            }
            data[n] = 0;
            free(latin);
        }
    }
    else if (!clip_fetch(XInternAtom(g.dpy, mime, False), &type, &data, &n))
        return NULL;
    *size = n;
    return data;
}

/* ========================================================================== */
/*  monitors (XRandR, one virtual coordinate space)                           */
/* ========================================================================== */

bool backend_mouse_global_position(int *x, int *y)
{
    Window root, child;
    int win_x, win_y;
    unsigned int mask;
    return XQueryPointer(g.dpy, g.root, &root, &child, x, y, &win_x, &win_y, &mask) == True;
}

int backend_monitor_count(void)
{
    int n = 0;
    XRRMonitorInfo *m = XRRGetMonitors(g.dpy, g.root, True, &n);
    if (m)
        XRRFreeMonitors(m);
    return n > 0 ? n : 1;
}

bool backend_monitor_info(int index, MonitorInfo *out)
{
    if (!out)
        return false;
    int n = 0;
    XRRMonitorInfo *m = XRRGetMonitors(g.dpy, g.root, True, &n);
    if (!m || index < 0 || index >= n)
    {
        if (m)
            XRRFreeMonitors(m);
        return false;
    }
    XRRMonitorInfo *mi = &m[index];
    memset(out, 0, sizeof *out);
    out->index = index;
    out->x = mi->x;
    out->y = mi->y;
    out->width = mi->width;
    out->height = mi->height;
    out->work_x = mi->x;
    out->work_y = mi->y;
    out->work_w = mi->width;
    out->work_h = mi->height;
    out->phys_width_mm = mi->mwidth;
    out->phys_height_mm = mi->mheight;
    out->refresh_hz = 60;
    /* content_scale stays 1.0: X11 has no logical/pixel split, fb == window size */
    out->content_scale = 1.0f;
    out->primary = mi->primary != 0;

    /* Refresh from the CRTC mode of the monitor's first output. */
    if (mi->noutput > 0)
    {
        XRRScreenResources *res = XRRGetScreenResourcesCurrent(g.dpy, g.root);
        if (res)
        {
            XRROutputInfo *oi = XRRGetOutputInfo(g.dpy, res, mi->outputs[0]);
            if (oi && oi->crtc)
            {
                XRRCrtcInfo *ci = XRRGetCrtcInfo(g.dpy, res, oi->crtc);
                if (ci)
                {
                    for (int i = 0; i < res->nmode; i++)
                        if (res->modes[i].id == ci->mode)
                        {
                            XRRModeInfo *mode = &res->modes[i];
                            unsigned long total = (unsigned long)mode->hTotal * mode->vTotal;
                            if (total)
                                out->refresh_hz = (int)((double)mode->dotClock / total + 0.5);
                            break;
                        }
                    XRRFreeCrtcInfo(ci);
                }
            }
            if (oi)
                XRRFreeOutputInfo(oi);
            XRRFreeScreenResources(res);
        }
    }
    char *name = XGetAtomName(g.dpy, mi->name);
    static char namebuf[64];
    snprintf(namebuf, sizeof namebuf, "%s", name ? name : "monitor");
    if (name)
        XFree(name);
    out->name = namebuf;
    XRRFreeMonitors(m);
    return true;
}

/* ========================================================================== */
/*  loop                                                                      */
/* ========================================================================== */

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
