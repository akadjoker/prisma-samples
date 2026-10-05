/*
 * test_input_logic.c - validates the portable core against backend_fake, with no
 * window and no display. This is where the input logic is pinned down before any
 * real backend exists.
 */
#include "platform.h"
#include "backend_fake.h"

#include <stdio.h>

static int g_pass, g_fail;

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

static PlatformWindow *make_window(void)
{
    WindowConfig cfg = {.title = "test", .width = 800, .height = 600, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    return window_create(&cfg);
}

/* key_pressed fires only on the transition; key_down persists; release edges. */
static void test_key_edges(PlatformWindow *w)
{
    fake_key(w, KEY_A, true, false);
    window_begin_frame(w);
    CHECK(key_down(w, KEY_A));
    CHECK(key_pressed(w, KEY_A));

    window_begin_frame(w);         /* no input this frame */
    CHECK(key_down(w, KEY_A));     /* still held */
    CHECK(!key_pressed(w, KEY_A)); /* edge gone */

    fake_key(w, KEY_A, false, false);
    window_begin_frame(w);
    CHECK(!key_down(w, KEY_A));
    CHECK(key_released(w, KEY_A));

    window_begin_frame(w);
    CHECK(!key_released(w, KEY_A));
}

/* GetCharPressed-style queue returns Unicode in order and then drains. */
static void test_char_queue(PlatformWindow *w)
{
    fake_inject_char(w, 'z');
    fake_inject_char(w, 0x00E9); /* e-acute, a non-ASCII codepoint */
    fake_inject_char(w, 'X');
    window_begin_frame(w);
    CHECK(char_get_pressed(w) == 'z');
    CHECK(char_get_pressed(w) == 0x00E9);
    CHECK(char_get_pressed(w) == 'X');
    CHECK(char_get_pressed(w) == 0); /* drained */
}

/* poll_event iterates the frame's events without clearing the cooked state. */
static void test_poll_keeps_state(PlatformWindow *w)
{
    fake_key(w, KEY_B, true, false);
    fake_mouse_move(w, 10, 20);
    window_begin_frame(w);

    bool saw_key = false, saw_move = false;
    Event e;
    while (poll_event(w, &e))
    {
        if (e.type == EVENT_KEY)
            saw_key = true;
        if (e.type == EVENT_MOUSE_MOVE)
            saw_move = true;
    }
    CHECK(saw_key);
    CHECK(saw_move);
    CHECK(key_down(w, KEY_B)); /* state survived the drain */
    CHECK(mouse_x(w) == 10 && mouse_y(w) == 20);
}

/* A touch keeps its id and slot across down -> move -> up; count tracks it. */
static void test_touch_tracking(PlatformWindow *w)
{
    fake_touch(w, 7, 1.0f, 2.0f, TOUCH_DOWN);
    window_begin_frame(w);
    CHECK(touch_count(w) == 1);
    CHECK(touch_id(w, 0) == 7);

    fake_touch(w, 7, 5.0f, 6.0f, TOUCH_MOVE);
    window_begin_frame(w);
    CHECK(touch_count(w) == 1);
    CHECK(touch_id(w, 0) == 7);
    float tx, ty;
    touch_position(w, 0, &tx, &ty);
    CHECK(tx == 5.0f && ty == 6.0f);

    fake_touch(w, 7, 5.0f, 6.0f, TOUCH_UP);
    window_begin_frame(w);
    CHECK(touch_count(w) == 0);
}

/* RESIZE (screen coords) and FB_RESIZE (pixels) are distinct events. */
static void test_resize_distinct(PlatformWindow *w)
{
    fake_resize(w, 800, 600);
    fake_fb_resize(w, 1600, 1200);
    window_begin_frame(w);

    int logical_w = 0, fb_w = 0;
    Event e;
    while (poll_event(w, &e))
    {
        if (e.type == EVENT_WINDOW_RESIZE)
            logical_w = e.data.resize.w;
        if (e.type == EVENT_WINDOW_FB_RESIZE)
            fb_w = e.data.resize.w;
    }
    CHECK(logical_w == 800);
    CHECK(fb_w == 1600);
    CHECK(logical_w != fb_w);
}

/* consumed lives on the event copy; it never touches the cooked state. */
static void test_consumed_isolated(PlatformWindow *w)
{
    fake_key(w, KEY_C, true, false);
    window_begin_frame(w);

    Event e;
    bool got = poll_event(w, &e);
    CHECK(got);
    e.consumed = true;         /* a higher layer would do this */
    CHECK(key_down(w, KEY_C)); /* state read elsewhere is unaffected */
}

/* mouse delta is measured against the previous frame's position. */
static void test_mouse_delta(PlatformWindow *w)
{
    fake_mouse_move(w, 10, 10);
    window_begin_frame(w);

    fake_mouse_move(w, 30, 25);
    window_begin_frame(w);
    int dx, dy;
    mouse_delta(w, &dx, &dy);
    CHECK(dx == 20 && dy == 15);
}

static void test_key_mods(PlatformWindow *w)
{
    window_begin_frame(w);
    CHECK(key_mods(w) == 0);

    fake_key(w, KEY_LEFT_SHIFT, true, false);
    window_begin_frame(w);
    CHECK(key_mods(w) == KEYMOD_SHIFT);

    fake_key(w, KEY_RIGHT_CONTROL, true, false);
    fake_key(w, KEY_LEFT_ALT, true, false);
    fake_key(w, KEY_RIGHT_SUPER, true, false);
    window_begin_frame(w);
    CHECK(key_mods(w) == (KEYMOD_SHIFT | KEYMOD_CTRL | KEYMOD_ALT | KEYMOD_SUPER));

    fake_key(w, KEY_LEFT_SHIFT, false, false);
    window_begin_frame(w);
    CHECK(key_mods(w) == (KEYMOD_CTRL | KEYMOD_ALT | KEYMOD_SUPER));

    fake_key(w, KEY_A, true, false);
    window_begin_frame(w);
    CHECK(key_mods(w) == (KEYMOD_CTRL | KEYMOD_ALT | KEYMOD_SUPER));
}

static void test_focus_loss_releases(PlatformWindow *w)
{
    fake_key(w, KEY_LEFT_CONTROL, true, false);
    fake_key(w, KEY_W, true, false);
    fake_mouse_button(w, MOUSE_LEFT, true);
    window_begin_frame(w);
    CHECK(key_down(w, KEY_W) && key_mods(w) == KEYMOD_CTRL && mouse_button_down(w, MOUSE_LEFT));

    Event gained = {.type = EVENT_WINDOW_FOCUS};
    gained.data.focus.gained = true;
    fake_inject_event(w, &gained);
    window_begin_frame(w);
    CHECK(key_down(w, KEY_W) && mouse_button_down(w, MOUSE_LEFT));

    Event lost = {.type = EVENT_WINDOW_FOCUS};
    lost.data.focus.gained = false;
    fake_inject_event(w, &lost);
    window_begin_frame(w);
    CHECK(!key_down(w, KEY_W) && key_released(w, KEY_W));
    CHECK(!key_down(w, KEY_LEFT_CONTROL) && key_released(w, KEY_LEFT_CONTROL));
    CHECK(key_mods(w) == 0);
    CHECK(!mouse_button_down(w, MOUSE_LEFT) && mouse_button_released(w, MOUSE_LEFT));

    window_begin_frame(w);
    CHECK(!key_released(w, KEY_W) && !mouse_button_released(w, MOUSE_LEFT));
}

static void test_keypad_keys(PlatformWindow *w)
{
    static const int keys[] = {KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7,
                               KEY_KP_8, KEY_KP_9, KEY_KP_DECIMAL, KEY_KP_DIVIDE, KEY_KP_MULTIPLY,
                               KEY_KP_SUBTRACT, KEY_KP_ADD, KEY_KP_ENTER, KEY_KP_EQUAL, KEY_MENU, KEY_SCROLL_LOCK};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
    {
        CHECK(keys[i] > 0 && keys[i] < KEY_MAX);
        for (size_t j = i + 1; j < sizeof keys / sizeof keys[0]; j++)
            CHECK(keys[i] != keys[j]);
        fake_key(w, keys[i], true, false);
        window_begin_frame(w);
        CHECK(key_pressed(w, keys[i]) && key_down(w, keys[i]));
        fake_key(w, keys[i], false, false);
        window_begin_frame(w);
        CHECK(key_released(w, keys[i]) && !key_down(w, keys[i]));
    }
}

static void test_sleep(void)
{
    uint64_t t0 = time_nanos();
    time_sleep(25);
    uint64_t dt = time_nanos() - t0;
    CHECK(dt >= 25ull * 1000000ull);
    CHECK(dt < 2000ull * 1000000ull);

    t0 = time_nanos();
    time_sleep(0);
    CHECK(time_nanos() - t0 < 500ull * 1000000ull);

    t0 = time_nanos();
    time_sleep(1100);
    CHECK(time_nanos() - t0 >= 1100ull * 1000000ull);
}

int main(void)
{
    if (!platform_init())
    {
        printf("platform_init failed\n");
        return 1;
    }

    /* A fresh window per test keeps the input state isolated. */
    PlatformWindow *w;
    w = make_window();
    test_key_edges(w);
    window_destroy(w);
    w = make_window();
    test_char_queue(w);
    window_destroy(w);
    w = make_window();
    test_poll_keeps_state(w);
    window_destroy(w);
    w = make_window();
    test_touch_tracking(w);
    window_destroy(w);
    w = make_window();
    test_resize_distinct(w);
    window_destroy(w);
    w = make_window();
    test_consumed_isolated(w);
    window_destroy(w);
    w = make_window();
    test_mouse_delta(w);
    window_destroy(w);
    w = make_window();
    test_key_mods(w);
    window_destroy(w);
    w = make_window();
    test_focus_loss_releases(w);
    window_destroy(w);
    w = make_window();
    test_keypad_keys(w);
    window_destroy(w);
    test_sleep();

    platform_shutdown();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
