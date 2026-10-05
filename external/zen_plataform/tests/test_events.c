/*
 * test_events.c - the event hook, text as an event, lock-key state and mouse
 * capture, over the fake backend.
 */
#include "platform.h"
#include "backend_fake.h"
#include "gamepad_internal.h"

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

static PlatformWindow *make(void)
{
    WindowConfig cfg = {.title = "t", .width = 320, .height = 200, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    return window_create(&cfg);
}

typedef struct
{
    EventType types[32];
    int n;
    PlatformWindow *seen_window;
    void *seen_user;
    bool key_down_inside_hook; /* what key_down said while the hook ran for a KEY down */
} Seen;

static void hook(PlatformWindow *w, const Event *e, void *user)
{
    Seen *s = user;
    if (s->n < 32)
        s->types[s->n++] = e->type;
    s->seen_window = w;
    s->seen_user = user;
    if (e->type == EVENT_KEY && e->data.key.down)
        s->key_down_inside_hook = key_down(w, e->data.key.key);
}

static void test_hook(PlatformWindow *w)
{
    Seen seen = {0};
    window_set_event_hook(w, hook, &seen);

    fake_key(w, KEY_A, true, false);
    fake_mouse_move(w, 10, 20);
    fake_inject_char(w, 'a');
    fake_mouse_button(w, MOUSE_LEFT, true);
    fake_wheel(w, 0, 1);
    fake_touch(w, 3, 5, 6, TOUCH_DOWN);
    fake_resize(w, 400, 300);
    window_begin_frame(w);

    CHECK(seen.n == 7);
    EventType want[] = {EVENT_KEY, EVENT_MOUSE_MOVE, EVENT_CHAR, EVENT_MOUSE_BUTTON, EVENT_MOUSE_WHEEL, EVENT_TOUCH, EVENT_WINDOW_RESIZE};
    for (int i = 0; i < 7 && i < seen.n; i++)
        CHECK(seen.types[i] == want[i]);
    CHECK(seen.seen_window == w);
    CHECK(seen.seen_user == &seen);
    CHECK(!seen.key_down_inside_hook); /* the hook runs before the polled state moves */
    CHECK(key_down(w, KEY_A));         /* ... which has moved by the time begin_frame returns */

    /* the hook did not take the events away from poll_event */
    int polled = 0;
    Event e;
    while (poll_event(w, &e))
        polled++;
    CHECK(polled == 7);

    window_set_event_hook(w, NULL, NULL);
    int before = seen.n;
    fake_key(w, KEY_B, true, false);
    window_begin_frame(w);
    CHECK(seen.n == before);

    fake_touch(w, 3, 5, 6, TOUCH_UP); /* the finger this test put down */
    fake_key(w, KEY_A, false, false);
    fake_key(w, KEY_B, false, false);
    fake_mouse_button(w, MOUSE_LEFT, false);
    window_begin_frame(w);
}

static void test_text_event(PlatformWindow *w)
{
    window_begin_frame(w);
    while (char_get_pressed(w))
    {
    }

    fake_inject_char(w, 0xE9); /* é */
    fake_inject_char(w, 0x4E2D);
    window_begin_frame(w);

    int chars = 0;
    uint32_t cps[4] = {0};
    Event e;
    while (poll_event(w, &e))
        if (e.type == EVENT_CHAR && chars < 4)
            cps[chars++] = e.data.codepoint;
    CHECK(chars == 2);
    CHECK(cps[0] == 0xE9 && cps[1] == 0x4E2D);

    /* ... and the queue still hands each one out exactly once */
    CHECK(char_get_pressed(w) == 0xE9);
    CHECK(char_get_pressed(w) == 0x4E2D);
    CHECK(char_get_pressed(w) == 0);
}

static void test_lock_mods(PlatformWindow *w)
{
    window_begin_frame(w);
    CHECK((key_mods(w) & (KEYMOD_CAPS_LOCK | KEYMOD_NUM_LOCK)) == 0);

    fake_set_lock_state(KEYMOD_CAPS_LOCK);
    CHECK(key_mods(w) == KEYMOD_CAPS_LOCK);

    fake_key(w, KEY_LEFT_SHIFT, true, false);
    window_begin_frame(w);
    fake_set_lock_state(KEYMOD_CAPS_LOCK | KEYMOD_NUM_LOCK);
    CHECK(key_mods(w) == (KEYMOD_SHIFT | KEYMOD_CAPS_LOCK | KEYMOD_NUM_LOCK));

    fake_set_lock_state(0);
    fake_key(w, KEY_LEFT_SHIFT, false, false);
    window_begin_frame(w);
    CHECK(key_mods(w) == 0);
}

static void test_capture(PlatformWindow *w)
{
    CHECK(mouse_capture(w, true));
    CHECK(!mouse_capture(w, false));
}


/* ---- gamepad and joystick connect / disconnect events, per window ---- */

static int device_events(PlatformWindow *w, EventType type, int index)
{
    int n = 0;
    Event e;
    while (poll_event(w, &e))
        n += e.type == type && e.data.device.index == index;
    return n;
}

static void test_device_events(PlatformWindow *w)
{
    window_begin_frame(w);
    CHECK(device_events(w, EVENT_GAMEPAD_CONNECTED, 0) == 0);

    int pad = gamepad_internal_connect("Pad");
    CHECK(pad == 0);
    window_begin_frame(w);
    CHECK(device_events(w, EVENT_GAMEPAD_CONNECTED, 0) == 1);
    window_begin_frame(w);
    CHECK(device_events(w, EVENT_GAMEPAD_CONNECTED, 0) == 0); /* told once */

    gamepad_internal_disconnect(pad);
    window_begin_frame(w);
    CHECK(device_events(w, EVENT_GAMEPAD_DISCONNECTED, 0) == 1);

    /* gone and back between two frames: a different device, so both events */
    pad = gamepad_internal_connect("Pad");
    window_begin_frame(w);
    poll_event(w, &(Event){0}); /* (consume) */
    gamepad_internal_disconnect(pad);
    pad = gamepad_internal_connect("Other pad");
    window_begin_frame(w);
    int disc = 0, conn = 0;
    Event e;
    EventType order[4];
    int n = 0;
    while (poll_event(w, &e))
    {
        if (e.type == EVENT_GAMEPAD_DISCONNECTED && e.data.device.index == 0)
            disc++, order[n++] = e.type;
        if (e.type == EVENT_GAMEPAD_CONNECTED && e.data.device.index == 0)
            conn++, order[n++] = e.type;
    }
    CHECK(disc == 1 && conn == 1);
    CHECK(n == 2 && order[0] == EVENT_GAMEPAD_DISCONNECTED && order[1] == EVENT_GAMEPAD_CONNECTED);

    /* a second window starts from nothing: it is told about the pad already there */
    WindowConfig cfg = {.title = "w2", .width = 100, .height = 80, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    PlatformWindow *w2 = window_create(&cfg);
    CHECK(w2 != NULL);
    if (w2)
    {
        window_begin_frame(w2);
        CHECK(device_events(w2, EVENT_GAMEPAD_CONNECTED, 0) == 1);
        window_destroy(w2);
    }

    /* joysticks the same way */
    int joy = joystick_internal_connect("Stick", 2, 4, 1);
    window_begin_frame(w);
    CHECK(device_events(w, EVENT_JOYSTICK_CONNECTED, joy) == 1);
    joystick_internal_disconnect(joy);
    window_begin_frame(w);
    CHECK(device_events(w, EVENT_JOYSTICK_DISCONNECTED, joy) == 1);

    gamepad_internal_disconnect(pad);
    window_begin_frame(w);
    CHECK(device_events(w, EVENT_GAMEPAD_DISCONNECTED, 0) == 1);
}


/* ---- a touch screen on a desktop: the first finger is the mouse ---- */

static int count_type(PlatformWindow *w, EventType type)
{
    int n = 0;
    Event e;
    while (poll_event(w, &e))
        n += e.type == type;
    return n;
}

static void test_real_touch(PlatformWindow *w)
{
    window_begin_frame(w);
    while (poll_event(w, &(Event){0}))
    {
    }

    /* finger down: a touch, and the mouse moves to it and presses */
    fake_real_touch(w, 5, 40, 60, TOUCH_DOWN);
    window_begin_frame(w);
    CHECK(touch_count(w) == 1 && touch_id(w, 0) == 5);
    CHECK(mouse_x(w) == 40 && mouse_y(w) == 60);
    CHECK(mouse_button_down(w, MOUSE_LEFT) && mouse_button_pressed(w, MOUSE_LEFT));
    CHECK(count_type(w, EVENT_MOUSE_BUTTON) == 1);

    /* dragging it drags the mouse */
    fake_real_touch(w, 5, 90, 120, TOUCH_MOVE);
    window_begin_frame(w);
    CHECK(mouse_x(w) == 90 && mouse_y(w) == 120 && mouse_button_down(w, MOUSE_LEFT));

    /* the system's own echo of the touch (a mouse event) is dropped while a finger is down */
    fake_mouse_move(w, 7, 7);
    fake_mouse_button(w, MOUSE_RIGHT, true);
    window_begin_frame(w);
    CHECK(mouse_x(w) == 90 && mouse_y(w) == 120);
    CHECK(!mouse_button_down(w, MOUSE_RIGHT));
    CHECK(count_type(w, EVENT_MOUSE_MOVE) == 0);

    /* a second finger is a touch only: the mouse stays with the first */
    fake_real_touch(w, 6, 200, 10, TOUCH_DOWN);
    window_begin_frame(w);
    CHECK(touch_count(w) == 2);
    CHECK(mouse_x(w) == 90 && mouse_button_down(w, MOUSE_LEFT));
    CHECK(count_type(w, EVENT_MOUSE_BUTTON) == 0);
    fake_real_touch(w, 6, 210, 20, TOUCH_MOVE);
    window_begin_frame(w);
    CHECK(mouse_x(w) == 90 && mouse_y(w) == 120);

    /* the first finger lifts: the mouse releases, and the second does not take over */
    fake_real_touch(w, 5, 90, 120, TOUCH_UP);
    window_begin_frame(w);
    CHECK(touch_count(w) == 1);
    CHECK(!mouse_button_down(w, MOUSE_LEFT) && mouse_button_released(w, MOUSE_LEFT));
    fake_real_touch(w, 6, 230, 40, TOUCH_MOVE);
    window_begin_frame(w);
    CHECK(mouse_x(w) == 90);

    /* all up: real mouse events count again */
    fake_real_touch(w, 6, 230, 40, TOUCH_UP);
    window_begin_frame(w);
    CHECK(touch_count(w) == 0);
    fake_mouse_move(w, 11, 22);
    window_begin_frame(w);
    CHECK(mouse_x(w) == 11 && mouse_y(w) == 22);

    /* a cancelled touch also lets go of the mouse */
    fake_real_touch(w, 9, 5, 5, TOUCH_DOWN);
    window_begin_frame(w);
    CHECK(mouse_button_down(w, MOUSE_LEFT));
    fake_real_touch(w, 9, 5, 5, TOUCH_CANCEL);
    window_begin_frame(w);
    CHECK(!mouse_button_down(w, MOUSE_LEFT) && touch_count(w) == 0);

    /* gestures see a real touch like any other */
    fake_real_touch(w, 1, 100, 100, TOUCH_DOWN);
    window_begin_frame(w);
    CHECK(gesture_is_detected(w, GESTURE_TAP));
    fake_real_touch(w, 1, 100, 100, TOUCH_UP);
    window_begin_frame(w);

    /* with mouse->touch emulation on, a real finger is not doubled by its own mouse echo */
    touch_set_mouse_emulation(w, true);
    fake_real_touch(w, 2, 30, 30, TOUCH_DOWN);
    window_begin_frame(w);
    CHECK(touch_count(w) == 1);
    fake_real_touch(w, 2, 30, 30, TOUCH_UP);
    window_begin_frame(w);
    CHECK(touch_count(w) == 0);
    touch_set_mouse_emulation(w, false);
}


/* ---- text input and the input method ---- */

static void test_text_input(PlatformWindow *w)
{
    window_begin_frame(w);
    while (char_get_pressed(w))
    {
    }
    CHECK(window_text_input_active(w) && fake_text_input_on(w));
    CHECK(strcmp(window_text_composition(w), "") == 0);

    /* an input method composing: edit events, and the composition is readable */
    fake_text_edit(w, "ni", 2);
    window_begin_frame(w);
    Event e;
    bool saw = false;
    while (poll_event(w, &e))
        if (e.type == EVENT_TEXT_EDIT)
            saw = strcmp(e.data.edit.text, "ni") == 0 && e.data.edit.cursor == 2;
    CHECK(saw);
    CHECK(strcmp(window_text_composition(w), "ni") == 0);

    /* committing: an empty edit, then the characters */
    fake_text_edit(w, "", 0);
    fake_inject_char(w, 0x4F60);
    window_begin_frame(w);
    CHECK(strcmp(window_text_composition(w), "") == 0);
    CHECK(char_get_pressed(w) == 0x4F60);

    /* a long composition is cut to what the event holds, never overflowed */
    char longtext[200];
    memset(longtext, 'z', sizeof longtext - 1);
    longtext[sizeof longtext - 1] = '\0';
    fake_text_edit(w, longtext, 10);
    window_begin_frame(w);
    CHECK(strlen(window_text_composition(w)) == 63);
    fake_text_edit(w, "", 0);
    window_begin_frame(w);

    /* the caret rectangle reaches the platform */
    window_set_text_input_rect(w, 10, 20, 3, 16);
    int r[4];
    fake_text_input_rect(w, r);
    CHECK(r[0] == 10 && r[1] == 20 && r[2] == 3 && r[3] == 16);

    /* stopping: no input method, no characters, no composition */
    fake_text_edit(w, "ab", 2);
    window_begin_frame(w);
    CHECK(strcmp(window_text_composition(w), "ab") == 0);
    window_text_input_stop(w);
    CHECK(!window_text_input_active(w) && !fake_text_input_on(w));
    CHECK(strcmp(window_text_composition(w), "") == 0);
    fake_inject_char(w, 'q');
    fake_key(w, KEY_Q, true, false);
    window_begin_frame(w);
    CHECK(char_get_pressed(w) == 0);       /* no text */
    CHECK(key_down(w, KEY_Q));             /* keys still work */
    int chars = 0;
    while (poll_event(w, &e))
        chars += e.type == EVENT_CHAR;
    CHECK(chars == 0);
    fake_key(w, KEY_Q, false, false);

    window_text_input_start(w);
    CHECK(window_text_input_active(w) && fake_text_input_on(w));
    fake_inject_char(w, 'q');
    window_begin_frame(w);
    CHECK(char_get_pressed(w) == 'q');
}

static HitTestResult hit_fn(PlatformWindow *w, int x, int y, void *user)
{
    (void)w;
    int *calls = user;
    (*calls)++;
    if (y < 30)
        return x > 280 ? HIT_NORMAL : HIT_DRAG;
    if (x >= 316)
        return HIT_RESIZE_RIGHT;
    return HIT_NORMAL;
}

static void test_window_features(void)
{
    WindowConfig cfg = {.title = "f", .width = 320, .height = 200, .x = WINDOW_POS_UNDEFINED, .y = WINDOW_POS_UNDEFINED};
    PlatformWindow *w = window_create(&cfg);
    CHECK(w != NULL);
    if (!w)
        return;

    /* decorations: on by default, off for undecorated, popups and tooltips, switchable */
    CHECK(fake_is_decorated(w));
    window_set_decorated(w, false);
    CHECK(!fake_is_decorated(w));
    window_set_decorated(w, true);
    CHECK(fake_is_decorated(w));

    WindowConfig pc = cfg;
    pc.kind = WINDOW_KIND_POPUP;
    pc.parent = w;
    PlatformWindow *popup = window_create(&pc);
    CHECK(popup && !fake_is_decorated(popup));
    pc.kind = WINDOW_KIND_NORMAL;
    pc.undecorated = true;
    PlatformWindow *bare = window_create(&pc);
    CHECK(bare && !fake_is_decorated(bare));
    if (popup)
        window_destroy(popup);
    if (bare)
        window_destroy(bare);

    /* hit test */
    int calls = 0;
    CHECK(fake_hit_test(w, 10, 10) == HIT_NORMAL); /* none set yet */
    window_set_hit_test(w, hit_fn, &calls);
    CHECK(fake_hit_test(w, 100, 10) == HIT_DRAG);
    CHECK(fake_hit_test(w, 318, 100) == HIT_RESIZE_RIGHT);
    CHECK(fake_hit_test(w, 100, 100) == HIT_NORMAL);
    CHECK(calls == 3);
    window_set_hit_test(w, NULL, NULL);
    CHECK(fake_hit_test(w, 100, 10) == HIT_NORMAL);

    /* cursor images */
    uint32_t px[4 * 4];
    for (int i = 0; i < 16; i++)
        px[i] = 0xFF000000u | (uint32_t)i;
    CHECK(cursor_create(NULL, 4, 4, 0, 0) == NULL);
    CHECK(cursor_create(px, 0, 4, 0, 0) == NULL);
    CHECK(cursor_create(px, 4, 4, 4, 0) == NULL);  /* the hot spot is outside */
    CHECK(cursor_create(px, 4, 4, 0, -1) == NULL);
    CHECK(cursor_create(px, 300, 4, 0, 0) == NULL); /* too large */
    PlatformCursor *c = cursor_create(px, 4, 4, 1, 2);
    CHECK(c != NULL);
    mouse_set_cursor_image(w, c);
    CHECK(fake_cursor_image(w) == c);
    mouse_set_cursor_image(w, NULL);
    CHECK(fake_cursor_image(w) == NULL);
    cursor_destroy(c);
    cursor_destroy(NULL); /* harmless */

    window_destroy(w);
}

int main(void)
{
    if (!platform_init())
        return 1;
    PlatformWindow *w = make();
    CHECK(w != NULL);
    if (!w)
        return 1;
    test_hook(w);
    test_text_event(w);
    test_lock_mods(w);
    test_capture(w);
    test_device_events(w);
    test_real_touch(w);
    test_text_input(w);
    window_destroy(w);
    test_window_features();
    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
