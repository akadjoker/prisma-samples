#define _GNU_SOURCE

#include "platform.h"
#include "../src/gamepad_evdev.c"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)

static void test_slots(void)
{
    CHECK(!gamepad_connected(0));
    CHECK(gamepad_name(0) == NULL);

    int s0 = gamepad_internal_connect("Pad Zero");
    int s1 = gamepad_internal_connect(NULL);
    int s2 = gamepad_internal_connect("");
    int s3 = gamepad_internal_connect("Pad Three");
    CHECK(s0 == 0 && s1 == 1 && s2 == 2 && s3 == 3);
    CHECK(gamepad_internal_connect("Pad Four") == -1);
    CHECK(strcmp(gamepad_name(0), "Pad Zero") == 0);
    CHECK(strcmp(gamepad_name(1), "Gamepad") == 0);
    CHECK(strcmp(gamepad_name(2), "Gamepad") == 0);

    gamepad_internal_set_button(1, GAMEPAD_BUTTON_A, true);
    gamepad_internal_set_axis(1, GAMEPAD_AXIS_LEFT_X, 0.5f);
    CHECK(gamepad_button_down(1, GAMEPAD_BUTTON_A));
    CHECK(NEAR(gamepad_axis(1, GAMEPAD_AXIS_LEFT_X), 0.5f));
    CHECK(!gamepad_button_down(0, GAMEPAD_BUTTON_A));

    gamepad_internal_disconnect(1);
    CHECK(!gamepad_connected(1) && gamepad_name(1) == NULL);
    CHECK(!gamepad_button_down(1, GAMEPAD_BUTTON_A));
    gamepad_internal_set_button(1, GAMEPAD_BUTTON_B, true);
    CHECK(!gamepad_button_down(1, GAMEPAD_BUTTON_B));

    CHECK(gamepad_internal_connect("Again") == 1);
    CHECK(!gamepad_button_down(1, GAMEPAD_BUTTON_A));
    CHECK(gamepad_axis(1, GAMEPAD_AXIS_LEFT_X) == 0.0f);

    CHECK(!gamepad_connected(-1) && !gamepad_connected(GAMEPAD_MAX) && !gamepad_connected(1000));
    CHECK(gamepad_name(-1) == NULL && gamepad_name(GAMEPAD_MAX) == NULL);
    CHECK(!gamepad_button_down(0, -1) && !gamepad_button_down(0, GAMEPAD_BUTTON_COUNT));
    CHECK(gamepad_axis(0, -1) == 0.0f && gamepad_axis(0, GAMEPAD_AXIS_COUNT) == 0.0f);
    gamepad_internal_disconnect(-1);
    gamepad_internal_disconnect(GAMEPAD_MAX);

    char longname[400];
    memset(longname, 'x', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    gamepad_internal_disconnect(3);
    CHECK(gamepad_internal_connect(longname) == 3);
    CHECK(strlen(gamepad_name(3)) == 127);

    gamepad_shutdown();
    CHECK(!gamepad_connected(0) && !gamepad_connected(3));
}

static void setup_pad(EvdevPad *d, int fd)
{
    memset(d, 0, sizeof *d);
    d->used = true;
    d->fd = fd;
    d->slot = gamepad_internal_connect("Test Pad");
    d->abs[ABS_X].present = d->abs[ABS_Y].present = true;
    d->abs[ABS_X].min = d->abs[ABS_Y].min = -32768;
    d->abs[ABS_X].max = d->abs[ABS_Y].max = 32767;
    d->abs[ABS_RX].present = d->abs[ABS_RY].present = true;
    d->abs[ABS_RX].min = d->abs[ABS_RY].min = -32768;
    d->abs[ABS_RX].max = d->abs[ABS_RY].max = 32767;
    d->abs[ABS_Z].present = d->abs[ABS_RZ].present = true;
    d->abs[ABS_Z].max = d->abs[ABS_RZ].max = 255;
    d->abs[ABS_HAT0X].present = d->abs[ABS_HAT0Y].present = true;
    d->abs[ABS_HAT0X].min = d->abs[ABS_HAT0Y].min = -1;
    d->abs[ABS_HAT0X].max = d->abs[ABS_HAT0Y].max = 1;
}

static struct input_event ev(int type, int code, int value)
{
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = (unsigned short)type;
    e.code = (unsigned short)code;
    e.value = value;
    return e;
}

static void feed(EvdevPad *d, int type, int code, int value)
{
    struct input_event e = ev(type, code, value);
    apply_event(d, &e);
}

static void test_buttons(void)
{
    EvdevPad d;
    setup_pad(&d, -1);
    for (size_t i = 0; i < sizeof k_key_map / sizeof k_key_map[0]; i++)
    {
        feed(&d, EV_KEY, k_key_map[i].code, 1);
        CHECK(gamepad_button_down(d.slot, k_key_map[i].button));
        for (int b = 0; b < GAMEPAD_BUTTON_COUNT; b++)
            CHECK(gamepad_button_down(d.slot, b) == (b == k_key_map[i].button));
        feed(&d, EV_KEY, k_key_map[i].code, 2);
        CHECK(gamepad_button_down(d.slot, k_key_map[i].button));
        feed(&d, EV_KEY, k_key_map[i].code, 0);
        CHECK(!gamepad_button_down(d.slot, k_key_map[i].button));
    }
    feed(&d, EV_KEY, BTN_SOUTH, 1);
    feed(&d, EV_KEY, KEY_A, 1);
    feed(&d, EV_KEY, BTN_TRIGGER, 1);
    CHECK(gamepad_button_down(d.slot, GAMEPAD_BUTTON_A));
    for (int b = 1; b < GAMEPAD_BUTTON_COUNT; b++)
        CHECK(!gamepad_button_down(d.slot, b));
    gamepad_shutdown();
}

static void test_axes(void)
{
    EvdevPad d;
    setup_pad(&d, -1);

    feed(&d, EV_ABS, ABS_X, 32767);
    CHECK(NEAR(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X), 1.0f));
    feed(&d, EV_ABS, ABS_X, -32768);
    CHECK(NEAR(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X), -1.0f));
    feed(&d, EV_ABS, ABS_X, 0);
    CHECK(fabsf(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X)) < 1e-4f);
    feed(&d, EV_ABS, ABS_X, 99999);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X) == 1.0f);
    feed(&d, EV_ABS, ABS_X, -99999);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X) == -1.0f);

    feed(&d, EV_ABS, ABS_Y, 32767);
    CHECK(NEAR(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_Y), 1.0f));
    feed(&d, EV_ABS, ABS_RX, -16384);
    CHECK(NEAR(gamepad_axis(d.slot, GAMEPAD_AXIS_RIGHT_X), -0.5f));
    feed(&d, EV_ABS, ABS_RY, 16383);
    CHECK(NEAR(gamepad_axis(d.slot, GAMEPAD_AXIS_RIGHT_Y), 0.5f));

    feed(&d, EV_ABS, ABS_Z, 0);
    feed(&d, EV_ABS, ABS_RZ, 255);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_LEFT) == 0.0f);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_RIGHT) == 1.0f);
    feed(&d, EV_ABS, ABS_Z, 51);
    CHECK(NEAR(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_LEFT), 0.2f));
    feed(&d, EV_ABS, ABS_Z, -50);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_LEFT) == 0.0f);

    feed(&d, EV_ABS, ABS_HAT0X, -1);
    CHECK(gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_LEFT) && !gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_RIGHT));
    feed(&d, EV_ABS, ABS_HAT0X, 1);
    CHECK(!gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_LEFT) && gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_RIGHT));
    feed(&d, EV_ABS, ABS_HAT0X, 0);
    CHECK(!gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_LEFT) && !gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_RIGHT));
    feed(&d, EV_ABS, ABS_HAT0Y, -1);
    CHECK(gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_UP) && !gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_DOWN));
    feed(&d, EV_ABS, ABS_HAT0Y, 1);
    CHECK(!gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_UP) && gamepad_button_down(d.slot, GAMEPAD_BUTTON_DPAD_DOWN));

    d.abs[ABS_RX].present = false;
    gamepad_internal_set_axis(d.slot, GAMEPAD_AXIS_RIGHT_X, 0.0f);
    feed(&d, EV_ABS, ABS_RX, 30000);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_RIGHT_X) == 0.0f);
    feed(&d, EV_ABS, ABS_MISC, 5);
    feed(&d, EV_ABS, ABS_CNT + 3, 5);

    d.abs[ABS_X].min = 0;
    d.abs[ABS_X].max = 255;
    feed(&d, EV_ABS, ABS_X, 0);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X) == -1.0f);
    feed(&d, EV_ABS, ABS_X, 255);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X) == 1.0f);
    feed(&d, EV_ABS, ABS_X, 128);
    CHECK(fabsf(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X)) < 0.01f);
    feed(&d, EV_ABS, ABS_X, 64);
    CHECK(NEAR(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X), -0.498f));

    d.abs[ABS_X].min = d.abs[ABS_X].max = 7;
    feed(&d, EV_ABS, ABS_X, 7);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X) == 0.0f);
    gamepad_shutdown();
}

