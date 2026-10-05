/*
 * backend_win32.c - Win32 + WGL backend. Creates the window and GL context, pumps
 * the message queue and normalizes everything into core_push_event / core_push_char.
 * Keys are translated from the hardware scancode, so a physical key keeps its
 * meaning on any keyboard layout. All sizes are pixels: the process is DPI aware
 * and content_scale is always 1.
 */
#include "core_internal.h"
#include "backend.h"
#include "win32_util.h"
#include "error_internal.h"
#include "vulkan_internal.h"
#include "png_internal.h"
#include "mime_util.h"

#include <windowsx.h>
#include <shellapi.h>
#include <GL/gl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WGL_DRAW_TO_WINDOW_ARB 0x2001
#define WGL_ACCELERATION_ARB 0x2003
#define WGL_SUPPORT_OPENGL_ARB 0x2010
#define WGL_DOUBLE_BUFFER_ARB 0x2011
#define WGL_PIXEL_TYPE_ARB 0x2013
#define WGL_RED_BITS_ARB 0x2015
#define WGL_GREEN_BITS_ARB 0x2017
#define WGL_BLUE_BITS_ARB 0x2019
#define WGL_ALPHA_BITS_ARB 0x201B
#define WGL_DEPTH_BITS_ARB 0x2022
#define WGL_STENCIL_BITS_ARB 0x2023
#define WGL_FULL_ACCELERATION_ARB 0x2027
#define WGL_TYPE_RGBA_ARB 0x202B
#define WGL_SAMPLE_BUFFERS_ARB 0x2041
#define WGL_SAMPLES_ARB 0x2042
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_DEBUG_BIT_ARB 0x00000001
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#define WGL_CONTEXT_ES2_PROFILE_BIT_EXT 0x00000004

#define WINDOW_CLASS L"zen_platform_window"
#define CURSOR_ID(n) MAKEINTRESOURCEW(n)
#define ZEN_IDC_ARROW CURSOR_ID(32512)
#define ZEN_IDC_IBEAM CURSOR_ID(32513)
#define ZEN_IDC_CROSS CURSOR_ID(32515)
#define ZEN_IDC_SIZENWSE CURSOR_ID(32642)
#define ZEN_IDC_SIZENESW CURSOR_ID(32643)
#define ZEN_IDC_SIZEWE CURSOR_ID(32644)
#define ZEN_IDC_SIZENS CURSOR_ID(32645)
#define ZEN_IDC_SIZEALL CURSOR_ID(32646)
#define ZEN_IDC_NO CURSOR_ID(32648)
#define ZEN_IDC_HAND CURSOR_ID(32649)
#define MAX_MONITORS 16
#define MONITOR_NAME_CAP 64
#define PENDING_MAX 256
#define LIVE_TIMER_ID 1 /* the timer that keeps the live callback going in a modal loop */

typedef BOOL(WINAPI *PFN_wglChoosePixelFormatARB)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
typedef HGLRC(WINAPI *PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
typedef BOOL(WINAPI *PFN_wglSwapIntervalEXT)(int);
typedef const char *(WINAPI *PFN_wglGetExtensionsStringARB)(HDC);
typedef BOOL(WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
typedef UINT(WINAPI *PFN_GetDpiForWindow)(HWND);
typedef UINT(WINAPI *PFN_GetDpiForSystem)(void);
typedef BOOL(WINAPI *PFN_AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);

static struct
{
    HINSTANCE instance;
    bool class_registered;
    bool wgl_loaded;
    PFN_wglChoosePixelFormatARB choose_pixel_format;
    PFN_wglCreateContextAttribsARB create_context;
    PFN_wglSwapIntervalEXT swap_interval;
    bool has_es_profile;
    PFN_GetDpiForWindow get_dpi_for_window;
    PFN_GetDpiForSystem get_dpi_for_system;
    PFN_AdjustWindowRectExForDpi adjust_for_dpi;
    HMODULE opengl32;
    HWND clip_owner; /* hidden window that owns what we put on the clipboard */
    char monitor_names[MAX_MONITORS][MONITOR_NAME_CAP];
} g;

typedef struct
{
    bool is_char;
    bool is_touch; /* a finger from WM_TOUCH: goes through core_push_touch */
    Event ev;
    uint32_t cp;
} PendingItem;

struct BackendWindow
{
    PendingItem pending[PENDING_MAX];
    int pending_count;
    bool pumping;

    HWND hwnd;
    HDC hdc;
    HGLRC glrc;
    Core *core;
    RenderMode render;
    WindowMode mode;

    bool focused, hovered, tracking_leave, minimized, maximized;
    int buttons_down;
    bool captured;                /* mouse_capture(true) is in effect */
    bool frameless;               /* no non-client area at all: WM_NCCALCSIZE removes it */
    bool text_input_off;          /* window_text_input_stop: no input method */
    int ime_x, ime_y, ime_h;      /* the caret rectangle, for the composition and candidate windows */
    DWORD style_framed, style_frameless; /* the two looks of this window, for window_set_decorated */
    HitTestFunc hit_cb;
    PlatformWindow *hit_w;
    void *hit_user;
    bool in_modal, in_live;       /* inside the OS's drag/menu loop; inside the live callback */
    PlatformWindow *live_w;
    FrameCallback live_cb;
    void *live_user;
    int cursor_mode;
    int cursor_shape;
    HCURSOR cursor;
    int last_x, last_y;
    int virtual_x, virtual_y;

    int min_w, min_h, max_w, max_h;
    bool resizable;
    DWORD saved_style;
    RECT saved_rect;
    bool saved_maximized;
    HICON icon_big, icon_small;
    wchar_t high_surrogate;

    uint32_t *pixels;
    int px_w, px_h;
};

/* ========================================================================== */
/*  helpers                                                                   */
/* ========================================================================== */

/* Events raised while the window is being driven by an API call (ShowWindow,
   SetWindowPos) arrive outside the pump; they wait here and are delivered at the
   start of the next pump, so begin_frame never discards them. */
static void push(BackendWindow *b, Event *e)
{
    if (b->pumping && b->core)
        core_push_event(b->core, e);
    else if (b->pending_count < PENDING_MAX)
        b->pending[b->pending_count++] = (PendingItem){.ev = *e};
}

static void push_touch(BackendWindow *b, TouchPhase phase, int id, float x, float y)
{
    if (b->pumping && b->core)
        core_push_touch(b->core, phase, id, x, y, phase == TOUCH_UP ? 0.0f : 1.0f);
    else if (b->pending_count < PENDING_MAX)
    {
        PendingItem it = {.is_touch = true};
        it.ev.type = EVENT_TOUCH;
        it.ev.data.touch.phase = phase;
        it.ev.data.touch.id = id;
        it.ev.data.touch.x = x;
        it.ev.data.touch.y = y;
        b->pending[b->pending_count++] = it;
    }
}

static void push_char(BackendWindow *b, uint32_t cp)
{
    if (b->pumping && b->core)
        core_push_char(b->core, cp);
    else if (b->pending_count < PENDING_MAX)
        b->pending[b->pending_count++] = (PendingItem){.is_char = true, .cp = cp};
}

static UINT window_dpi(HWND hwnd)
{
    if (hwnd && g.get_dpi_for_window)
        return g.get_dpi_for_window(hwnd);
    return g.get_dpi_for_system ? g.get_dpi_for_system() : 96;
}

static void adjust_rect(RECT *rc, DWORD style, DWORD exstyle, HWND hwnd)
{
    if (g.adjust_for_dpi)
        g.adjust_for_dpi(rc, style, FALSE, exstyle, window_dpi(hwnd));
    else
        AdjustWindowRectEx(rc, style, FALSE, exstyle);
}

static DWORD window_style(bool resizable, WindowMode mode)
{
    if (mode != WINDOW_WINDOWED)
        return WS_POPUP;
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!resizable)
        style &= ~(DWORD)(WS_THICKFRAME | WS_MAXIMIZEBOX);
    return style;
}

/* The style bits and extended style of a window of a given kind. `framed` is the
   look with a title bar and border, `frameless` the look without; frameless windows
   keep WS_THICKFRAME (for resizing through the hit test) and lose the frame in
   WM_NCCALCSIZE. */
static void kind_styles(const WindowConfig *cfg, DWORD *framed, DWORD *frameless, DWORD *ex, bool *start_frameless)
{
    DWORD resize = cfg->resizable ? (WS_THICKFRAME | WS_MAXIMIZEBOX) : 0;
    *ex = 0;
    *start_frameless = cfg->undecorated;
    *frameless = WS_POPUP | WS_MINIMIZEBOX | resize;
    switch (cfg->kind)
    {
    case WINDOW_KIND_DIALOG:
        *framed = WS_POPUP | WS_CAPTION | WS_SYSMENU | (cfg->resizable ? WS_THICKFRAME : 0);
        break;
    case WINDOW_KIND_UTILITY:
        *framed = WS_POPUP | WS_CAPTION | WS_SYSMENU | (cfg->resizable ? WS_THICKFRAME : 0);
        *ex = WS_EX_TOOLWINDOW;
        break;
    case WINDOW_KIND_POPUP:
        *framed = *frameless = WS_POPUP;
        *ex = WS_EX_TOOLWINDOW;
        *start_frameless = true;
        break;
    case WINDOW_KIND_TOOLTIP:
        *framed = *frameless = WS_POPUP;
        *ex = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;
        *start_frameless = true;
        break;
    default:
        *framed = window_style(cfg->resizable, WINDOW_WINDOWED);
        break;
    }
}

static void client_size(BackendWindow *b, int *w, int *h)
{
    RECT rc;
    GetClientRect(b->hwnd, &rc);
    if (w)
        *w = rc.right - rc.left;
    if (h)
        *h = rc.bottom - rc.top;
}

static void client_origin(BackendWindow *b, int *x, int *y)
{
    POINT p = {0, 0};
    ClientToScreen(b->hwnd, &p);
    if (x)
        *x = p.x;
    if (y)
        *y = p.y;
}

static void to_wide_title(const char *title, wchar_t *out, size_t cap)
{
    if (!title || !win32_widen(title, out, cap))
        out[0] = L'\0';
}

/* ========================================================================== */
/*  monitors                                                                  */
/* ========================================================================== */

typedef struct
{
    HMONITOR list[MAX_MONITORS];
    int count;
} MonitorList;

static BOOL CALLBACK collect_monitor(HMONITOR m, HDC dc, LPRECT rc, LPARAM data)
{
    (void)dc;
    (void)rc;
    MonitorList *ml = (MonitorList *)data;
    if (ml->count < MAX_MONITORS)
        ml->list[ml->count++] = m;
    return TRUE;
}

static void list_monitors(MonitorList *ml)
{
    ml->count = 0;
    EnumDisplayMonitors(NULL, NULL, collect_monitor, (LPARAM)ml);
}

bool backend_mouse_global_position(int *x, int *y)
{
    POINT p;
    if (!GetCursorPos(&p))
        return false;
    *x = p.x;
    *y = p.y;
    return true;
}

int backend_monitor_count(void)
{
    MonitorList ml;
    list_monitors(&ml);
    return ml.count;
}

bool backend_monitor_info(int index, MonitorInfo *out)
{
    MonitorList ml;
    MONITORINFOEXW mi;
    if (!out)
        return false;
    list_monitors(&ml);
    if (index < 0 || index >= ml.count)
        return false;
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(ml.list[index], (MONITORINFO *)&mi))
        return false;

    memset(out, 0, sizeof *out);
    out->index = index;
    out->x = mi.rcMonitor.left;
    out->y = mi.rcMonitor.top;
    out->width = mi.rcMonitor.right - mi.rcMonitor.left;
    out->height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    out->work_x = mi.rcWork.left;
    out->work_y = mi.rcWork.top;
    out->work_w = mi.rcWork.right - mi.rcWork.left;
    out->work_h = mi.rcWork.bottom - mi.rcWork.top;
    out->refresh_hz = 60;
    out->content_scale = 1.0f;
    out->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    DEVMODEW dm;
    memset(&dm, 0, sizeof dm);
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        out->refresh_hz = (int)dm.dmDisplayFrequency;

    HDC dc = CreateDCW(L"DISPLAY", mi.szDevice, NULL, NULL);
    if (dc)
    {
        out->phys_width_mm = GetDeviceCaps(dc, HORZSIZE);
        out->phys_height_mm = GetDeviceCaps(dc, VERTSIZE);
        DeleteDC(dc);
    }

    if (!win32_narrow(mi.szDevice, g.monitor_names[index], MONITOR_NAME_CAP))
        snprintf(g.monitor_names[index], MONITOR_NAME_CAP, "monitor %d", index);
    out->name = g.monitor_names[index];
    return true;
}

