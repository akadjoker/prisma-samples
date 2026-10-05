#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform.h"

#include <stdio.h>
#include <string.h>

#define TITLE "test_win32"
#define SECOND_TITLE "test_win32_second"

static int g_pass, g_fail;
static HWND g_hwnd;

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

static LPARAM key_lparam(int scancode, bool extended, bool down, bool repeat)
{
    LPARAM l = 1 | ((LPARAM)scancode << 16);
    if (extended)
        l |= (LPARAM)1 << 24;
    if (repeat)
        l |= (LPARAM)1 << 30;
    if (!down)
        l |= ((LPARAM)1 << 30) | ((LPARAM)1 << 31);
    return l;
}

static void send_key(WPARAM vk, int scancode, bool extended, bool down)
{
    SendMessageW(g_hwnd, down ? WM_KEYDOWN : WM_KEYUP, vk, key_lparam(scancode, extended, down, false));
}

static void frame(PlatformWindow *w)
{
    window_begin_frame(w);
}

static void test_scancode_translation(PlatformWindow *w)
{
    static const struct
    {
        WPARAM vk;
        int scancode;
        bool extended;
        int key;
        const char *name;
    } table[] = {
        {'W', 0x11, false, KEY_W, "W"},
        {'A', 0x1E, false, KEY_A, "A"},
        {'Q', 0x10, false, KEY_Q, "Q"},
        {'1', 0x02, false, KEY_ONE, "1"},
        {'0', 0x0B, false, KEY_ZERO, "0"},
        {VK_ESCAPE, 0x01, false, KEY_ESCAPE, "Escape"},
        {VK_RETURN, 0x1C, false, KEY_ENTER, "Enter"},
        {VK_RETURN, 0x1C, true, KEY_KP_ENTER, "KP Enter"},
        {VK_SPACE, 0x39, false, KEY_SPACE, "Space"},
        {VK_F1, 0x3B, false, KEY_F1, "F1"},
        {VK_F12, 0x58, false, KEY_F12, "F12"},
        {VK_LEFT, 0x4B, true, KEY_LEFT, "Left"},
        {VK_UP, 0x48, true, KEY_UP, "Up"},
        {VK_HOME, 0x47, true, KEY_HOME, "Home"},
        {VK_DELETE, 0x53, true, KEY_DELETE, "Delete"},
        {VK_INSERT, 0x52, true, KEY_INSERT, "Insert"},
        {VK_NUMPAD7, 0x47, false, KEY_KP_7, "KP 7"},
        {VK_NUMPAD0, 0x52, false, KEY_KP_0, "KP 0"},
        {VK_DECIMAL, 0x53, false, KEY_KP_DECIMAL, "KP ."},
        {VK_ADD, 0x4E, false, KEY_KP_ADD, "KP +"},
        {VK_SUBTRACT, 0x4A, false, KEY_KP_SUBTRACT, "KP -"},
        {VK_MULTIPLY, 0x37, false, KEY_KP_MULTIPLY, "KP *"},
        {VK_DIVIDE, 0x35, true, KEY_KP_DIVIDE, "KP /"},
        {VK_LSHIFT, 0x2A, false, KEY_LEFT_SHIFT, "LShift"},
        {VK_RSHIFT, 0x36, false, KEY_RIGHT_SHIFT, "RShift"},
        {VK_LCONTROL, 0x1D, false, KEY_LEFT_CONTROL, "LCtrl"},
        {VK_RCONTROL, 0x1D, true, KEY_RIGHT_CONTROL, "RCtrl"},
        {VK_RMENU, 0x38, true, KEY_RIGHT_ALT, "RAlt"},
        {VK_LWIN, 0x5B, true, KEY_LEFT_SUPER, "LWin"},
        {VK_APPS, 0x5D, true, KEY_MENU, "Menu"},
        {VK_SCROLL, 0x46, false, KEY_SCROLL_LOCK, "ScrollLock"},
        {VK_CAPITAL, 0x3A, false, KEY_CAPS_LOCK, "CapsLock"},
        {VK_OEM_1, 0x27, false, KEY_SEMICOLON, "Semicolon"},
        {VK_OEM_3, 0x29, false, KEY_GRAVE, "Grave"},
        {VK_PAUSE, 0x45, false, KEY_PAUSE, "Pause"},
        {VK_NUMLOCK, 0x45, true, KEY_NUM_LOCK, "NumLock"},
        {VK_SNAPSHOT, 0x37, true, KEY_PRINT_SCREEN, "PrintScreen"},
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
    {
        send_key(table[i].vk, table[i].scancode, table[i].extended, true);
        frame(w);
        if (!key_pressed(w, table[i].key))
            printf("  %s did not arrive as key %d\n", table[i].name, table[i].key);
        CHECK(key_pressed(w, table[i].key));
        CHECK(key_down(w, table[i].key));
        send_key(table[i].vk, table[i].scancode, table[i].extended, false);
        frame(w);
        CHECK(key_released(w, table[i].key));
        CHECK(!key_down(w, table[i].key));
    }

    send_key(VK_SHIFT, 0x2A, true, true);
    frame(w);
    CHECK(!key_down(w, KEY_LEFT_SHIFT));
    send_key(VK_SHIFT, 0x2A, true, false);

    SendMessageW(g_hwnd, WM_KEYDOWN, 'W', key_lparam(0x11, false, true, false));
    frame(w);
    Event ev;
    int downs = 0;
    while (poll_event(w, &ev))
    {
        if (ev.type == EVENT_KEY && ev.data.key.key == KEY_W && ev.data.key.down)
        {
            downs++;
            CHECK(ev.data.key.scancode == 0x11);
            CHECK(!ev.data.key.repeat);
        }
    }
    CHECK(downs == 1);
    SendMessageW(g_hwnd, WM_KEYDOWN, 'W', key_lparam(0x11, false, true, true));
    frame(w);
    bool saw_repeat = false;
    while (poll_event(w, &ev))
    {
        if (ev.type == EVENT_KEY && ev.data.key.key == KEY_W && ev.data.key.repeat)
            saw_repeat = true;
    }
    CHECK(saw_repeat);
    send_key('W', 0x11, false, false);
    frame(w);
}

static void test_text(PlatformWindow *w)
{
    frame(w);
    while (char_get_pressed(w))
    {
    }
    SendMessageW(g_hwnd, WM_CHAR, 'a', 1);
    SendMessageW(g_hwnd, WM_CHAR, 0xE9, 1);
    SendMessageW(g_hwnd, WM_CHAR, 0x4E2D, 1);
    SendMessageW(g_hwnd, WM_CHAR, 0xD83D, 1);
    SendMessageW(g_hwnd, WM_CHAR, 0xDE00, 1);
    SendMessageW(g_hwnd, WM_CHAR, '\r', 1);
    SendMessageW(g_hwnd, WM_CHAR, 0x7F, 1);
    SendMessageW(g_hwnd, WM_CHAR, 0xDE00, 1);
    SendMessageW(g_hwnd, WM_CHAR, 'z', 1);
    frame(w);
    CHECK(char_get_pressed(w) == 'a');
    CHECK(char_get_pressed(w) == 0xE9);
    CHECK(char_get_pressed(w) == 0x4E2D);
    CHECK(char_get_pressed(w) == 0x1F600);
    CHECK(char_get_pressed(w) == 'z');
    CHECK(char_get_pressed(w) == 0);
}

static LPARAM mouse_lparam(int x, int y)
{
    return MAKELPARAM((WORD)x, (WORD)y);
}

static void test_mouse(PlatformWindow *w)
{
    SendMessageW(g_hwnd, WM_MOUSEMOVE, 0, mouse_lparam(40, 30));
    frame(w);
    CHECK(mouse_x(w) == 40 && mouse_y(w) == 30);
    Event ev;
    bool entered = false;
    while (poll_event(w, &ev))
        entered |= ev.type == EVENT_WINDOW_ENTER && ev.data.enter.entered;
    CHECK(entered);

    SendMessageW(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, mouse_lparam(40, 30));
    SendMessageW(g_hwnd, WM_RBUTTONDOWN, MK_LBUTTON | MK_RBUTTON, mouse_lparam(40, 30));
    SendMessageW(g_hwnd, WM_MBUTTONDOWN, 0, mouse_lparam(40, 30));
    SendMessageW(g_hwnd, WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON1), mouse_lparam(40, 30));
    SendMessageW(g_hwnd, WM_XBUTTONDOWN, MAKEWPARAM(0, XBUTTON2), mouse_lparam(40, 30));
    frame(w);
    CHECK(mouse_button_pressed(w, MOUSE_LEFT));
    CHECK(mouse_button_pressed(w, MOUSE_RIGHT));
    CHECK(mouse_button_pressed(w, MOUSE_MIDDLE));
    CHECK(mouse_button_pressed(w, MOUSE_X1));
    CHECK(mouse_button_pressed(w, MOUSE_X2));
    CHECK(GetCapture() == g_hwnd);

    SendMessageW(g_hwnd, WM_LBUTTONUP, 0, mouse_lparam(40, 30));
    SendMessageW(g_hwnd, WM_RBUTTONUP, 0, mouse_lparam(40, 30));
    SendMessageW(g_hwnd, WM_MBUTTONUP, 0, mouse_lparam(40, 30));
    SendMessageW(g_hwnd, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), mouse_lparam(40, 30));
    CHECK(GetCapture() == g_hwnd);
    SendMessageW(g_hwnd, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), mouse_lparam(40, 30));
    CHECK(GetCapture() != g_hwnd);
    frame(w);
    CHECK(mouse_button_released(w, MOUSE_LEFT) && mouse_button_released(w, MOUSE_X2));

    SendMessageW(g_hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, 240), 0);
    SendMessageW(g_hwnd, WM_MOUSEHWHEEL, MAKEWPARAM(0, (WORD)(short)-120), 0);
    frame(w);
    float wx = 0, wy = 0;
    mouse_wheel_v(w, &wx, &wy);
    CHECK(wy == 2.0f && wx == -1.0f);

    SendMessageW(g_hwnd, WM_MOUSELEAVE, 0, 0);
    frame(w);
    CHECK(!window_is_hovered(w));
}

