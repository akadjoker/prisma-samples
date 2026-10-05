/*
 * test_gestures.c - the gesture recognizer, driven with synthetic timestamps
 * (gesture.c has no clock), plus one pass through the core with the fake backend.
 */
#include "platform.h"
#include "backend_fake.h"
#include "gesture_internal.h"

#include <stdio.h>

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

#define W 800.0f
#define H 600.0f

static void touch1(GestureState *g, GestureAction a, float x, float y, double t)
{
    GVec2 p = {x, y};
    gesture_feed(g, a, 1, &p, W, H, t);
}

static void touch2(GestureState *g, GestureAction a, GVec2 p0, GVec2 p1, double t)
{
    GVec2 p[2] = {p0, p1};
    gesture_feed(g, a, 2, p, W, H, t);
}

static void test_tap_then_hold(void)
{
    GestureState g;
    gesture_state_init(&g);
    touch1(&g, GESTURE_ACTION_DOWN, 100, 100, 1.0);
    CHECK(gesture_state_detected(&g) == GESTURE_TAP);
    gesture_update(&g, 1.016);
    CHECK(gesture_state_detected(&g) == GESTURE_HOLD);
    CHECK(gesture_state_hold_duration(&g, 1.516) > 0.49f);
    touch1(&g, GESTURE_ACTION_UP, 100, 100, 1.6);
    CHECK(gesture_state_detected(&g) == GESTURE_NONE);
}

static void test_double_tap(void)
{
    GestureState g;
    gesture_state_init(&g);
    touch1(&g, GESTURE_ACTION_DOWN, 100, 100, 1.0);
    touch1(&g, GESTURE_ACTION_UP, 100, 100, 1.05);
    CHECK(gesture_state_detected(&g) == GESTURE_NONE);
    touch1(&g, GESTURE_ACTION_DOWN, 102, 101, 1.15);
    CHECK(gesture_state_detected(&g) == GESTURE_DOUBLETAP);

    /* too slow: the second tap is a plain tap */
    gesture_state_init(&g);
    touch1(&g, GESTURE_ACTION_DOWN, 100, 100, 1.0);
    touch1(&g, GESTURE_ACTION_UP, 100, 100, 1.05);
    touch1(&g, GESTURE_ACTION_DOWN, 100, 100, 2.0);
    CHECK(gesture_state_detected(&g) == GESTURE_TAP);

    /* too far: also a plain tap */
    gesture_state_init(&g);
    touch1(&g, GESTURE_ACTION_DOWN, 100, 100, 1.0);
    touch1(&g, GESTURE_ACTION_UP, 100, 100, 1.05);
    touch1(&g, GESTURE_ACTION_DOWN, 500, 400, 1.1);
    CHECK(gesture_state_detected(&g) == GESTURE_TAP);
}

static void test_swipes(void)
{
    struct
    {
        float dx, dy;
        unsigned want;
    } c[] = {{300, 0, GESTURE_SWIPE_RIGHT},
             {-300, 0, GESTURE_SWIPE_LEFT},
             {0, -300, GESTURE_SWIPE_UP}, /* y grows down */
             {0, 300, GESTURE_SWIPE_DOWN}};
    for (int i = 0; i < 4; i++)
    {
        GestureState g;
        gesture_state_init(&g);
        touch1(&g, GESTURE_ACTION_DOWN, 400, 300, 1.0);
        touch1(&g, GESTURE_ACTION_UP, 400 + c[i].dx, 300 + c[i].dy, 1.1);
        CHECK(gesture_state_detected(&g) == c[i].want);
        gesture_update(&g, 1.116);
        CHECK(gesture_state_detected(&g) == GESTURE_NONE); /* one frame only */
    }

    /* the angle is read as on paper: up-right is 45 degrees */
    GestureState d;
    gesture_state_init(&d);
    touch1(&d, GESTURE_ACTION_DOWN, 400, 300, 1.0);
    touch1(&d, GESTURE_ACTION_UP, 700, 0, 1.1);
    CHECK(d.drag_angle > 44.9f && d.drag_angle < 45.1f);

    /* slow movement is no swipe */
    GestureState g;
    gesture_state_init(&g);
    touch1(&g, GESTURE_ACTION_DOWN, 400, 300, 1.0);
    touch1(&g, GESTURE_ACTION_UP, 420, 300, 3.0);
    CHECK(gesture_state_detected(&g) == GESTURE_NONE);
}