static void test_digital_triggers(void)
{
    EvdevPad d;
    setup_pad(&d, -1);
    feed(&d, EV_KEY, BTN_TL2, 1);
    feed(&d, EV_KEY, BTN_TR2, 1);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_LEFT) == 0.0f);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_RIGHT) == 0.0f);

    d.abs[ABS_Z].present = d.abs[ABS_RZ].present = false;
    feed(&d, EV_KEY, BTN_TL2, 1);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_LEFT) == 1.0f);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_RIGHT) == 0.0f);
    feed(&d, EV_KEY, BTN_TR2, 1);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_RIGHT) == 1.0f);
    feed(&d, EV_KEY, BTN_TL2, 0);
    CHECK(gamepad_axis(d.slot, GAMEPAD_AXIS_TRIGGER_LEFT) == 0.0f);
    gamepad_shutdown();
}

static void test_detection(void)
{
    unsigned long keys[BITS_TO_LONGS(KEY_CNT)];
    unsigned long abs_bits[BITS_TO_LONGS(ABS_CNT)];
    memset(keys, 0, sizeof keys);
    memset(abs_bits, 0, sizeof abs_bits);
    CHECK(!evdev_is_gamepad(keys, abs_bits));

    keys[BTN_SOUTH / BITS_PER_LONG] |= 1UL << (BTN_SOUTH % BITS_PER_LONG);
    CHECK(!evdev_is_gamepad(keys, abs_bits));
    abs_bits[ABS_X / BITS_PER_LONG] |= 1UL << (ABS_X % BITS_PER_LONG);
    CHECK(!evdev_is_gamepad(keys, abs_bits));
    abs_bits[ABS_Y / BITS_PER_LONG] |= 1UL << (ABS_Y % BITS_PER_LONG);
    CHECK(evdev_is_gamepad(keys, abs_bits));

    keys[BTN_SOUTH / BITS_PER_LONG] &= ~(1UL << (BTN_SOUTH % BITS_PER_LONG));
    keys[BTN_TRIGGER / BITS_PER_LONG] |= 1UL << (BTN_TRIGGER % BITS_PER_LONG);
    CHECK(!evdev_is_gamepad(keys, abs_bits));
}