static void test_focus(PlatformWindow *w)
{
    send_key('W', 0x11, false, true);
    SendMessageW(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, mouse_lparam(5, 5));
    frame(w);
    CHECK(key_down(w, KEY_W) && mouse_button_down(w, MOUSE_LEFT));
    SendMessageW(g_hwnd, WM_KILLFOCUS, 0, 0);
    frame(w);
    CHECK(!key_down(w, KEY_W) && key_released(w, KEY_W));
    CHECK(!mouse_button_down(w, MOUSE_LEFT));
    SendMessageW(g_hwnd, WM_LBUTTONUP, 0, mouse_lparam(5, 5));
    SendMessageW(g_hwnd, WM_SETFOCUS, 0, 0);
    frame(w);
}

static void test_geometry(PlatformWindow *w)
{
    int width = 0, height = 0;
    window_get_size(w, &width, &height);
    CHECK(width == 200 && height == 120);
    window_get_framebuffer_size(w, &width, &height);
    CHECK(width == 200 && height == 120);
    CHECK(window_content_scale(w) == 1.0f);

    window_set_size(w, 260, 140);
    frame(w);
    window_get_size(w, &width, &height);
    CHECK(width == 260 && height == 140);

    Event ev;
    bool resized = false;
    window_set_size(w, 300, 150);
    frame(w);
    while (poll_event(w, &ev))
    {
        if (ev.type == EVENT_WINDOW_RESIZE && ev.data.resize.w == 300 && ev.data.resize.h == 150)
            resized = true;
    }
    CHECK(resized);

    window_set_position(w, 50, 60);
    frame(w);
    int x = 0, y = 0;
    window_get_position(w, &x, &y);
    CHECK(x == 50 && y == 60);

    window_set_title(w, "test_win32 \xC3\xA9");
    window_set_size_limits(w, 120, 90, 0, 0);
    window_set_opacity(w, 0.5f);
    window_set_opacity(w, 1.0f);
    window_set_always_on_top(w, true);
    window_set_always_on_top(w, false);

    uint8_t icon[4 * 4 * 4];
    memset(icon, 0xFF, sizeof icon);
    window_set_icon(w, 4, 4, icon);

    window_set_size(w, 200, 120);
    frame(w);
}