static int primary_monitor(void)
{
    int n = backend_monitor_count();
    for (int i = 0; i < n; i++)
    {
        MonitorInfo mi;
        if (backend_monitor_info(i, &mi) && mi.primary)
            return i;
    }
    return 0;
}

/* ========================================================================== */
/*  key translation                                                           */
/* ========================================================================== */

static const unsigned short k_scan[0x5A] = {
    [0x01] = KEY_ESCAPE,
    [0x02] = KEY_ONE,
    [0x03] = KEY_TWO,
    [0x04] = KEY_THREE,
    [0x05] = KEY_FOUR,
    [0x06] = KEY_FIVE,
    [0x07] = KEY_SIX,
    [0x08] = KEY_SEVEN,
    [0x09] = KEY_EIGHT,
    [0x0A] = KEY_NINE,
    [0x0B] = KEY_ZERO,
    [0x0C] = KEY_MINUS,
    [0x0D] = KEY_EQUAL,
    [0x0E] = KEY_BACKSPACE,
    [0x0F] = KEY_TAB,
    [0x10] = KEY_Q,
    [0x11] = KEY_W,
    [0x12] = KEY_E,
    [0x13] = KEY_R,
    [0x14] = KEY_T,
    [0x15] = KEY_Y,
    [0x16] = KEY_U,
    [0x17] = KEY_I,
    [0x18] = KEY_O,
    [0x19] = KEY_P,
    [0x1A] = KEY_LEFT_BRACKET,
    [0x1B] = KEY_RIGHT_BRACKET,
    [0x1C] = KEY_ENTER,
    [0x1D] = KEY_LEFT_CONTROL,
    [0x1E] = KEY_A,
    [0x1F] = KEY_S,
    [0x20] = KEY_D,
    [0x21] = KEY_F,
    [0x22] = KEY_G,
    [0x23] = KEY_H,
    [0x24] = KEY_J,
    [0x25] = KEY_K,
    [0x26] = KEY_L,
    [0x27] = KEY_SEMICOLON,
    [0x28] = KEY_APOSTROPHE,
    [0x29] = KEY_GRAVE,
    [0x2A] = KEY_LEFT_SHIFT,
    [0x2B] = KEY_BACKSLASH,
    [0x2C] = KEY_Z,
    [0x2D] = KEY_X,
    [0x2E] = KEY_C,
    [0x2F] = KEY_V,
    [0x30] = KEY_B,
    [0x31] = KEY_N,
    [0x32] = KEY_M,
    [0x33] = KEY_COMMA,
    [0x34] = KEY_PERIOD,
    [0x35] = KEY_SLASH,
    [0x36] = KEY_RIGHT_SHIFT,
    [0x37] = KEY_KP_MULTIPLY,
    [0x38] = KEY_LEFT_ALT,
    [0x39] = KEY_SPACE,
    [0x3A] = KEY_CAPS_LOCK,
    [0x3B] = KEY_F1,
    [0x3C] = KEY_F2,
    [0x3D] = KEY_F3,
    [0x3E] = KEY_F4,
    [0x3F] = KEY_F5,
    [0x40] = KEY_F6,
    [0x41] = KEY_F7,
    [0x42] = KEY_F8,
    [0x43] = KEY_F9,
    [0x44] = KEY_F10,
    [0x45] = KEY_NUM_LOCK,
    [0x46] = KEY_SCROLL_LOCK,
    [0x47] = KEY_KP_7,
    [0x48] = KEY_KP_8,
    [0x49] = KEY_KP_9,
    [0x4A] = KEY_KP_SUBTRACT,
    [0x4B] = KEY_KP_4,
    [0x4C] = KEY_KP_5,
    [0x4D] = KEY_KP_6,
    [0x4E] = KEY_KP_ADD,
    [0x4F] = KEY_KP_1,
    [0x50] = KEY_KP_2,
    [0x51] = KEY_KP_3,
    [0x52] = KEY_KP_0,
    [0x53] = KEY_KP_DECIMAL,
    [0x57] = KEY_F11,
    [0x58] = KEY_F12,
    [0x59] = KEY_KP_EQUAL,
};

static const unsigned short k_scan_ext[0x5E] = {
    [0x1C] = KEY_KP_ENTER,
    [0x1D] = KEY_RIGHT_CONTROL,
    [0x35] = KEY_KP_DIVIDE,
    [0x37] = KEY_PRINT_SCREEN,
    [0x38] = KEY_RIGHT_ALT,
    [0x45] = KEY_NUM_LOCK,
    [0x47] = KEY_HOME,
    [0x48] = KEY_UP,
    [0x49] = KEY_PAGE_UP,
    [0x4B] = KEY_LEFT,
    [0x4D] = KEY_RIGHT,
    [0x4F] = KEY_END,
    [0x50] = KEY_DOWN,
    [0x51] = KEY_PAGE_DOWN,
    [0x52] = KEY_INSERT,
    [0x53] = KEY_DELETE,
    [0x5B] = KEY_LEFT_SUPER,
    [0x5C] = KEY_RIGHT_SUPER,
    [0x5D] = KEY_MENU,
};

static int translate_key(WPARAM vk, LPARAM lparam)
{
    int scancode = (int)((lparam >> 16) & 0xFF);
    bool extended = ((lparam >> 24) & 1) != 0;

    if (vk == VK_PAUSE)
        return KEY_PAUSE;
    if (vk == VK_NUMLOCK)
        return KEY_NUM_LOCK;
    if (vk == VK_SNAPSHOT)
        return KEY_PRINT_SCREEN;
    if (extended)
        return scancode < (int)(sizeof k_scan_ext / sizeof k_scan_ext[0]) ? k_scan_ext[scancode] : KEY_NULL;
    return scancode < (int)(sizeof k_scan / sizeof k_scan[0]) ? k_scan[scancode] : KEY_NULL;
}

static int current_mods(void)
{
    int mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        mods |= KEYMOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        mods |= KEYMOD_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000)
        mods |= KEYMOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        mods |= KEYMOD_SUPER;
    return mods;
}

static void handle_key(BackendWindow *b, WPARAM wparam, LPARAM lparam, bool down)
{
    int scancode = (int)((lparam >> 16) & 0xFF);
    bool extended = ((lparam >> 24) & 1) != 0;
    if (scancode == 0x2A && extended)
        return;

    Event e = {.type = EVENT_KEY};
    e.data.key.key = translate_key(wparam, lparam);
    e.data.key.scancode = scancode | (extended ? 0xE000 : 0);
    e.data.key.down = down;
    e.data.key.repeat = down && ((lparam >> 30) & 1);
    e.data.key.mods = current_mods();
    push(b, &e);
}

/* ========================================================================== */
/*  cursor and mouse mode                                                     */
/* ========================================================================== */

static LPCWSTR cursor_resource(int cursor)
{
    switch (cursor)
    {
    case CURSOR_IBEAM:
        return ZEN_IDC_IBEAM;
    case CURSOR_CROSSHAIR:
        return ZEN_IDC_CROSS;
    case CURSOR_HAND:
        return ZEN_IDC_HAND;
    case CURSOR_RESIZE_EW:
        return ZEN_IDC_SIZEWE;
    case CURSOR_RESIZE_NS:
        return ZEN_IDC_SIZENS;
    case CURSOR_RESIZE_NWSE:
        return ZEN_IDC_SIZENWSE;
    case CURSOR_RESIZE_NESW:
        return ZEN_IDC_SIZENESW;
    case CURSOR_RESIZE_ALL:
        return ZEN_IDC_SIZEALL;
    case CURSOR_NOT_ALLOWED:
        return ZEN_IDC_NO;
    default:
        return ZEN_IDC_ARROW;
    }
}