static int make_pipe(int fds[2])
{
    return pipe2(fds, O_NONBLOCK | O_CLOEXEC);
}

static void put(int fd, int type, int code, int value)
{
    struct input_event e = ev(type, code, value);
    CHECK(write(fd, &e, sizeof e) == (ssize_t)sizeof e);
}

static void test_reading(void)
{
    int fds[2];
    CHECK(make_pipe(fds) == 0);
    EvdevPad d;
    setup_pad(&d, fds[0]);
    d.used = true;

    CHECK(read_pad(&d));

    put(fds[1], EV_KEY, BTN_SOUTH, 1);
    put(fds[1], EV_ABS, ABS_X, 32767);
    put(fds[1], EV_SYN, SYN_REPORT, 0);
    CHECK(read_pad(&d));
    CHECK(gamepad_button_down(d.slot, GAMEPAD_BUTTON_A));
    CHECK(NEAR(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X), 1.0f));

    for (int i = 0; i < 101; i++)
        put(fds[1], EV_KEY, BTN_EAST, i % 2 == 0);
    CHECK(read_pad(&d));
    CHECK(gamepad_button_down(d.slot, GAMEPAD_BUTTON_B));

    put(fds[1], EV_SYN, SYN_DROPPED, 0);
    put(fds[1], EV_KEY, BTN_SOUTH, 0);
    put(fds[1], EV_ABS, ABS_X, -32768);
    CHECK(read_pad(&d));
    CHECK(d.dropped);
    CHECK(gamepad_button_down(d.slot, GAMEPAD_BUTTON_A));
    CHECK(NEAR(gamepad_axis(d.slot, GAMEPAD_AXIS_LEFT_X), 1.0f));
    put(fds[1], EV_SYN, SYN_REPORT, 0);
    CHECK(read_pad(&d));
    CHECK(!d.dropped);
    put(fds[1], EV_KEY, BTN_SOUTH, 0);
    CHECK(read_pad(&d));
    CHECK(!gamepad_button_down(d.slot, GAMEPAD_BUTTON_A));

    close(fds[1]);
    CHECK(!read_pad(&d));
    close_pad(&d);
    CHECK(!d.used);
    CHECK(!gamepad_connected(0));
    gamepad_shutdown();
}