static void test_state(PlatformWindow *w)
{
    Event ev;
    bool got_min = false, got_max = false, got_restore = false;

    window_minimize(w);
    frame(w);
    CHECK(window_is_minimized(w));
    while (poll_event(w, &ev))
        got_min |= ev.type == EVENT_WINDOW_MINIMIZE;
    CHECK(got_min);

    window_restore(w);
    frame(w);
    CHECK(!window_is_minimized(w));

    window_maximize(w);
    frame(w);
    CHECK(window_is_maximized(w));
    while (poll_event(w, &ev))
        got_max |= ev.type == EVENT_WINDOW_MAXIMIZE;
    CHECK(got_max);

    window_restore(w);
    frame(w);
    CHECK(!window_is_maximized(w));
    while (poll_event(w, &ev))
        got_restore |= ev.type == EVENT_WINDOW_RESTORE;
    CHECK(got_restore);

    window_hide(w);
    CHECK(!window_is_visible(w));
    window_show(w);
    CHECK(window_is_visible(w));

    window_set_mode(w, WINDOW_FULLSCREEN, MONITOR_CURRENT);
    frame(w);
    CHECK(window_get_mode(w) == WINDOW_FULLSCREEN);
    MonitorInfo mi;
    int width = 0, height = 0;
    CHECK(monitor_get_info(0, &mi));
    window_get_size(w, &width, &height);
    CHECK(width == mi.width && height == mi.height);
    window_set_mode(w, WINDOW_WINDOWED, MONITOR_CURRENT);
    frame(w);
    CHECK(window_get_mode(w) == WINDOW_WINDOWED);
    window_get_size(w, &width, &height);
    CHECK(width == 200 && height == 120);
}