static void apply_clip(BackendWindow *b)
{
    if (b->cursor_mode == MOUSE_MODE_CAPTURED && b->focused)
    {
        RECT rc;
        POINT tl, br;
        GetClientRect(b->hwnd, &rc);
        tl.x = rc.left;
        tl.y = rc.top;
        br.x = rc.right;
        br.y = rc.bottom;
        ClientToScreen(b->hwnd, &tl);
        ClientToScreen(b->hwnd, &br);
        RECT clip = {tl.x, tl.y, br.x, br.y};
        ClipCursor(&clip);
    }
    else
    {
        ClipCursor(NULL);
    }
}

static void warp_to_center(BackendWindow *b)
{
    int w, h;
    client_size(b, &w, &h);
    POINT p = {w / 2, h / 2};
    ClientToScreen(b->hwnd, &p);
    SetCursorPos(p.x, p.y);
}

void backend_set_cursor(BackendWindow *b, int cursor)
{
    if (cursor < 0 || cursor >= CURSOR_COUNT)
        cursor = CURSOR_DEFAULT;
    b->cursor_shape = cursor;
    b->cursor = LoadCursorW(NULL, cursor_resource(cursor));
    if (b->cursor_mode == MOUSE_MODE_NORMAL && b->hovered)
        SetCursor(b->cursor);
}

void backend_set_decorated(BackendWindow *b, bool on)
{
    if (b->mode != WINDOW_WINDOWED)
        return; /* fullscreen has none; the choice shows when it returns to a window */
    b->frameless = !on;
    DWORD cur = (DWORD)GetWindowLongPtrW(b->hwnd, GWL_STYLE);
    DWORD keep = cur & (WS_VISIBLE | WS_MAXIMIZE | WS_MINIMIZE);
    SetWindowLongPtrW(b->hwnd, GWL_STYLE, (LONG_PTR)(keep | (on ? b->style_framed : b->style_frameless)));
    SetWindowPos(b->hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void backend_set_hit_test(BackendWindow *b, PlatformWindow *w, HitTestFunc fn, void *user)
{
    b->hit_cb = fn;
    b->hit_w = w;
    b->hit_user = user;
}

struct PlatformCursor
{
    HCURSOR handle;
};

PlatformCursor *backend_cursor_create(const uint32_t *argb, int w, int h, int hot_x, int hot_y)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HDC dc = GetDC(NULL);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, dc);
    HBITMAP mask = CreateBitmap(w, h, 1, 1, NULL);
    if (!color || !mask || !bits)
    {
        if (color)
            DeleteObject(color);
        if (mask)
            DeleteObject(mask);
        error_set("cannot create the cursor bitmap");
        return NULL;
    }
    memcpy(bits, argb, (size_t)w * (size_t)h * 4); /* 0xAARRGGBB is BGRA in memory, what a 32-bit DIB holds */

    ICONINFO ii;
    memset(&ii, 0, sizeof ii);
    ii.fIcon = FALSE;
    ii.xHotspot = (DWORD)hot_x;
    ii.yHotspot = (DWORD)hot_y;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    if (!icon)
    {
        error_set("cannot create the cursor");
        return NULL;
    }
    PlatformCursor *c = malloc(sizeof *c);
    if (!c)
    {
        DestroyIcon(icon);
        return NULL;
    }
    c->handle = (HCURSOR)icon;
    return c;
}

void backend_cursor_destroy(PlatformCursor *c)
{
    DestroyIcon((HICON)c->handle);
    free(c);
}

void backend_set_cursor_image(BackendWindow *b, PlatformCursor *c)
{
    if (!c)
    {
        backend_set_cursor(b, b->cursor_shape);
        return;
    }
    b->cursor = c->handle;
    if (b->cursor_mode == MOUSE_MODE_NORMAL && b->hovered)
        SetCursor(b->cursor);
}

void backend_set_mouse_mode(BackendWindow *b, int mode)
{
    bool was_captured = b->cursor_mode == MOUSE_MODE_CAPTURED;
    b->cursor_mode = mode;
    if (mode == MOUSE_MODE_CAPTURED && !was_captured)
    {
        b->virtual_x = b->last_x;
        b->virtual_y = b->last_y;
        apply_clip(b);
        warp_to_center(b);
    }
    else if (mode != MOUSE_MODE_CAPTURED)
    {
        ClipCursor(NULL);
    }
    SetCursor(mode == MOUSE_MODE_NORMAL ? b->cursor : NULL);
}

void backend_set_live_callback(BackendWindow *b, PlatformWindow *w, FrameCallback cb, void *user)
{
    b->live_w = w;
    b->live_cb = cb;
    b->live_user = user;
    if (b->in_modal) /* set from inside a modal loop already running */
    {
        if (cb)
            SetTimer(b->hwnd, LIVE_TIMER_ID, 16, NULL);
        else
            KillTimer(b->hwnd, LIVE_TIMER_ID);
    }
}

int backend_lock_state(void)
{
    return ((GetKeyState(VK_CAPITAL) & 1) ? KEYMOD_CAPS_LOCK : 0) | ((GetKeyState(VK_NUMLOCK) & 1) ? KEYMOD_NUM_LOCK : 0);
}

bool backend_mouse_capture(BackendWindow *b, bool on)
{
    if (on)
    {
        SetCapture(b->hwnd);
        b->captured = GetCapture() == b->hwnd;
    }
    else
    {
        b->captured = false;
        if (b->buttons_down == 0 && GetCapture() == b->hwnd)
            ReleaseCapture();
    }
    return b->captured;
}

void backend_set_mouse_pos(BackendWindow *b, int x, int y)
{
    POINT p = {x, y};
    ClientToScreen(b->hwnd, &p);
    SetCursorPos(p.x, p.y);
    b->last_x = b->virtual_x = x;
    b->last_y = b->virtual_y = y;
}

/* ========================================================================== */
/*  window procedure                                                          */
/* ========================================================================== */

static void handle_button(BackendWindow *b, int button, bool down, LPARAM lparam)
{
    if (down)
    {
        if (b->buttons_down++ == 0)
            SetCapture(b->hwnd);
    }
    else if (b->buttons_down > 0 && --b->buttons_down == 0 && !b->captured)
    {
        ReleaseCapture();
    }
    Event e = {.type = EVENT_MOUSE_BUTTON};
    e.data.mouse.button = button;
    e.data.mouse.down = down;
    e.data.mouse.x = GET_X_LPARAM(lparam);
    e.data.mouse.y = GET_Y_LPARAM(lparam);
    push(b, &e);
}

static void handle_mouse_move(BackendWindow *b, LPARAM lparam)
{
    int x = GET_X_LPARAM(lparam);
    int y = GET_Y_LPARAM(lparam);

    if (!b->tracking_leave)
    {
        TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, b->hwnd, 0};
        TrackMouseEvent(&tme);
        b->tracking_leave = true;
    }
    if (!b->hovered)
    {
        b->hovered = true;
        Event enter = {.type = EVENT_WINDOW_ENTER};
        enter.data.enter.entered = true;
        push(b, &enter);
    }

    if (b->cursor_mode == MOUSE_MODE_CAPTURED && b->focused)
    {
        int w, h;
        client_size(b, &w, &h);
        int cx = w / 2;
        int cy = h / 2;
        if (x == cx && y == cy)
            return;
        b->virtual_x += x - cx;
        b->virtual_y += y - cy;
        x = b->virtual_x;
        y = b->virtual_y;
        warp_to_center(b);
    }
    else
    {
        b->virtual_x = x;
        b->virtual_y = y;
    }
    b->last_x = x;
    b->last_y = y;

    Event e = {.type = EVENT_MOUSE_MOVE};
    e.data.mouse.x = x;
    e.data.mouse.y = y;
    push(b, &e);
}

static void handle_size(BackendWindow *b, WPARAM wparam, LPARAM lparam)
{
    int w = LOWORD(lparam);
    int h = HIWORD(lparam);

    if (wparam == SIZE_MINIMIZED)
    {
        if (!b->minimized)
        {
            b->minimized = true;
            Event e = {.type = EVENT_WINDOW_MINIMIZE};
            push(b, &e);
        }
        return;
    }

    bool was_minimized = b->minimized;
    bool was_maximized = b->maximized;
    b->minimized = false;
    b->maximized = wparam == SIZE_MAXIMIZED;

    Event r = {.type = EVENT_WINDOW_RESIZE};
    r.data.resize.w = w;
    r.data.resize.h = h;
    push(b, &r);
    Event fb = {.type = EVENT_WINDOW_FB_RESIZE};
    fb.data.resize.w = w;
    fb.data.resize.h = h;
    push(b, &fb);

    if (b->maximized && !was_maximized)
    {
        Event e = {.type = EVENT_WINDOW_MAXIMIZE};
        push(b, &e);
    }
    else if (!b->maximized && (was_minimized || was_maximized))
    {
        Event e = {.type = EVENT_WINDOW_RESTORE};
        push(b, &e);
    }
    apply_clip(b);
}

static void handle_drop(BackendWindow *b, HDROP drop)
{
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    char **paths = count ? calloc(count, sizeof *paths) : NULL;
    UINT stored = 0;
    for (UINT i = 0; paths && i < count; i++)
    {
        wchar_t wide[MAX_PATH * 4];
        char utf8[MAX_PATH * 4 * 3];
        if (DragQueryFileW(drop, i, wide, (UINT)(sizeof wide / sizeof wide[0])) == 0)
            continue;
        if (!win32_narrow(wide, utf8, sizeof utf8))
            continue;
        win32_slashes(utf8);
        paths[stored] = _strdup(utf8);
        if (paths[stored])
            stored++;
    }
    if (stored && b->pumping && b->core)
    {
        Event e = {.type = EVENT_WINDOW_DROP};
        e.data.drop.count = (int)stored;
        e.data.drop.paths = (const char **)paths;
        push(b, &e);
    }
    for (UINT i = 0; i < stored; i++)
        free(paths[i]);
    free(paths);
    DragFinish(drop);
}