static void test_probe_rejects(void)
{
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    CHECK(!probe_fd(fd, "/dev/null"));
    close(fd);

    int fds[2];
    CHECK(make_pipe(fds) == 0);
    CHECK(!probe_fd(fds[0], "pipe"));
    close(fds[0]);
    close(fds[1]);

    for (int i = 0; i < GAMEPAD_MAX; i++)
        CHECK(!gamepad_connected(i) && !g_dev[i].used);
    probe_path("/nonexistent/event0");
    CHECK(!gamepad_connected(0));
}

#define HOTPLUG_DIR "test_gamepad_tmp"

static void test_hotplug_events(void)
{
    fs_create_directory(HOTPLUG_DIR);
    int fds[2];
    CHECK(make_pipe(fds) == 0);

    EvdevPad *d = &g_dev[0];
    setup_pad(d, fds[0]);
    snprintf(d->path, sizeof d->path, INPUT_DIR "/event99");
    CHECK(gamepad_connected(0));

    g_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    CHECK(g_inotify >= 0);
    CHECK(inotify_add_watch(g_inotify, HOTPLUG_DIR, IN_CREATE | IN_DELETE | IN_ATTRIB) >= 0);

    CHECK(file_write_text(HOTPLUG_DIR "/js0", "x"));
    CHECK(file_write_text(HOTPLUG_DIR "/event98", "x"));
    handle_inotify();
    CHECK(g_dev[0].used && gamepad_connected(0));
    CHECK(!g_dev[1].used);

    fs_remove_path(HOTPLUG_DIR "/js0");
    fs_remove_path(HOTPLUG_DIR "/event98");
    CHECK(file_write_text(HOTPLUG_DIR "/event99", "x"));
    handle_inotify();
    CHECK(g_dev[0].used);

    fs_remove_path(HOTPLUG_DIR "/event99");
    handle_inotify();
    CHECK(!g_dev[0].used);
    CHECK(!gamepad_connected(0));

    close(g_inotify);
    g_inotify = -1;
    close(fds[1]);
    fs_remove_path(HOTPLUG_DIR);
    gamepad_shutdown();
}