static void test_drag(void)
{
    GestureState g;
    gesture_state_init(&g);
    touch1(&g, GESTURE_ACTION_DOWN, 100, 100, 1.0);
    gesture_update(&g, 1.016);                      /* TAP -> HOLD */
    touch1(&g, GESTURE_ACTION_MOVE, 130, 110, 1.5); /* held longer than the drag timeout */
    CHECK(gesture_state_detected(&g) == GESTURE_DRAG);
    CHECK(g.drag_vector.x == 30.0f && g.drag_vector.y == 10.0f);
    touch1(&g, GESTURE_ACTION_UP, 130, 110, 1.6);
    CHECK(gesture_state_detected(&g) == GESTURE_NONE); /* a drag is not a swipe */
}

static void test_pinch(void)
{
    GestureState g;
    gesture_state_init(&g);
    touch2(&g, GESTURE_ACTION_DOWN, (GVec2){300, 300}, (GVec2){500, 300}, 1.0);
    CHECK(gesture_state_detected(&g) == GESTURE_HOLD);
    CHECK(g.pinch_vector.x == 200.0f && g.pinch_vector.y == 0.0f);

    touch2(&g, GESTURE_ACTION_MOVE, (GVec2){250, 300}, (GVec2){550, 300}, 1.1);
    CHECK(gesture_state_detected(&g) == GESTURE_PINCH_OUT);
    CHECK(g.pinch_vector.x == 300.0f);

    /* the reference points stay at the touch-down, so closing below it is a pinch in */
    touch2(&g, GESTURE_ACTION_MOVE, (GVec2){350, 300}, (GVec2){450, 300}, 1.2);
    CHECK(gesture_state_detected(&g) == GESTURE_PINCH_IN);

    touch2(&g, GESTURE_ACTION_UP, (GVec2){350, 300}, (GVec2){450, 300}, 1.3);
    CHECK(gesture_state_detected(&g) == GESTURE_NONE);
    CHECK(g.pinch_vector.x == 0.0f);
}

static void test_enabled_mask_and_cancel(void)
{
    GestureState g;
    gesture_state_init(&g);
    g.enabled = GESTURE_SWIPE_LEFT;
    touch1(&g, GESTURE_ACTION_DOWN, 100, 100, 1.0);
    CHECK(gesture_state_detected(&g) == GESTURE_NONE); /* TAP is masked out */

    gesture_state_init(&g);
    touch1(&g, GESTURE_ACTION_DOWN, 100, 100, 1.0);
    gesture_feed(&g, GESTURE_ACTION_CANCEL, 0, NULL, W, H, 1.1);
    CHECK(gesture_state_detected(&g) == GESTURE_NONE);
}

/* ---- through the core: events -> touch slots -> gestures ---- */