static void handle_char(BackendWindow *b, WPARAM wparam)
{
    uint32_t cp = (uint32_t)wparam;
    if (cp >= 0xD800 && cp <= 0xDBFF)
    {
        b->high_surrogate = (wchar_t)cp;
        return;
    }
    if (cp >= 0xDC00 && cp <= 0xDFFF)
    {
        if (!b->high_surrogate)
            return;
        cp = 0x10000 + (((uint32_t)b->high_surrogate - 0xD800) << 10) + (cp - 0xDC00);
    }
    b->high_surrogate = 0;
    if (cp >= 0x20 && cp != 0x7F)
        push_char(b, cp);
}

/* ---- input method (imm32, loaded at run time so nothing new is imported) ---- */

typedef HIMC(WINAPI *PFN_ImmGetContext)(HWND);
typedef BOOL(WINAPI *PFN_ImmReleaseContext)(HWND, HIMC);
typedef LONG(WINAPI *PFN_ImmGetCompositionStringW)(HIMC, DWORD, LPVOID, DWORD);
typedef BOOL(WINAPI *PFN_ImmSetCandidateWindow)(HIMC, LPCANDIDATEFORM);
typedef BOOL(WINAPI *PFN_ImmSetCompositionWindow)(HIMC, LPCOMPOSITIONFORM);
typedef HIMC(WINAPI *PFN_ImmAssociateContext)(HWND, HIMC);
typedef BOOL(WINAPI *PFN_ImmAssociateContextEx)(HWND, HIMC, DWORD);

static struct
{
    bool tried;
    HMODULE lib;
    PFN_ImmGetContext get_context;
    PFN_ImmReleaseContext release_context;
    PFN_ImmGetCompositionStringW get_string;
    PFN_ImmSetCandidateWindow set_candidate;
    PFN_ImmSetCompositionWindow set_composition;
    PFN_ImmAssociateContext associate;
    PFN_ImmAssociateContextEx associate_ex;
} imm;

static bool imm_load(void)
{
    if (!imm.tried)
    {
        imm.tried = true;
        imm.lib = LoadLibraryW(L"imm32.dll");
        if (imm.lib)
        {
            imm.get_context = (PFN_ImmGetContext)(void *)GetProcAddress(imm.lib, "ImmGetContext");
            imm.release_context = (PFN_ImmReleaseContext)(void *)GetProcAddress(imm.lib, "ImmReleaseContext");
            imm.get_string = (PFN_ImmGetCompositionStringW)(void *)GetProcAddress(imm.lib, "ImmGetCompositionStringW");
            imm.set_candidate = (PFN_ImmSetCandidateWindow)(void *)GetProcAddress(imm.lib, "ImmSetCandidateWindow");
            imm.set_composition = (PFN_ImmSetCompositionWindow)(void *)GetProcAddress(imm.lib, "ImmSetCompositionWindow");
            imm.associate = (PFN_ImmAssociateContext)(void *)GetProcAddress(imm.lib, "ImmAssociateContext");
            imm.associate_ex = (PFN_ImmAssociateContextEx)(void *)GetProcAddress(imm.lib, "ImmAssociateContextEx");
        }
    }
    return imm.get_context && imm.release_context && imm.get_string;
}

/* Put the composition and candidate windows by the caret. */
static void imm_place(BackendWindow *b)
{
    if (!imm_load() || !imm.set_candidate || !imm.set_composition)
        return;
    HIMC ctx = imm.get_context(b->hwnd);
    if (!ctx)
        return;
    COMPOSITIONFORM cf = {CFS_POINT, {b->ime_x, b->ime_y}, {0, 0, 0, 0}};
    imm.set_composition(ctx, &cf);
    CANDIDATEFORM cand = {0, CFS_CANDIDATEPOS, {b->ime_x, b->ime_y + b->ime_h}, {0, 0, 0, 0}};
    imm.set_candidate(ctx, &cand);
    imm.release_context(b->hwnd, ctx);
}

/* UTF-16 to the UTF-8 EVENT_TEXT_EDIT carries (cut on a character boundary). */
static void imm_edit(BackendWindow *b, const wchar_t *wide, int units, int cursor_units)
{
    Event e = {.type = EVENT_TEXT_EDIT};
    int n = 0, cursor = -1;
    for (int i = 0; i <= units; i++)
    {
        if (i == cursor_units)
            cursor = n;
        if (i == units)
            break;
        uint32_t cp = wide[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < units && wide[i + 1] >= 0xDC00 && wide[i + 1] < 0xE000)
        {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (wide[i + 1] - 0xDC00);
            i++;
        }
        char buf[4];
        int len = cp < 0x80 ? (buf[0] = (char)cp, 1)
                  : cp < 0x800 ? (buf[0] = (char)(0xC0 | (cp >> 6)), buf[1] = (char)(0x80 | (cp & 0x3F)), 2)
                  : cp < 0x10000 ? (buf[0] = (char)(0xE0 | (cp >> 12)), buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F)),
                                    buf[2] = (char)(0x80 | (cp & 0x3F)), 3)
                                 : (buf[0] = (char)(0xF0 | (cp >> 18)), buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F)),
                                    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F)), buf[3] = (char)(0x80 | (cp & 0x3F)), 4);
        if (n + len > (int)sizeof e.data.edit.text - 1)
            break;
        memcpy(e.data.edit.text + n, buf, (size_t)len);
        n += len;
    }
    e.data.edit.text[n] = '\0';
    e.data.edit.cursor = cursor < 0 ? n : cursor;
    push(b, &e);
}

/* WM_IME_COMPOSITION: the committed text becomes characters, the composition an edit
   event. Returns true when it was handled. */
static bool imm_composition(BackendWindow *b, LPARAM lparam)
{
    if (b->text_input_off || !imm_load())
        return false;
    HIMC ctx = imm.get_context(b->hwnd);
    if (!ctx)
        return false;
    if (lparam & GCS_RESULTSTR)
    {
        wchar_t buf[256];
        LONG bytes = imm.get_string(ctx, GCS_RESULTSTR, buf, sizeof buf - sizeof(wchar_t));
        for (LONG i = 0; i < bytes / (LONG)sizeof(wchar_t); i++)
        {
            uint32_t cp = buf[i];
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < bytes / (LONG)sizeof(wchar_t))
                cp = 0x10000 + ((cp - 0xD800) << 10) + (buf[++i] - 0xDC00);
            if (cp >= 0x20 && cp != 0x7F)
                push_char(b, cp);
        }
    }
    if (lparam & GCS_COMPSTR)
    {
        wchar_t buf[128];
        LONG bytes = imm.get_string(ctx, GCS_COMPSTR, buf, sizeof buf - sizeof(wchar_t));
        LONG cursor = imm.get_string(ctx, GCS_CURSORPOS, NULL, 0);
        imm_edit(b, buf, bytes > 0 ? (int)(bytes / (LONG)sizeof(wchar_t)) : 0, (int)cursor);
    }
    imm.release_context(b->hwnd, ctx);
    return true;
}

void backend_set_text_input(BackendWindow *b, bool on)
{
    b->text_input_off = !on;
    if (!imm_load())
        return;
    if (on)
    {
        if (imm.associate_ex)
            imm.associate_ex(b->hwnd, NULL, 0x0010 /* IACE_DEFAULT */);
    }
    else if (imm.associate)
        imm.associate(b->hwnd, NULL);
}

void backend_set_text_input_rect(BackendWindow *b, int x, int y, int w, int h)
{
    (void)w;
    b->ime_x = x;
    b->ime_y = y;
    b->ime_h = h;
    imm_place(b);
}

static void paint_pixels(BackendWindow *b);

/* The OS runs its own loop while a window is dragged or resized, so the
   application's does not; give it a chance to draw. */