/* ---- joysticks ---- */

static void test_joystick_slots(void)
{
    CHECK(!joystick_connected(0) && joystick_name(0) == NULL);
    CHECK(joystick_axis_count(0) == 0 && joystick_button_count(0) == 0 && joystick_hat_count(0) == 0);

    int a = joystick_internal_connect("Stick", 4, 12, 1);
    int b = joystick_internal_connect(NULL, 99, 999, 99); /* clamped to the limits */
    CHECK(a == 0 && b == 1);
    CHECK(strcmp(joystick_name(a), "Stick") == 0 && strcmp(joystick_name(b), "Joystick") == 0);
    CHECK(joystick_axis_count(a) == 4 && joystick_button_count(a) == 12 && joystick_hat_count(a) == 1);
    CHECK(joystick_axis_count(b) == JOYSTICK_MAX_AXES);
    CHECK(joystick_button_count(b) == JOYSTICK_MAX_BUTTONS);
    CHECK(joystick_hat_count(b) == JOYSTICK_MAX_HATS);
    CHECK(joystick_gamepad_index(a) == -1);

    joystick_internal_set_axis(a, 2, -0.25f);
    joystick_internal_set_button(a, 11, true);
    joystick_internal_set_hat(a, 0, JOYHAT_UP | JOYHAT_RIGHT);
    CHECK(NEAR(joystick_axis(a, 2), -0.25f) && joystick_button(a, 11) && joystick_hat(a, 0) == (JOYHAT_UP | JOYHAT_RIGHT));
    /* outside what the device has */
    joystick_internal_set_axis(a, 4, 1.0f);
    joystick_internal_set_button(a, 12, true);
    CHECK(joystick_axis(a, 4) == 0.0f && !joystick_button(a, 12) && joystick_hat(a, 1) == 0);
    CHECK(joystick_axis(a, -1) == 0.0f && !joystick_button(a, -1) && joystick_hat(a, -1) == 0);

    joystick_internal_link_gamepad(a, 2);
    CHECK(joystick_gamepad_index(a) == 2);
    joystick_internal_link_gamepad(a, 99);
    CHECK(joystick_gamepad_index(a) == -1);

    unsigned gen = joystick_internal_generation(a);
    joystick_internal_disconnect(a);
    CHECK(!joystick_connected(a) && joystick_name(a) == NULL && !joystick_button(a, 11));
    CHECK(joystick_internal_connect("Again", 1, 1, 0) == a);
    CHECK(joystick_internal_generation(a) == gen + 1); /* a different device in the same slot */
    CHECK(joystick_axis(a, 0) == 0.0f);

    CHECK(!joystick_connected(-1) && !joystick_connected(JOYSTICK_MAX));
    for (int i = 0; i < JOYSTICK_MAX + 2; i++)
        joystick_internal_connect("x", 1, 1, 0);
    CHECK(joystick_internal_connect("full", 1, 1, 0) == -1);
    gamepad_shutdown();
    CHECK(!joystick_connected(0) && !joystick_connected(1));
}

static void setup_joy(EvdevJoy *j, int fd)
{
    memset(j, 0, sizeof *j);
    j->used = true;
    j->fd = fd;
    j->axes = 2;
    j->axis_code[0] = ABS_X;
    j->axis_min[0] = 0;
    j->axis_max[0] = 1023;
    j->axis_code[1] = ABS_THROTTLE;
    j->axis_min[1] = -100;
    j->axis_max[1] = 100;
    j->buttons = 3;
    j->button_code[0] = BTN_TRIGGER;
    j->button_code[1] = BTN_THUMB;
    j->button_code[2] = BTN_TRIGGER_HAPPY1;
    j->hats = 2;
    j->slot = joystick_internal_connect("Test Stick", j->axes, j->buttons, j->hats);
}