static void test_core_touch(void)
{
    WindowConfig cfg = {.title = "t", .width = 800, .height = 600, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    PlatformWindow *w = window_create(&cfg);
    CHECK(w != NULL);
    if (!w)
        return;

    fake_touch(w, 7, 100, 100, TOUCH_DOWN);
    window_begin_frame(w);
    CHECK(touch_count(w) == 1);
    CHECK(gesture_is_detected(w, GESTURE_TAP));

    window_begin_frame(w); /* next frame: the tap became a hold */
    CHECK(gesture_is_detected(w, GESTURE_HOLD));
    CHECK(gesture_hold_duration(w) >= 0.0f);

    gesture_set_enabled(w, GESTURE_SWIPE_RIGHT);
    CHECK(gesture_detected(w) == GESTURE_NONE);
    gesture_set_enabled(w, GESTURE_ALL);

    fake_touch(w, 7, 100, 100, TOUCH_UP);
    window_begin_frame(w);
    CHECK(touch_count(w) == 0);
    CHECK(gesture_detected(w) == GESTURE_NONE);

    /* a second finger arrives: two points, a hold, then a pinch out */
    fake_touch(w, 1, 300, 300, TOUCH_DOWN);
    fake_touch(w, 2, 500, 300, TOUCH_DOWN);
    window_begin_frame(w);
    CHECK(touch_count(w) == 2);
    fake_touch(w, 1, 200, 300, TOUCH_MOVE);
    fake_touch(w, 2, 600, 300, TOUCH_MOVE);
    window_begin_frame(w);
    CHECK(gesture_is_detected(w, GESTURE_PINCH_OUT));
    float px, py;
    gesture_pinch_vector(w, &px, &py);
    CHECK(px == 400.0f && py == 0.0f);
    fake_touch(w, 2, 600, 300, TOUCH_UP);
    window_begin_frame(w);
    CHECK(touch_count(w) == 1);
    CHECK(gesture_detected(w) == GESTURE_NONE);

    window_destroy(w);
}

static void test_mouse_emulation(void)
{
    WindowConfig cfg = {.title = "t", .width = 800, .height = 600, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    PlatformWindow *w = window_create(&cfg);
    CHECK(w != NULL);
    if (!w)
        return;

    /* off by default */
    fake_mouse_move(w, 50, 50);
    fake_mouse_button(w, MOUSE_LEFT, true);
    window_begin_frame(w);
    CHECK(gesture_detected(w) == GESTURE_NONE);
    fake_mouse_button(w, MOUSE_LEFT, false);
    window_begin_frame(w);

    touch_set_mouse_emulation(w, true);
    fake_mouse_move(w, 50, 50);
    fake_mouse_button(w, MOUSE_LEFT, true);
    window_begin_frame(w);
    CHECK(touch_count(w) == 1);
    CHECK(touch_id(w, 0) == TOUCH_ID_MOUSE);
    CHECK(touch_x(w, 0) == 50 && touch_y(w, 0) == 50);
    CHECK(gesture_is_detected(w, GESTURE_TAP));

    /* the finger follows the pointer while the button is held, and a touch event is in the stream */
    fake_mouse_move(w, 80, 90);
    window_begin_frame(w);
    CHECK(touch_x(w, 0) == 80 && touch_y(w, 0) == 90);
    bool saw_touch = false;
    Event ev;
    while (poll_event(w, &ev))
        if (ev.type == EVENT_TOUCH && ev.data.touch.id == TOUCH_ID_MOUSE && ev.data.touch.phase == TOUCH_MOVE)
            saw_touch = true;
    CHECK(saw_touch);

    /* lifting after a fast 50 px move is a swipe (down: y grows downwards) */
    fake_mouse_button(w, MOUSE_LEFT, false);
    window_begin_frame(w);
    CHECK(touch_count(w) == 0);
    CHECK(gesture_detected(w) == GESTURE_SWIPE_DOWN);
    window_begin_frame(w);
    CHECK(gesture_detected(w) == GESTURE_NONE);

    /* switching it off while the button is down lifts the finger */
    fake_mouse_button(w, MOUSE_LEFT, true);
    window_begin_frame(w);
    CHECK(touch_count(w) == 1);
    touch_set_mouse_emulation(w, false);
    window_begin_frame(w);
    CHECK(touch_count(w) == 0);
    fake_mouse_button(w, MOUSE_LEFT, false);
    window_begin_frame(w);
    CHECK(touch_count(w) == 0);

    window_destroy(w);
}

int main(void)
{
    if (!platform_init())
        return 1;
    test_tap_then_hold();
    test_double_tap();
    test_swipes();
    test_drag();
    test_pinch();
    test_enabled_mask_and_cancel();
    test_core_touch();
    test_mouse_emulation();
    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