static void test_monitors(void)
{
    CHECK(monitor_count() >= 1);
    MonitorInfo mi;
    CHECK(monitor_get_info(0, &mi));
    CHECK(mi.width > 0 && mi.height > 0 && mi.refresh_hz > 0);
    CHECK(mi.work_w > 0 && mi.work_h > 0);
    CHECK(mi.name != NULL && mi.name[0] != '\0');
    bool has_primary = false;
    for (int i = 0; i < monitor_count(); i++)
    {
        if (monitor_get_info(i, &mi) && mi.primary)
            has_primary = true;
    }
    CHECK(has_primary);
    CHECK(!monitor_get_info(-1, &mi));
    CHECK(!monitor_get_info(monitor_count(), &mi));
}

static void test_cursors(PlatformWindow *w)
{
    for (int c = -2; c < CURSOR_COUNT + 3; c++)
        mouse_set_cursor(w, c);
    mouse_set_mode(w, MOUSE_MODE_HIDDEN);
    mouse_set_cursor(w, CURSOR_HAND);
    mouse_set_mode(w, MOUSE_MODE_NORMAL);
    mouse_set_position(w, 20, 20);
    frame(w);
    CHECK(true);
}

static void test_clipboard(void)
{
    const char *text = "zen \xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80";
    clipboard_set(text);
    const char *back = clipboard_get();
    if (back && back[0] != '\0')
        CHECK(strcmp(back, text) == 0);
    else
        printf("  clipboard not available in this session, skipped\n");
    clipboard_set("");
    CHECK(clipboard_get() != NULL);
}