static void jfeed(EvdevJoy *j, int type, int code, int value)
{
    struct input_event e = ev(type, code, value);
    joy_event(j, &e);
}

static void test_joystick_events(void)
{
    EvdevJoy j;
    setup_joy(&j, -1);
    CHECK(j.slot == 0);

    jfeed(&j, EV_ABS, ABS_X, 0);
    CHECK(NEAR(joystick_axis(j.slot, 0), -1.0f));
    jfeed(&j, EV_ABS, ABS_X, 1023);
    CHECK(NEAR(joystick_axis(j.slot, 0), 1.0f));
    jfeed(&j, EV_ABS, ABS_X, 512);
    CHECK(fabsf(joystick_axis(j.slot, 0)) < 0.01f);
    jfeed(&j, EV_ABS, ABS_THROTTLE, -100);
    CHECK(NEAR(joystick_axis(j.slot, 1), -1.0f));
    jfeed(&j, EV_ABS, ABS_Y, 5); /* an axis the device did not announce */
    CHECK(joystick_axis(j.slot, 0) < 0.5f);

    jfeed(&j, EV_KEY, BTN_THUMB, 1);
    CHECK(!joystick_button(j.slot, 0) && joystick_button(j.slot, 1) && !joystick_button(j.slot, 2));
    jfeed(&j, EV_KEY, BTN_TRIGGER_HAPPY1, 1);
    CHECK(joystick_button(j.slot, 2));
    jfeed(&j, EV_KEY, BTN_THUMB, 0);
    CHECK(!joystick_button(j.slot, 1));
    jfeed(&j, EV_KEY, KEY_A, 1); /* a key that is no button of this device */
    CHECK(!joystick_button(j.slot, 0));

    /* a hat is two axes, -1 / 0 / 1 */
    jfeed(&j, EV_ABS, ABS_HAT0X, 1);
    CHECK(joystick_hat(j.slot, 0) == JOYHAT_RIGHT);
    jfeed(&j, EV_ABS, ABS_HAT0Y, -1);
    CHECK(joystick_hat(j.slot, 0) == (JOYHAT_RIGHT | JOYHAT_UP));
    jfeed(&j, EV_ABS, ABS_HAT0X, -1);
    jfeed(&j, EV_ABS, ABS_HAT0Y, 1);
    CHECK(joystick_hat(j.slot, 0) == (JOYHAT_LEFT | JOYHAT_DOWN));
    jfeed(&j, EV_ABS, ABS_HAT0X, 0);
    jfeed(&j, EV_ABS, ABS_HAT0Y, 0);
    CHECK(joystick_hat(j.slot, 0) == 0);
    jfeed(&j, EV_ABS, ABS_HAT1Y, 1); /* the second hat */
    CHECK(joystick_hat(j.slot, 1) == JOYHAT_DOWN && joystick_hat(j.slot, 0) == 0);
    jfeed(&j, EV_ABS, ABS_HAT2X, 1); /* a third: not announced */
    CHECK(joystick_hat(j.slot, 2) == 0);

    /* dropped events: ignored until the report, then resynchronised (no real device here) */
    jfeed(&j, EV_SYN, SYN_DROPPED, 0);
    CHECK(j.dropped);
    jfeed(&j, EV_KEY, BTN_TRIGGER, 1);
    CHECK(!joystick_button(j.slot, 0));
    jfeed(&j, EV_SYN, SYN_REPORT, 0);
    CHECK(!j.dropped);

    close_joy(&j);
    CHECK(!j.used && !joystick_connected(0));
    gamepad_shutdown();
}