static void live_tick(BackendWindow *b)
{
    if (b->live_cb && b->in_modal && !b->in_live)
    {
        b->in_live = true;
        b->live_cb(b->live_w, b->live_user);
        b->in_live = false;
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    BackendWindow *b = (BackendWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (!b)
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg)
    {
    case WM_CLOSE:
    {
        Event e = {.type = EVENT_WINDOW_CLOSE};
        push(b, &e);
        return 0;
    }
    case WM_SIZE:
        handle_size(b, wparam, lparam);
        live_tick(b);
        return 0;
    case WM_IME_STARTCOMPOSITION:
        if (!b->text_input_off && imm_load())
        {
            imm_place(b);
            return 0; /* the application shows the composition: no system window */
        }
        break;
    case WM_IME_COMPOSITION:
        if (imm_composition(b, lparam))
            return 0;
        break;
    case WM_IME_ENDCOMPOSITION:
        if (!b->text_input_off)
        {
            imm_edit(b, L"", 0, 0); /* an empty edit ends the composition */
            return 0;
        }
        break;
    case WM_TOUCH:
    {
        /* Fingers on a touch screen. Windows reports them in hundredths of a pixel of the
           screen; the window's coordinates are client pixels. */
        UINT count = LOWORD(wparam);
        TOUCHINPUT inputs[32];
        if (count > 32)
            count = 32;
        if (count && GetTouchInputInfo((HTOUCHINPUT)lparam, count, inputs, sizeof(TOUCHINPUT)))
        {
            for (UINT i = 0; i < count; i++)
            {
                POINT pt = {inputs[i].x / 100, inputs[i].y / 100};
                ScreenToClient(hwnd, &pt);
                TouchPhase phase = (inputs[i].dwFlags & TOUCHEVENTF_DOWN)   ? TOUCH_DOWN
                                   : (inputs[i].dwFlags & TOUCHEVENTF_UP)   ? TOUCH_UP
                                   : (inputs[i].dwFlags & TOUCHEVENTF_MOVE) ? TOUCH_MOVE
                                                                            : TOUCH_MOVE;
                if (inputs[i].dwFlags & (TOUCHEVENTF_DOWN | TOUCHEVENTF_UP | TOUCHEVENTF_MOVE))
                    push_touch(b, phase, (int)inputs[i].dwID, (float)pt.x, (float)pt.y);
            }
            CloseTouchInputHandle((HTOUCHINPUT)lparam);
            return 0;
        }
        break;
    }
    case WM_NCCALCSIZE:
        if (b->frameless && wparam)
            return 0; /* the client area is the whole window */
        break;
    case WM_NCHITTEST:
        if (b->hit_cb)
        {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &pt);
            switch (b->hit_cb(b->hit_w, pt.x, pt.y, b->hit_user))
            {
            case HIT_DRAG: return HTCAPTION;
            case HIT_RESIZE_TOPLEFT: return HTTOPLEFT;
            case HIT_RESIZE_TOP: return HTTOP;
            case HIT_RESIZE_TOPRIGHT: return HTTOPRIGHT;
            case HIT_RESIZE_RIGHT: return HTRIGHT;
            case HIT_RESIZE_BOTTOMRIGHT: return HTBOTTOMRIGHT;
            case HIT_RESIZE_BOTTOM: return HTBOTTOM;
            case HIT_RESIZE_BOTTOMLEFT: return HTBOTTOMLEFT;
            case HIT_RESIZE_LEFT: return HTLEFT;
            default:
                if (b->frameless)
                    return HTCLIENT;
                break; /* a framed window keeps its own frame hits */
            }
        }
        else if (b->frameless)
            return HTCLIENT;
        break;
    case WM_ENTERSIZEMOVE:
    case WM_ENTERMENULOOP:
        b->in_modal = true;
        if (b->live_cb)
            SetTimer(hwnd, LIVE_TIMER_ID, 16, NULL);
        break;
    case WM_EXITSIZEMOVE:
    case WM_EXITMENULOOP:
        b->in_modal = false;
        KillTimer(hwnd, LIVE_TIMER_ID);
        break;
    case WM_TIMER:
        if (wparam == LIVE_TIMER_ID)
        {
            live_tick(b);
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if ((HWND)lparam != hwnd)
            b->captured = false; /* another window or the system took it */
        break;
    case WM_MOVE:
    {
        Event e = {.type = EVENT_WINDOW_MOVE};
        e.data.move.x = (int)(short)LOWORD(lparam);
        e.data.move.y = (int)(short)HIWORD(lparam);
        push(b, &e);
        apply_clip(b);
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    {
        b->focused = msg == WM_SETFOCUS;
        Event e = {.type = EVENT_WINDOW_FOCUS};
        e.data.focus.gained = b->focused;
        push(b, &e);
        apply_clip(b);
        return 0;
    }
    case WM_MOUSEMOVE:
        handle_mouse_move(b, lparam);
        return 0;
    case WM_MOUSELEAVE:
    {
        b->tracking_leave = false;
        b->hovered = false;
        Event e = {.type = EVENT_WINDOW_ENTER};
        e.data.enter.entered = false;
        push(b, &e);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        handle_button(b, MOUSE_LEFT, msg == WM_LBUTTONDOWN, lparam);
        return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        handle_button(b, MOUSE_RIGHT, msg == WM_RBUTTONDOWN, lparam);
        return 0;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        handle_button(b, MOUSE_MIDDLE, msg == WM_MBUTTONDOWN, lparam);
        return 0;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        handle_button(b, GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? MOUSE_X1 : MOUSE_X2, msg == WM_XBUTTONDOWN, lparam);
        return TRUE;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    {
        float notches = (float)GET_WHEEL_DELTA_WPARAM(wparam) / (float)WHEEL_DELTA;
        Event e = {.type = EVENT_MOUSE_WHEEL};
        if (msg == WM_MOUSEWHEEL)
            e.data.wheel.y = notches;
        else
            e.data.wheel.x = notches;
        push(b, &e);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_KEYUP:
        handle_key(b, wparam, lparam, msg == WM_KEYDOWN);
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        handle_key(b, wparam, lparam, msg == WM_SYSKEYDOWN);
        if (wparam == VK_F4)
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        return 0;
    case WM_CHAR:
        handle_char(b, wparam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT)
        {
            SetCursor(b->cursor_mode == MOUSE_MODE_NORMAL ? b->cursor : NULL);
            return TRUE;
        }
        break;
    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = (MINMAXINFO *)lparam;
        DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
        DWORD exstyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (b->min_w > 0 || b->min_h > 0)
        {
            RECT rc = {0, 0, b->min_w > 0 ? b->min_w : 1, b->min_h > 0 ? b->min_h : 1};
            adjust_rect(&rc, style, exstyle, hwnd);
            mmi->ptMinTrackSize.x = rc.right - rc.left;
            mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        }
        if (b->max_w > 0 || b->max_h > 0)
        {
            RECT rc = {0, 0, b->max_w > 0 ? b->max_w : 32767, b->max_h > 0 ? b->max_h : 32767};
            adjust_rect(&rc, style, exstyle, hwnd);
            mmi->ptMaxTrackSize.x = rc.right - rc.left;
            mmi->ptMaxTrackSize.y = rc.bottom - rc.top;
        }
        return 0;
    }
    case WM_DPICHANGED:
    {
        const RECT *rc = (const RECT *)lparam;
        SetWindowPos(hwnd, NULL, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DROPFILES:
        handle_drop(b, (HDROP)wparam);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (b->render == RENDER_PIXELS && b->pixels)
        {
            paint_pixels(b);
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* ========================================================================== */
/*  init / shutdown                                                           */
/* ========================================================================== */

static void load_dpi_functions(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32)
        return;
    PFN_SetProcessDpiAwarenessContext set_awareness = (PFN_SetProcessDpiAwarenessContext)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (!set_awareness || !set_awareness((HANDLE)-4))
        SetProcessDPIAware();
    g.get_dpi_for_window = (PFN_GetDpiForWindow)(void *)GetProcAddress(user32, "GetDpiForWindow");
    g.get_dpi_for_system = (PFN_GetDpiForSystem)(void *)GetProcAddress(user32, "GetDpiForSystem");
    g.adjust_for_dpi = (PFN_AdjustWindowRectExForDpi)(void *)GetProcAddress(user32, "AdjustWindowRectExForDpi");
}

bool backend_init(void)
{
    g.instance = GetModuleHandleW(NULL);
    load_dpi_functions();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = g.instance;
    wc.hCursor = LoadCursorW(NULL, ZEN_IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIcon = LoadIconW(NULL, CURSOR_ID(32512));
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return win32_error("cannot register the window class", NULL);
    g.class_registered = true;
    return true;
}

void backend_shutdown(void)
{
    if (g.class_registered)
        UnregisterClassW(WINDOW_CLASS, g.instance);
    memset(&g, 0, sizeof g);
}

/* ========================================================================== */
/*  OpenGL                                                                    */
/* ========================================================================== */

static bool has_extension(const char *list, const char *name)
{
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

static void set_legacy_pixel_format(HDC dc)
{
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int format = ChoosePixelFormat(dc, &pfd);
    if (format)
        SetPixelFormat(dc, format, &pfd);
}

static void load_wgl(void)
{
    if (g.wgl_loaded)
        return;
    g.wgl_loaded = true;
    g.opengl32 = GetModuleHandleW(L"opengl32.dll");

    HWND dummy = CreateWindowExW(0, WINDOW_CLASS, L"", WS_OVERLAPPED, 0, 0, 1, 1, NULL, NULL, g.instance, NULL);
    if (!dummy)
        return;
    HDC dc = GetDC(dummy);
    set_legacy_pixel_format(dc);
    HGLRC rc = wglCreateContext(dc);
    if (rc && wglMakeCurrent(dc, rc))
    {
        g.choose_pixel_format = (PFN_wglChoosePixelFormatARB)(void *)wglGetProcAddress("wglChoosePixelFormatARB");
        g.create_context = (PFN_wglCreateContextAttribsARB)(void *)wglGetProcAddress("wglCreateContextAttribsARB");
        g.swap_interval = (PFN_wglSwapIntervalEXT)(void *)wglGetProcAddress("wglSwapIntervalEXT");
        PFN_wglGetExtensionsStringARB get_ext = (PFN_wglGetExtensionsStringARB)(void *)wglGetProcAddress("wglGetExtensionsStringARB");
        if (get_ext)
            g.has_es_profile = has_extension(get_ext(dc), "WGL_EXT_create_context_es2_profile");
        wglMakeCurrent(NULL, NULL);
    }
    if (rc)
        wglDeleteContext(rc);
    ReleaseDC(dummy, dc);
    DestroyWindow(dummy);
}

static bool choose_pixel_format(HDC dc, const GLConfig *gl)
{
    if (!g.choose_pixel_format)
    {
        if (gl->msaa > 0)
            return error_set("WGL_ARB_pixel_format is not supported, cannot request %d samples", gl->msaa);
        set_legacy_pixel_format(dc);
        return true;
    }
    int attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_RED_BITS_ARB, 8, WGL_GREEN_BITS_ARB, 8, WGL_BLUE_BITS_ARB, 8, WGL_ALPHA_BITS_ARB, 8,
        WGL_DEPTH_BITS_ARB, 24, WGL_STENCIL_BITS_ARB, 8,
        WGL_SAMPLE_BUFFERS_ARB, gl->msaa > 0 ? 1 : 0,
        WGL_SAMPLES_ARB, gl->msaa > 0 ? gl->msaa : 0,
        0};
    int format = 0;
    UINT count = 0;
    if (!g.choose_pixel_format(dc, attribs, NULL, 1, &format, &count) || count == 0)
        return error_set("no pixel format with RGBA8, depth 24, stencil 8 and %d samples", gl->msaa);
    PIXELFORMATDESCRIPTOR pfd;
    DescribePixelFormat(dc, format, sizeof pfd, &pfd);
    if (!SetPixelFormat(dc, format, &pfd))
        return win32_error("cannot set the pixel format", NULL);
    return true;
}

static HGLRC create_gl_context(HDC dc, const GLConfig *gl)
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
    int mask = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
    if (profile == GL_PROFILE_COMPAT)
    {
        name = "compatibility";
        mask = WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
    }
    else if (profile == GL_PROFILE_ES)
    {
        name = "ES";
        mask = WGL_CONTEXT_ES2_PROFILE_BIT_EXT;
        if (!g.has_es_profile)
        {
            error_set("WGL_EXT_create_context_es2_profile is not supported");
            return NULL;
        }
    }
    if (!g.create_context)
    {
        error_set("WGL_ARB_create_context is not supported");
        return NULL;
    }

    int attribs[9];
    int n = 0;
    attribs[n++] = WGL_CONTEXT_MAJOR_VERSION_ARB;
    attribs[n++] = major;
    attribs[n++] = WGL_CONTEXT_MINOR_VERSION_ARB;
    attribs[n++] = minor;
    attribs[n++] = WGL_CONTEXT_PROFILE_MASK_ARB;
    attribs[n++] = mask;
    if (gl->debug)
    {
        attribs[n++] = WGL_CONTEXT_FLAGS_ARB;
        attribs[n++] = WGL_CONTEXT_DEBUG_BIT_ARB;
    }
    attribs[n] = 0;

    HGLRC rc = g.create_context(dc, NULL, attribs);
    if (!rc)
        error_set("cannot create an OpenGL %s %d.%d context%s", name, major, minor, gl->debug ? " with debug" : "");
    return rc;
}

void backend_make_current(BackendWindow *b)
{
    if (b->render == RENDER_GL)
        wglMakeCurrent(b->hdc, b->glrc);
}

void backend_make_current_on(BackendWindow *target, BackendWindow *context)
{
    if (target->render == RENDER_GL && context->render == RENDER_GL)
        wglMakeCurrent(target->hdc, context->glrc);
}

void backend_set_vsync(BackendWindow *b, bool on)
{
    (void)b;
    if (g.swap_interval)
        g.swap_interval(on ? 1 : 0);
}

void *backend_gl_proc_address(const char *name)
{
    void *p = (void *)wglGetProcAddress(name);
    if (p == NULL || p == (void *)0x1 || p == (void *)0x2 || p == (void *)0x3 || p == (void *)-1)
    {
        if (!g.opengl32)
            g.opengl32 = GetModuleHandleW(L"opengl32.dll");
        p = g.opengl32 ? (void *)GetProcAddress(g.opengl32, name) : NULL;
    }
    return p;
}

/* ========================================================================== */
/*  native handles and Vulkan                                                 */
/* ========================================================================== */

void *backend_native_handle(BackendWindow *b, NativeHandleType type)
{
    switch (type)
    {
    case NATIVE_DISPLAY:
        return (void *)g.instance;
    case NATIVE_WINDOW:
        return (void *)b->hwnd;
    case NATIVE_GL_CONTEXT:
        return (void *)b->glrc;
    }
    return NULL;
}

const char *const *backend_vulkan_extensions(uint32_t *count)
{
    static const char *const ext[] = {"VK_KHR_surface", "VK_KHR_win32_surface"};
    *count = 2;
    return ext;
}

typedef struct
{
    int sType;
    const void *pNext;
    uint32_t flags;
    HINSTANCE hinstance;
    HWND hwnd;
} ZenVkWin32SurfaceCreateInfo;

bool backend_vulkan_create_surface(BackendWindow *b, void *instance, const void *allocator, uint64_t *out_surface)
{
    ZenVkWin32SurfaceCreateInfo info = {
        .sType = ZEN_VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = g.instance,
        .hwnd = b->hwnd,
    };
    return vulkan_call_create_surface(instance, "vkCreateWin32SurfaceKHR", &info, allocator, out_surface);
}

void backend_swap(BackendWindow *b)
{
    if (b->render == RENDER_GL)
        SwapBuffers(b->hdc);
}

/* ========================================================================== */
/*  create / destroy                                                          */
/* ========================================================================== */

BackendWindow *backend_create(const WindowConfig *cfg)
{
    int w = cfg->width > 0 ? cfg->width : 640;
    int h = cfg->height > 0 ? cfg->height : 480;
    wchar_t title[512];
    to_wide_title(cfg->title, title, 512);

    BackendWindow *b = calloc(1, sizeof *b);
    if (!b)
    {
        error_set("out of memory");
        return NULL;
    }
    b->render = cfg->render;
    b->mode = WINDOW_WINDOWED;
    b->resizable = cfg->resizable;
    b->cursor_mode = MOUSE_MODE_NORMAL;
    b->cursor = LoadCursorW(NULL, ZEN_IDC_ARROW);

    if (cfg->render == RENDER_GL)
        load_wgl();

    DWORD ex;
    bool frameless;
    kind_styles(cfg, &b->style_framed, &b->style_frameless, &ex, &frameless);
    b->frameless = frameless;
    DWORD style = frameless ? b->style_frameless : b->style_framed;
    HWND owner = cfg->parent && cfg->parent->b ? cfg->parent->b->hwnd : NULL;
    RECT rc = {0, 0, w, h};
    if (!frameless)
        adjust_rect(&rc, style, ex, NULL); /* a frameless window's client area is the whole window */
    int outer_w = rc.right - rc.left;
    int outer_h = rc.bottom - rc.top;

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (cfg->x == WINDOW_POS_CENTERED || cfg->y == WINDOW_POS_CENTERED)
    {
        MonitorInfo mi;
        int index = cfg->monitor >= 0 ? cfg->monitor : primary_monitor();
        if (backend_monitor_info(index, &mi))
        {
            x = cfg->x == WINDOW_POS_CENTERED ? mi.work_x + (mi.work_w - outer_w) / 2 : cfg->x + rc.left;
            y = cfg->y == WINDOW_POS_CENTERED ? mi.work_y + (mi.work_h - outer_h) / 2 : cfg->y + rc.top;
        }
    }
    else if (cfg->x != WINDOW_POS_UNDEFINED && cfg->y != WINDOW_POS_UNDEFINED)
    {
        x = cfg->x + rc.left;
        y = cfg->y + rc.top;
    }

    b->hwnd = CreateWindowExW(ex, WINDOW_CLASS, title, style, x, y, outer_w, outer_h, owner, NULL, g.instance, b);
    if (!b->hwnd)
    {
        win32_error("cannot create the window", NULL);
        free(b);
        return NULL;
    }
    b->hdc = GetDC(b->hwnd);

    if (cfg->render == RENDER_GL)
    {
        if (!choose_pixel_format(b->hdc, &cfg->gl))
        {
            backend_destroy(b);
            return NULL;
        }
        b->glrc = create_gl_context(b->hdc, &cfg->gl);
        if (!b->glrc)
        {
            backend_destroy(b);
            return NULL;
        }
        wglMakeCurrent(b->hdc, b->glrc);
        if (cfg->vsync)
            backend_set_vsync(b, true);
    }

    RegisterTouchWindow(b->hwnd, 0); /* WM_TOUCH instead of the mouse-from-touch conversion */
    DragAcceptFiles(b->hwnd, TRUE);
    ShowWindow(b->hwnd, cfg->kind == WINDOW_KIND_TOOLTIP ? SW_SHOWNOACTIVATE : SW_SHOW);
    UpdateWindow(b->hwnd);
    b->focused = GetFocus() == b->hwnd;

    if (cfg->mode != WINDOW_WINDOWED)
        backend_set_mode(b, cfg->mode, cfg->monitor);
    return b;
}

void backend_destroy(BackendWindow *b)
{
    if (!b)
        return;
    if (b->cursor_mode == MOUSE_MODE_CAPTURED)
        ClipCursor(NULL);
    if (b->glrc)
    {
        /* Leave another window's context current, unless it draws to this window. */
        if (wglGetCurrentContext() == b->glrc || wglGetCurrentDC() == b->hdc)
            wglMakeCurrent(NULL, NULL);
        wglDeleteContext(b->glrc);
    }
    if (b->hwnd)
    {
        SetWindowLongPtrW(b->hwnd, GWLP_USERDATA, 0);
        if (b->hdc)
            ReleaseDC(b->hwnd, b->hdc);
        DestroyWindow(b->hwnd);
    }
    if (b->icon_big)
        DestroyIcon(b->icon_big);
    if (b->icon_small)
        DestroyIcon(b->icon_small);
    free(b->pixels);
    free(b);
}

/* ========================================================================== */
/*  events                                                                    */
/* ========================================================================== */

void backend_pump_events(BackendWindow *b, Core *core)
{
    MSG msg;
    b->core = core;
    for (int i = 0; i < b->pending_count; i++)
    {
        if (b->pending[i].is_char)
            core_push_char(core, b->pending[i].cp);
        else if (b->pending[i].is_touch)
            core_push_touch(core, b->pending[i].ev.data.touch.phase, b->pending[i].ev.data.touch.id,
                            b->pending[i].ev.data.touch.x, b->pending[i].ev.data.touch.y,
                            b->pending[i].ev.data.touch.phase == TOUCH_UP ? 0.0f : 1.0f);
        else
            core_push_event(core, &b->pending[i].ev);
    }
    b->pending_count = 0;

    b->pumping = true;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    b->pumping = false;
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
/*  geometry and state                                                        */
/* ========================================================================== */

void backend_get_size(BackendWindow *b, int *w, int *h)
{
    client_size(b, w, h);
}

void backend_get_fb_size(BackendWindow *b, int *w, int *h)
{
    client_size(b, w, h);
}

void backend_get_pos(BackendWindow *b, int *x, int *y)
{
    client_origin(b, x, y);
}

void backend_set_size(BackendWindow *b, int w, int h)
{
    DWORD style = (DWORD)GetWindowLongPtrW(b->hwnd, GWL_STYLE);
    DWORD exstyle = (DWORD)GetWindowLongPtrW(b->hwnd, GWL_EXSTYLE);
    RECT rc = {0, 0, w, h};
    adjust_rect(&rc, style, exstyle, b->hwnd);
    SetWindowPos(b->hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void backend_set_pos(BackendWindow *b, int x, int y)
{
    DWORD style = (DWORD)GetWindowLongPtrW(b->hwnd, GWL_STYLE);
    DWORD exstyle = (DWORD)GetWindowLongPtrW(b->hwnd, GWL_EXSTYLE);
    RECT rc = {0, 0, 0, 0};
    adjust_rect(&rc, style, exstyle, b->hwnd);
    SetWindowPos(b->hwnd, NULL, x + rc.left, y + rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

float backend_content_scale(BackendWindow *b)
{
    (void)b;
    return 1.0f;
}

void backend_set_title(BackendWindow *b, const char *title)
{
    wchar_t wide[512];
    to_wide_title(title, wide, 512);
    SetWindowTextW(b->hwnd, wide);
}

void backend_set_size_limits(BackendWindow *b, int minw, int minh, int maxw, int maxh)
{
    b->min_w = minw;
    b->min_h = minh;
    b->max_w = maxw;
    b->max_h = maxh;
}

void backend_minimize(BackendWindow *b)
{
    ShowWindow(b->hwnd, SW_MINIMIZE);
}

void backend_maximize(BackendWindow *b)
{
    ShowWindow(b->hwnd, SW_MAXIMIZE);
}

void backend_restore(BackendWindow *b)
{
    ShowWindow(b->hwnd, SW_RESTORE);
}

void backend_show(BackendWindow *b)
{
    ShowWindow(b->hwnd, SW_SHOW);
}

void backend_hide(BackendWindow *b)
{
    ShowWindow(b->hwnd, SW_HIDE);
}

void backend_focus(BackendWindow *b)
{
    SetForegroundWindow(b->hwnd);
    SetFocus(b->hwnd);
}

void backend_request_attention(BackendWindow *b)
{
    FLASHWINFO fi = {sizeof fi, b->hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 0, 0};
    FlashWindowEx(&fi);
}

bool backend_get_flag(BackendWindow *b, int flag)
{
    switch (flag)
    {
    case WIN_FLAG_FOCUSED:
        return GetFocus() == b->hwnd;
    case WIN_FLAG_MINIMIZED:
        return IsIconic(b->hwnd) != 0;
    case WIN_FLAG_MAXIMIZED:
        return IsZoomed(b->hwnd) != 0;
    case WIN_FLAG_VISIBLE:
        return IsWindowVisible(b->hwnd) != 0;
    case WIN_FLAG_HOVERED:
        return b->hovered;
    default:
        return false;
    }
}

void backend_set_mode(BackendWindow *b, WindowMode mode, int monitor)
{
    if (mode == b->mode)
        return;

    if (mode == WINDOW_WINDOWED)
    {
        SetWindowLongPtrW(b->hwnd, GWL_STYLE, (LONG_PTR)(b->saved_style | WS_VISIBLE));
        SetWindowPos(b->hwnd, HWND_NOTOPMOST, b->saved_rect.left, b->saved_rect.top, b->saved_rect.right - b->saved_rect.left,
                     b->saved_rect.bottom - b->saved_rect.top, SWP_FRAMECHANGED | SWP_NOACTIVATE);
        if (b->saved_maximized)
            ShowWindow(b->hwnd, SW_MAXIMIZE);
        b->mode = WINDOW_WINDOWED;
        return;
    }

    MonitorInfo mi;
    HMONITOR hm = MonitorFromWindow(b->hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info;
    memset(&info, 0, sizeof info);
    info.cbSize = sizeof info;
    RECT target;
    if (monitor >= 0 && backend_monitor_info(monitor, &mi))
    {
        target.left = mi.x;
        target.top = mi.y;
        target.right = mi.x + mi.width;
        target.bottom = mi.y + mi.height;
    }
    else if (GetMonitorInfoW(hm, &info))
    {
        target = info.rcMonitor;
    }
    else
    {
        return;
    }

    if (b->mode == WINDOW_WINDOWED)
    {
        b->saved_style = (DWORD)GetWindowLongPtrW(b->hwnd, GWL_STYLE);
        b->saved_maximized = IsZoomed(b->hwnd) != 0;
        if (b->saved_maximized)
            ShowWindow(b->hwnd, SW_RESTORE);
        GetWindowRect(b->hwnd, &b->saved_rect);
    }
    SetWindowLongPtrW(b->hwnd, GWL_STYLE, (LONG_PTR)(WS_POPUP | WS_VISIBLE));
    SetWindowPos(b->hwnd, HWND_TOP, target.left, target.top, target.right - target.left, target.bottom - target.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    b->mode = mode;
}

WindowMode backend_get_mode(BackendWindow *b)
{
    return b->mode;
}

void backend_set_icon(BackendWindow *b, int w, int h, const uint8_t *rgba)
{
    if (w <= 0 || h <= 0 || !rgba)
        return;
    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof bi);
    bi.bV5Size = sizeof bi;
    bi.bV5Width = w;
    bi.bV5Height = -h;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    uint8_t *dst = NULL;
    HDC dc = GetDC(NULL);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&dst, NULL, 0);
    ReleaseDC(NULL, dc);
    HBITMAP mask = CreateBitmap(w, h, 1, 1, NULL);
    if (!color || !mask || !dst)
    {
        if (color)
            DeleteObject(color);
        if (mask)
            DeleteObject(mask);
        return;
    }
    for (int i = 0; i < w * h; i++)
    {
        dst[i * 4 + 0] = rgba[i * 4 + 2];
        dst[i * 4 + 1] = rgba[i * 4 + 1];
        dst[i * 4 + 2] = rgba[i * 4 + 0];
        dst[i * 4 + 3] = rgba[i * 4 + 3];
    }
    ICONINFO ii = {TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    if (!icon)
        return;

    SendMessageW(b->hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessageW(b->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    if (b->icon_big)
        DestroyIcon(b->icon_big);
    b->icon_big = icon;
}

void backend_set_opacity(BackendWindow *b, float a)
{
    LONG_PTR ex = GetWindowLongPtrW(b->hwnd, GWL_EXSTYLE);
    if (a >= 1.0f)
    {
        SetWindowLongPtrW(b->hwnd, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
        return;
    }
    if (a < 0.0f)
        a = 0.0f;
    SetWindowLongPtrW(b->hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    SetLayeredWindowAttributes(b->hwnd, 0, (BYTE)(a * 255.0f + 0.5f), LWA_ALPHA);
}

void backend_set_always_on_top(BackendWindow *b, bool on)
{
    SetWindowPos(b->hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* ========================================================================== */
/*  pixel surface                                                             */
/* ========================================================================== */

static void blit_pixels(BackendWindow *b, HDC dc)
{
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
    bmi.bmiHeader.biWidth = b->px_w;
    bmi.bmiHeader.biHeight = -b->px_h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, (DWORD)b->px_w, (DWORD)b->px_h, 0, 0, 0, (UINT)b->px_h, b->pixels, &bmi, DIB_RGB_COLORS);
}

static void paint_pixels(BackendWindow *b)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(b->hwnd, &ps);
    blit_pixels(b, dc);
    EndPaint(b->hwnd, &ps);
}

bool backend_lock_pixels(BackendWindow *b, Framebuffer *out)
{
    if (b->render != RENDER_PIXELS || !out)
        return false;
    int w, h;
    client_size(b, &w, &h);
    if (w <= 0 || h <= 0)
        return false;
    if (!b->pixels || b->px_w != w || b->px_h != h)
    {
        uint32_t *grown = calloc((size_t)w * (size_t)h, 4);
        if (!grown)
            return false;
        free(b->pixels);
        b->pixels = grown;
        b->px_w = w;
        b->px_h = h;
    }
    out->pixels = b->pixels;
    out->width = b->px_w;
    out->height = b->px_h;
    out->stride = b->px_w;
    return true;
}

void backend_present_pixels(BackendWindow *b)
{
    if (b->render != RENDER_PIXELS || !b->pixels)
        return;
    blit_pixels(b, b->hdc);
}

/* ========================================================================== */
/*  clipboard                                                                 */
/* ========================================================================== */

/* Formats: text/plain is CF_UNICODETEXT, image/png is the registered "PNG" format
   plus CF_DIBV5 (so Paint and older programs can paste it), text/uri-list is
   CF_HDROP, anything else a registered format named after the MIME type. */

static HWND clip_owner(void)
{
    if (!g.clip_owner)
        g.clip_owner = CreateWindowExW(0, WINDOW_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g.instance, NULL);
    return g.clip_owner;
}

static bool clip_open(void)
{
    for (int i = 0; i < 10; i++) /* another program may hold it for a moment */
    {
        if (OpenClipboard(clip_owner()))
            return true;
        Sleep(10);
    }
    return false;
}

static HGLOBAL clip_global(const void *data, size_t size)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
    if (!h)
        return NULL;
    void *p = GlobalLock(h);
    if (!p)
    {
        GlobalFree(h);
        return NULL;
    }
    if (size)
        memcpy(p, data, size);
    GlobalUnlock(h);
    return h;
}

static bool clip_put(UINT format, const void *data, size_t size)
{
    HGLOBAL h = clip_global(data, size);
    if (!h)
        return false;
    if (!SetClipboardData(format, h))
    {
        GlobalFree(h);
        return false;
    }
    return true;
}

static UINT clip_png_format(void)
{
    static UINT f;
    if (!f)
        f = RegisterClipboardFormatA("PNG");
    return f;
}

static bool clip_put_text(const char *utf8, size_t size)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, (int)size, NULL, 0);
    wchar_t *wide = malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!wide)
        return false;
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, utf8, (int)size, wide, n);
    wide[n] = L'\0';
    bool ok = clip_put(CF_UNICODETEXT, wide, ((size_t)n + 1) * sizeof(wchar_t));
    free(wide);
    return ok;
}

/* A 32-bit BGRA bottom-up DIBV5, which keeps the alpha channel. */
static bool clip_put_dib(const Framebuffer *fb)
{
    size_t row = (size_t)fb->width * 4;
    size_t total = sizeof(BITMAPV5HEADER) + row * (size_t)fb->height;
    uint8_t *buf = calloc(1, total);
    if (!buf)
        return false;
    BITMAPV5HEADER *h = (BITMAPV5HEADER *)buf;
    h->bV5Size = sizeof *h;
    h->bV5Width = fb->width;
    h->bV5Height = fb->height; /* positive: bottom-up */
    h->bV5Planes = 1;
    h->bV5BitCount = 32;
    h->bV5Compression = BI_BITFIELDS;
    h->bV5SizeImage = (DWORD)(row * (size_t)fb->height);
    h->bV5RedMask = 0x00FF0000;
    h->bV5GreenMask = 0x0000FF00;
    h->bV5BlueMask = 0x000000FF;
    h->bV5AlphaMask = 0xFF000000;
    h->bV5CSType = 0x73524742; /* LCS_sRGB */
    h->bV5Intent = LCS_GM_IMAGES;
    uint8_t *dst = buf + sizeof *h;
    for (int y = 0; y < fb->height; y++)
        memcpy(dst + row * (size_t)(fb->height - 1 - y), fb->pixels + (size_t)y * fb->stride, row);
    bool ok = clip_put(CF_DIBV5, buf, total);
    free(buf);
    return ok;
}

static int hexval(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* "file:///C:/dir/a%20b" -> wide path "C:\dir\a b". Returns the length, 0 on a line
   that is not a file URI. */
static size_t uri_to_path(const char *line, size_t len, wchar_t *out, size_t cap)
{
    if (len < 8 || _strnicmp(line, "file://", 7) != 0)
        return 0;
    const char *p = line + 7;
    len -= 7;
    if (len && *p != '/') /* file://host/path: skip the host */
    {
        while (len && *p != '/')
        {
            p++;
            len--;
        }
    }
    if (len >= 3 && p[0] == '/' && p[2] == ':') /* /C:/... */
    {
        p++;
        len--;
    }
    char tmp[MAX_PATH * 3];
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < sizeof tmp; i++)
    {
        if (p[i] == '%' && i + 2 < len + 0 && hexval(p[i + 1]) >= 0 && hexval(p[i + 2]) >= 0)
        {
            tmp[n++] = (char)(hexval(p[i + 1]) * 16 + hexval(p[i + 2]));
            i += 2;
        }
        else
            tmp[n++] = p[i] == '/' ? '\\' : p[i];
    }
    int w = MultiByteToWideChar(CP_UTF8, 0, tmp, (int)n, out, (int)cap - 1);
    if (w <= 0)
        return 0;
    out[w] = L'\0';
    return (size_t)w;
}

/* DROPFILES lives in shlobj.h, which is far bigger than this needs. */
typedef struct
{
    DWORD pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
} ZenDropFiles;

static bool clip_put_uris(const char *text, size_t size)
{
    size_t cap = size + 2, used = 0; /* wide chars never outnumber the UTF-8 bytes */
    wchar_t *list = malloc((cap + 1) * sizeof(wchar_t));
    if (!list)
        return false;
    size_t pos = 0;
    while (pos < size)
    {
        size_t end = pos;
        while (end < size && text[end] != '\r' && text[end] != '\n')
            end++;
        wchar_t path[MAX_PATH * 2];
        size_t n = end > pos ? uri_to_path(text + pos, end - pos, path, sizeof path / sizeof path[0]) : 0;
        if (n && used + n + 1 < cap)
        {
            memcpy(list + used, path, (n + 1) * sizeof(wchar_t));
            used += n + 1;
        }
        pos = end + 1;
    }
    bool ok = false;
    if (used)
    {
        list[used++] = L'\0'; /* double NUL ends the list */
        size_t bytes = sizeof(ZenDropFiles) + used * sizeof(wchar_t);
        uint8_t *buf = calloc(1, bytes);
        if (buf)
        {
            ZenDropFiles *d = (ZenDropFiles *)buf;
            d->pFiles = sizeof *d;
            d->fWide = TRUE;
            memcpy(buf + sizeof *d, list, used * sizeof(wchar_t));
            ok = clip_put(CF_HDROP, buf, bytes);
            free(buf);
        }
    }
    free(list);
    return ok;
}

bool backend_clipboard_set(const ClipboardItem *items, int count)
{
    if (!clip_open())
        return error_set("cannot open the clipboard (another program is using it)");
    EmptyClipboard();
    bool ok = true;
    for (int i = 0; i < count && ok; i++)
    {
        const ClipboardItem *it = &items[i];
        if (clip_mime_equal(it->mime, CLIPBOARD_TEXT))
            ok = clip_put_text((const char *)it->data, it->size);
        else if (clip_mime_equal(it->mime, CLIPBOARD_PNG))
        {
            ok = clip_put(clip_png_format(), it->data, it->size);
            Framebuffer fb;
            if (ok && png_decode(it->data, it->size, &fb))
            {
                clip_put_dib(&fb); /* the DIB is a courtesy for older programs; PNG is what counts */
                framebuffer_free(&fb);
            }
        }
        else if (clip_mime_equal(it->mime, CLIPBOARD_URIS))
            ok = clip_put_uris((const char *)it->data, it->size);
        else
        {
            UINT f = RegisterClipboardFormatA(it->mime);
            ok = f && clip_put(f, it->data, it->size);
        }
    }
    CloseClipboard();
    return ok || error_set("cannot put the data on the clipboard");
}

bool backend_clipboard_has(const char *mime)
{
    if (clip_mime_equal(mime, CLIPBOARD_TEXT))
        return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
    if (clip_mime_equal(mime, CLIPBOARD_PNG))
        return IsClipboardFormatAvailable(clip_png_format()) || IsClipboardFormatAvailable(CF_DIBV5) ||
               IsClipboardFormatAvailable(CF_DIB);
    if (clip_mime_equal(mime, CLIPBOARD_URIS))
        return IsClipboardFormatAvailable(CF_HDROP) != 0;
    UINT f = RegisterClipboardFormatA(mime);
    return f && IsClipboardFormatAvailable(f);
}

static void *clip_copy_out(HANDLE h, size_t *size)
{
    SIZE_T n = GlobalSize(h);
    const void *p = GlobalLock(h);
    if (!p)
        return NULL;
    uint8_t *copy = malloc(n + 1);
    if (copy)
    {
        memcpy(copy, p, n);
        copy[n] = 0;
        *size = n;
    }
    GlobalUnlock(h);
    return copy;
}

/* A DIB from another program (Paint, a screenshot tool) as a PNG. */
static void *clip_dib_as_png(size_t *size)
{
    HANDLE h = GetClipboardData(CF_DIBV5);
    if (!h)
        h = GetClipboardData(CF_DIB);
    const BITMAPINFOHEADER *bi = h ? GlobalLock(h) : NULL;
    if (!bi)
        return NULL;
    void *png = NULL;
    int w = bi->biWidth, hgt = bi->biHeight < 0 ? -bi->biHeight : bi->biHeight;
    bool top_down = bi->biHeight < 0;
    bool ok = bi->biPlanes == 1 && w > 0 && hgt > 0 &&
              (bi->biBitCount == 32 || bi->biBitCount == 24) &&
              (bi->biCompression == BI_RGB || bi->biCompression == BI_BITFIELDS);
    if (ok)
    {
        size_t header = bi->biSize;
        if (bi->biCompression == BI_BITFIELDS && bi->biSize == sizeof(BITMAPINFOHEADER))
            header += 12; /* the three masks follow a plain header */
        const uint8_t *bits = (const uint8_t *)bi + header + (size_t)bi->biClrUsed * 4;
        size_t bpp = bi->biBitCount / 8;
        size_t stride = (((size_t)w * bi->biBitCount + 31) / 32) * 4;
        Framebuffer fb;
        if (framebuffer_alloc(&fb, w, hgt))
        {
            bool any_alpha = false;
            for (int y = 0; y < hgt; y++)
            {
                const uint8_t *src = bits + stride * (size_t)(top_down ? y : hgt - 1 - y);
                for (int x = 0; x < w; x++)
                {
                    const uint8_t *px = src + (size_t)x * bpp;
                    uint32_t a = bpp == 4 ? px[3] : 255;
                    any_alpha |= a != 0;
                    fb.pixels[(size_t)y * w + x] = (a << 24) | ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | px[0];
                }
            }
            if (!any_alpha) /* a 32-bit BI_RGB DIB usually leaves alpha at zero: it means opaque */
                for (size_t i = 0; i < (size_t)w * hgt; i++)
                    fb.pixels[i] |= 0xFF000000u;
            png = png_encode(&fb, size);
            framebuffer_free(&fb);
        }
    }
    GlobalUnlock(h);
    return png;
}

static void *clip_hdrop_as_uris(size_t *size)
{
    HDROP drop = (HDROP)GetClipboardData(CF_HDROP);
    if (!drop)
        return NULL;
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    size_t cap = 1, used = 0;
    char *out = malloc(cap);
    if (!out)
        return NULL;
    for (UINT i = 0; i < n; i++)
    {
        wchar_t wide[MAX_PATH * 2];
        if (!DragQueryFileW(drop, i, wide, sizeof wide / sizeof wide[0]))
            continue;
        char utf8[MAX_PATH * 4];
        int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof utf8, NULL, NULL);
        if (len <= 1)
            continue;
        size_t need = used + 8 + (size_t)(len - 1) * 3 + 3;
        char *grown = realloc(out, need);
        if (!grown)
        {
            free(out);
            return NULL;
        }
        out = grown;
        memcpy(out + used, "file:///", 8);
        used += 8;
        for (int k = 0; k < len - 1; k++)
        {
            unsigned char c = (unsigned char)utf8[k];
            if (c == '\\')
                out[used++] = '/';
            else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                     c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':')
                out[used++] = (char)c;
            else
                used += (size_t)snprintf(out + used, 4, "%%%02X", c);
        }
        out[used++] = '\r';
        out[used++] = '\n';
    }
    out[used] = '\0';
    *size = used;
    return out;
}

void *backend_clipboard_get(const char *mime, size_t *size)
{
    if (!clip_open())
        return NULL;
    void *result = NULL;
    if (clip_mime_equal(mime, CLIPBOARD_TEXT))
    {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        const wchar_t *wide = h ? GlobalLock(h) : NULL;
        if (wide)
        {
            int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
            if (n > 0)
            {
                char *utf8 = malloc((size_t)n);
                if (utf8)
                {
                    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, n, NULL, NULL);
                    *size = (size_t)n - 1; /* the conversion included the terminator */
                    result = utf8;
                }
            }
            GlobalUnlock(h);
        }
    }
    else if (clip_mime_equal(mime, CLIPBOARD_PNG))
    {
        HANDLE h = GetClipboardData(clip_png_format());
        if (h)
        {
            result = clip_copy_out(h, size);
            if (result) /* the block may be rounded up: end the data at the IEND chunk */
            {
                const uint8_t *b = result;
                for (size_t i = *size; i >= 12; i--)
                    if (memcmp(b + i - 8, "IEND", 4) == 0)
                    {
                        *size = i + 4;
                        ((uint8_t *)result)[*size] = 0;
                        break;
                    }
            }
        }
        else
            result = clip_dib_as_png(size);
    }
    else if (clip_mime_equal(mime, CLIPBOARD_URIS))
        result = clip_hdrop_as_uris(size);
    else
    {
        UINT f = RegisterClipboardFormatA(mime);
        HANDLE h = f ? GetClipboardData(f) : NULL;
        if (h)
            result = clip_copy_out(h, size);
    }
    CloseClipboard();
    return result;
}