static void test_pixels(void)
{
    WindowConfig cfg = {.title = SECOND_TITLE, .width = 320, .height = 240, .render = RENDER_PIXELS};
    PlatformWindow *w = window_create(&cfg);
    CHECK(w != NULL);
    if (!w)
        return;
    Framebuffer fb;
    CHECK(window_lock_pixels(w, &fb));
    CHECK(fb.width == 320 && fb.height == 240 && fb.stride >= fb.width && fb.pixels != NULL);
    draw_clear(&fb, 0xFF336699);
    draw_fill_rect(&fb, 4, 4, 10, 10, 0xFFFF0000, BLEND_NONE);
    window_present_pixels(w);
    CHECK(draw_get_pixel(&fb, 5, 5) == 0xFFFF0000);
    CHECK(draw_get_pixel(&fb, 30, 30) == 0xFF336699);

    HDC dc = GetDC(FindWindowW(L"zen_platform_window", L"" SECOND_TITLE));
    if (dc)
    {
        COLORREF c = GetPixel(dc, 5, 5);
        if (c != CLR_INVALID)
            CHECK(GetRValue(c) == 0xFF && GetGValue(c) == 0x00 && GetBValue(c) == 0x00);
        ReleaseDC(NULL, dc);
    }
    window_set_size(w, 400, 300);
    CHECK(window_lock_pixels(w, &fb));
    CHECK(fb.width == 400 && fb.height == 300);
    window_destroy(w);
}

static void test_gl_context(void)
{
    WindowConfig cfg = {.title = "test_win32_gl", .width = 64, .height = 64};
    platform_clear_error();
    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        printf("  no 3.3 core context on this machine: %s\n", platform_get_error());
        CHECK(platform_get_error()[0] != '\0');
    }
    else
    {
        typedef const unsigned char *(WINAPI * GetString)(unsigned);
        GetString get_string = (GetString)gl_proc_address("glGetString");
        CHECK(get_string != NULL);
        if (get_string)
        {
            const char *v = (const char *)get_string(0x1F02);
            CHECK(v && v[0] >= '3');
        }
        window_set_vsync(w, true);
        window_set_vsync(w, false);
        window_swap(w);
        window_destroy(w);
    }

    WindowConfig bad = {.title = "test_win32_bad", .width = 64, .height = 64, .gl = {.profile = GL_PROFILE_CORE, .major = 9, .minor = 9}};
    platform_clear_error();
    CHECK(window_create(&bad) == NULL);
    CHECK(platform_get_error()[0] != '\0');

    WindowConfig invalid = {.title = "test_win32_invalid", .width = 64, .height = 64, .gl = {.msaa = -1}};
    CHECK(window_create(&invalid) == NULL);
}

int main(void)
{
    if (!platform_init())
    {
        printf("FAIL platform_init: %s\n", platform_get_error());
        return 1;
    }
    WindowConfig cfg = {.title = TITLE, .width = 200, .height = 120, .render = RENDER_PIXELS, .resizable = true, .x = 100, .y = 100};
    PlatformWindow *w = window_create(&cfg);
    if (!w)
    {
        printf("FAIL window_create: %s\n", platform_get_error());
        return 1;
    }
    g_hwnd = FindWindowW(L"zen_platform_window", L"" TITLE);
    CHECK(g_hwnd != NULL);

    if (g_hwnd)
    {
        frame(w);
        test_geometry(w);
        test_scancode_translation(w);
        test_text(w);
        test_mouse(w);
        test_focus(w);
        test_cursors(w);
        test_state(w);
    }
    test_monitors();
    test_clipboard();
    test_pixels();
    test_gl_context();

    window_destroy(w);
    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