static void test_joystick_reading(void)
{
    int fds[2];
    CHECK(make_pipe(fds) == 0);
    EvdevJoy j;
    setup_joy(&j, fds[0]);

    CHECK(read_joy(&j)); /* nothing yet, still alive */
    put(fds[1], EV_ABS, ABS_X, 1023);
    put(fds[1], EV_KEY, BTN_TRIGGER, 1);
    put(fds[1], EV_ABS, ABS_HAT0X, 1);
    put(fds[1], EV_SYN, SYN_REPORT, 0);
    CHECK(read_joy(&j));
    CHECK(NEAR(joystick_axis(j.slot, 0), 1.0f) && joystick_button(j.slot, 0) && joystick_hat(j.slot, 0) == JOYHAT_RIGHT);

    close(fds[1]);
    CHECK(!read_joy(&j)); /* the device went away */
    close_joy(&j);
    gamepad_shutdown();
}

static void test_joystick_probe(void)
{
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0 && !joy_probe_fd(fd, "/dev/null", -1));
    close(fd);
    int fds[2];
    CHECK(make_pipe(fds) == 0);
    CHECK(!joy_probe_fd(fds[0], "pipe", -1));
    close(fds[0]);
    close(fds[1]);
    CHECK(!joystick_connected(0));

    CHECK(is_joy_button(BTN_TRIGGER) && is_joy_button(BTN_SOUTH) && is_joy_button(BTN_TRIGGER_HAPPY1));
    CHECK(!is_joy_button(KEY_A) && !is_joy_button(KEY_ENTER));
    CHECK(is_hat_code(ABS_HAT0X) && is_hat_code(ABS_HAT3Y) && !is_hat_code(ABS_X) && !is_hat_code(ABS_MISC));
}

/* ---- rumble ---- */

static void test_rumble(void)
{
    CHECK(!gamepad_rumble(0, 1.0f, 1.0f, 100)); /* nothing connected */
    CHECK(!gamepad_rumble(-1, 1.0f, 1.0f, 100));

    EvdevPad d;
    setup_pad(&d, -1);
    d.ff_id = -1;
    CHECK(!gamepad_rumble(d.slot, 1.0f, 0.5f, 200)); /* a pad without force feedback */
    CHECK(!gamepad_backend_rumble(d.slot + 1, 1.0f, 1.0f, 100)); /* no such pad */

    /* a pad that claims FF, on a descriptor that is not an evdev node: the upload fails cleanly */
    int fds[2];
    CHECK(make_pipe(fds) == 0);
    d.fd = fds[0];
    d.can_rumble = true;
    g_dev[0] = d;
    CHECK(!gamepad_backend_rumble(d.slot, 1.0f, 1.0f, 100));
    CHECK(g_dev[0].ff_id == -1);
    CHECK(gamepad_backend_rumble(d.slot, 0.0f, 0.0f, 100)); /* stopping always works */
    CHECK(gamepad_backend_rumble(d.slot, 1.0f, 1.0f, 0));
    CHECK(gamepad_rumble(d.slot, 5.0f, -3.0f, 50) == false); /* out-of-range strengths are clamped, then fail the same way */
    memset(&g_dev[0], 0, sizeof g_dev[0]);
    close(fds[0]);
    close(fds[1]);
    gamepad_shutdown();
}

static void test_init_poll_shutdown(void)
{
    platform_clear_error();
    gamepad_backend_init();
    gamepad_backend_poll();
    gamepad_backend_poll();
    if (!dir_exists(INPUT_DIR))
    {
        platform_clear_error();
        gamepad_backend_shutdown();
        gamepad_backend_init();
        CHECK(platform_get_error()[0] == '\0');
        CHECK(!gamepad_connected(0));
    }
    gamepad_backend_shutdown();
    gamepad_backend_shutdown();
    CHECK(g_inotify == -1);
}

int main(void)
{
    test_slots();
    test_buttons();
    test_axes();
    test_digital_triggers();
    test_detection();
    test_reading();
    test_probe_rejects();
    test_hotplug_events();
    test_joystick_slots();
    test_joystick_events();
    test_joystick_reading();
    test_joystick_probe();
    test_rumble();
    test_init_poll_shutdown();

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
