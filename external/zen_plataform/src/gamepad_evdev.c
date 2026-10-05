#include "gamepad_internal.h"
#include "error_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define INPUT_DIR "/dev/input"
#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(n) (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define EVENT_BATCH 32
#define PAD_PATH_CAP 64

typedef struct
{
    bool used;
    bool dropped;
    int fd;
    int slot;
    char path[PAD_PATH_CAP];
    bool can_rumble; /* FF_RUMBLE, and the node was opened for writing */
    int ff_id;       /* the uploaded effect, or -1 */
    struct
    {
        bool present;
        int min, max;
    } abs[ABS_CNT];
} EvdevPad;

/* A raw joystick: whatever the device has, in code order. */
#define JOY_NODE_CAP PAD_PATH_CAP
typedef struct
{
    bool used;
    bool dropped;
    int fd;
    int slot;
    char path[JOY_NODE_CAP];
    int axes, buttons, hats;
    int axis_code[JOYSTICK_MAX_AXES];
    int axis_min[JOYSTICK_MAX_AXES], axis_max[JOYSTICK_MAX_AXES];
    int button_code[JOYSTICK_MAX_BUTTONS];
    int hat_x[JOYSTICK_MAX_HATS], hat_y[JOYSTICK_MAX_HATS];
} EvdevJoy;

static EvdevPad g_dev[GAMEPAD_MAX];
static EvdevJoy g_joy[JOYSTICK_MAX];
static int g_inotify = -1;

static const struct
{
    int code;
    int button;
} k_key_map[] = {
    {BTN_SOUTH, GAMEPAD_BUTTON_A},
    {BTN_EAST, GAMEPAD_BUTTON_B},
    {BTN_NORTH, GAMEPAD_BUTTON_X},
    {BTN_WEST, GAMEPAD_BUTTON_Y},
    {BTN_SELECT, GAMEPAD_BUTTON_BACK},
    {BTN_MODE, GAMEPAD_BUTTON_GUIDE},
    {BTN_START, GAMEPAD_BUTTON_START},
    {BTN_THUMBL, GAMEPAD_BUTTON_LEFT_STICK},
    {BTN_THUMBR, GAMEPAD_BUTTON_RIGHT_STICK},
    {BTN_TL, GAMEPAD_BUTTON_LEFT_SHOULDER},
    {BTN_TR, GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {BTN_DPAD_UP, GAMEPAD_BUTTON_DPAD_UP},
    {BTN_DPAD_DOWN, GAMEPAD_BUTTON_DPAD_DOWN},
    {BTN_DPAD_LEFT, GAMEPAD_BUTTON_DPAD_LEFT},
    {BTN_DPAD_RIGHT, GAMEPAD_BUTTON_DPAD_RIGHT},
};

static const struct
{
    int code;
    int axis;
} k_stick_map[] = {
    {ABS_X, GAMEPAD_AXIS_LEFT_X},
    {ABS_Y, GAMEPAD_AXIS_LEFT_Y},
    {ABS_RX, GAMEPAD_AXIS_RIGHT_X},
    {ABS_RY, GAMEPAD_AXIS_RIGHT_Y},
};

static const struct
{
    int code;
    int axis;
} k_trigger_map[] = {
    {ABS_Z, GAMEPAD_AXIS_TRIGGER_LEFT},
    {ABS_RZ, GAMEPAD_AXIS_TRIGGER_RIGHT},
};

static bool test_bit(const unsigned long *bits, int bit)
{
    unsigned b = (unsigned)bit;
    return (bits[b / BITS_PER_LONG] >> (b % BITS_PER_LONG)) & 1UL;
}

static bool evdev_is_gamepad(const unsigned long *key_bits, const unsigned long *abs_bits)
{
    return test_bit(key_bits, BTN_SOUTH) && test_bit(abs_bits, ABS_X) && test_bit(abs_bits, ABS_Y);
}

static float norm_stick(int value, int min, int max)
{
    if (max <= min)
        return 0.0f;
    double half = ((double)max - (double)min) / 2.0;
    double v = ((double)value - ((double)min + (double)max) / 2.0) / half;
    return (float)(v < -1.0 ? -1.0 : v > 1.0 ? 1.0
                                             : v);
}

static float norm_trigger(int value, int min, int max)
{
    if (max <= min)
        return 0.0f;
    double v = ((double)value - (double)min) / ((double)max - (double)min);
    return (float)(v < 0.0 ? 0.0 : v > 1.0 ? 1.0
                                           : v);
}

static void apply_hat(EvdevPad *d, int negative, int positive, int value)
{
    gamepad_internal_set_button(d->slot, negative, value < 0);
    gamepad_internal_set_button(d->slot, positive, value > 0);
}

static void apply_abs(EvdevPad *d, int code, int value)
{
    if (code < 0 || code >= ABS_CNT || !d->abs[code].present)
        return;
    int min = d->abs[code].min;
    int max = d->abs[code].max;
    if (code == ABS_HAT0X)
    {
        apply_hat(d, GAMEPAD_BUTTON_DPAD_LEFT, GAMEPAD_BUTTON_DPAD_RIGHT, value);
        return;
    }
    if (code == ABS_HAT0Y)
    {
        apply_hat(d, GAMEPAD_BUTTON_DPAD_UP, GAMEPAD_BUTTON_DPAD_DOWN, value);
        return;
    }
    for (size_t i = 0; i < sizeof k_stick_map / sizeof k_stick_map[0]; i++)
    {
        if (k_stick_map[i].code == code)
            gamepad_internal_set_axis(d->slot, k_stick_map[i].axis, norm_stick(value, min, max));
    }
    for (size_t i = 0; i < sizeof k_trigger_map / sizeof k_trigger_map[0]; i++)
    {
        if (k_trigger_map[i].code == code)
            gamepad_internal_set_axis(d->slot, k_trigger_map[i].axis, norm_trigger(value, min, max));
    }
}

static void apply_key(EvdevPad *d, int code, bool down)
{
    for (size_t i = 0; i < sizeof k_key_map / sizeof k_key_map[0]; i++)
    {
        if (k_key_map[i].code == code)
            gamepad_internal_set_button(d->slot, k_key_map[i].button, down);
    }
    if (code == BTN_TL2 && !d->abs[ABS_Z].present)
        gamepad_internal_set_axis(d->slot, GAMEPAD_AXIS_TRIGGER_LEFT, down ? 1.0f : 0.0f);
    if (code == BTN_TR2 && !d->abs[ABS_RZ].present)
        gamepad_internal_set_axis(d->slot, GAMEPAD_AXIS_TRIGGER_RIGHT, down ? 1.0f : 0.0f);
}

static void sync_state(EvdevPad *d)
{
    for (int code = 0; code < ABS_CNT; code++)
    {
        struct input_absinfo info;
        if (d->abs[code].present && ioctl(d->fd, EVIOCGABS((unsigned)code), &info) == 0)
            apply_abs(d, code, info.value);
    }

    unsigned long keys[BITS_TO_LONGS(KEY_CNT)];
    memset(keys, 0, sizeof keys);
    if (ioctl(d->fd, EVIOCGKEY(sizeof keys), keys) < 0)
        return;
    for (size_t i = 0; i < sizeof k_key_map / sizeof k_key_map[0]; i++)
        apply_key(d, k_key_map[i].code, test_bit(keys, k_key_map[i].code));
    apply_key(d, BTN_TL2, test_bit(keys, BTN_TL2));
    apply_key(d, BTN_TR2, test_bit(keys, BTN_TR2));
}

static void apply_event(EvdevPad *d, const struct input_event *ev)
{
    if (ev->type == EV_SYN)
    {
        if (ev->code == SYN_DROPPED)
            d->dropped = true;
        else if (ev->code == SYN_REPORT && d->dropped)
        {
            d->dropped = false;
            sync_state(d);
        }
        return;
    }
    if (d->dropped)
        return;
    if (ev->type == EV_KEY && ev->code <= KEY_MAX)
        apply_key(d, ev->code, ev->value != 0);
    else if (ev->type == EV_ABS)
        apply_abs(d, ev->code, ev->value);
}

static void rumble_stop(EvdevPad *d)
{
    if (d->fd < 0 || d->ff_id < 0)
        return;
    struct input_event stop;
    memset(&stop, 0, sizeof stop);
    stop.type = EV_FF;
    stop.code = (unsigned short)d->ff_id;
    stop.value = 0;
    if (write(d->fd, &stop, sizeof stop) < 0)
    {
        /* the device may be gone already */
    }
    ioctl(d->fd, EVIOCRMFF, d->ff_id);
    d->ff_id = -1;
}

static void close_pad(EvdevPad *d)
{
    if (!d || !d->used)
        return;
    rumble_stop(d);
    close(d->fd);
    gamepad_internal_disconnect(d->slot);
    memset(d, 0, sizeof *d);
}

static bool read_pad(EvdevPad *d)
{
    struct input_event batch[EVENT_BATCH];
    for (;;)
    {
        ssize_t n = read(d->fd, batch, sizeof batch);
        if (n > 0)
        {
            for (size_t i = 0; i < (size_t)n / sizeof batch[0]; i++)
                apply_event(d, &batch[i]);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN)
            return true;
        return false;
    }
}

static EvdevPad *find_pad(const char *path)
{
    for (int i = 0; i < GAMEPAD_MAX; i++)
    {
        if (g_dev[i].used && strcmp(g_dev[i].path, path) == 0)
            return &g_dev[i];
    }
    return NULL;
}

static bool probe_fd(int fd, const char *path)
{
    unsigned long types[BITS_TO_LONGS(EV_CNT)];
    unsigned long keys[BITS_TO_LONGS(KEY_CNT)];
    unsigned long abs_bits[BITS_TO_LONGS(ABS_CNT)];
    memset(types, 0, sizeof types);
    memset(keys, 0, sizeof keys);
    memset(abs_bits, 0, sizeof abs_bits);

    if (ioctl(fd, EVIOCGBIT(0, sizeof types), types) < 0 || !test_bit(types, EV_KEY) || !test_bit(types, EV_ABS))
        return false;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0 || ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs_bits), abs_bits) < 0)
        return false;
    if (!evdev_is_gamepad(keys, abs_bits))
        return false;

    EvdevPad *d = NULL;
    for (int i = 0; i < GAMEPAD_MAX && !d; i++)
    {
        if (!g_dev[i].used)
            d = &g_dev[i];
    }
    if (!d)
        return false;

    char name[128] = "";
    if (ioctl(fd, EVIOCGNAME(sizeof name), name) < 0)
        name[0] = '\0';

    memset(d, 0, sizeof *d);
    for (int code = 0; code < ABS_CNT; code++)
    {
        struct input_absinfo info;
        if (test_bit(abs_bits, code) && ioctl(fd, EVIOCGABS((unsigned)code), &info) == 0)
        {
            d->abs[code].present = true;
            d->abs[code].min = info.minimum;
            d->abs[code].max = info.maximum;
        }
    }

    d->slot = gamepad_internal_connect(name);
    if (d->slot < 0)
    {
        memset(d, 0, sizeof *d);
        return false;
    }
    d->used = true;
    d->fd = fd;
    d->ff_id = -1;
    {
        unsigned long ff[BITS_TO_LONGS(FF_CNT)];
        memset(ff, 0, sizeof ff);
        int mode = fcntl(fd, F_GETFL);
        d->can_rumble = mode >= 0 && (mode & O_ACCMODE) == O_RDWR &&
                        ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) >= 0 && test_bit(ff, FF_RUMBLE);
    }
    snprintf(d->path, sizeof d->path, "%s", path);
    sync_state(d);
    return true;
}

/* ---- raw joysticks ---- */

static bool is_hat_code(int code)
{
    return code >= ABS_HAT0X && code <= ABS_HAT3Y;
}

static EvdevJoy *find_joy(const char *path)
{
    for (int i = 0; i < JOYSTICK_MAX; i++)
    {
        if (g_joy[i].used && strcmp(g_joy[i].path, path) == 0)
            return &g_joy[i];
    }
    return NULL;
}

static void joy_apply_abs(EvdevJoy *j, int code, int value)
{
    if (is_hat_code(code))
    {
        int hat = (code - ABS_HAT0X) / 2;
        if (hat >= j->hats)
            return;
        if ((code - ABS_HAT0X) % 2 == 0)
            j->hat_x[hat] = value;
        else
            j->hat_y[hat] = value;
        int mask = (j->hat_y[hat] < 0 ? JOYHAT_UP : 0) | (j->hat_y[hat] > 0 ? JOYHAT_DOWN : 0) |
                   (j->hat_x[hat] < 0 ? JOYHAT_LEFT : 0) | (j->hat_x[hat] > 0 ? JOYHAT_RIGHT : 0);
        joystick_internal_set_hat(j->slot, hat, mask);
        return;
    }
    for (int a = 0; a < j->axes; a++)
    {
        if (j->axis_code[a] == code)
        {
            joystick_internal_set_axis(j->slot, a, norm_stick(value, j->axis_min[a], j->axis_max[a]));
            return;
        }
    }
}

static void joy_apply_key(EvdevJoy *j, int code, bool down)
{
    for (int b = 0; b < j->buttons; b++)
    {
        if (j->button_code[b] == code)
        {
            joystick_internal_set_button(j->slot, b, down);
            return;
        }
    }
}

static void joy_sync(EvdevJoy *j)
{
    for (int a = 0; a < j->axes; a++)
    {
        struct input_absinfo info;
        if (ioctl(j->fd, EVIOCGABS((unsigned)j->axis_code[a]), &info) == 0)
            joy_apply_abs(j, j->axis_code[a], info.value);
    }
    for (int h = 0; h < j->hats; h++)
    {
        struct input_absinfo info;
        if (ioctl(j->fd, EVIOCGABS((unsigned)(ABS_HAT0X + 2 * h)), &info) == 0)
            joy_apply_abs(j, ABS_HAT0X + 2 * h, info.value);
        if (ioctl(j->fd, EVIOCGABS((unsigned)(ABS_HAT0Y + 2 * h)), &info) == 0)
            joy_apply_abs(j, ABS_HAT0Y + 2 * h, info.value);
    }
    unsigned long keys[BITS_TO_LONGS(KEY_CNT)];
    memset(keys, 0, sizeof keys);
    if (ioctl(j->fd, EVIOCGKEY(sizeof keys), keys) < 0)
        return;
    for (int b = 0; b < j->buttons; b++)
        joystick_internal_set_button(j->slot, b, test_bit(keys, j->button_code[b]));
}

static void joy_event(EvdevJoy *j, const struct input_event *ev)
{
    if (ev->type == EV_SYN)
    {
        if (ev->code == SYN_DROPPED)
            j->dropped = true;
        else if (ev->code == SYN_REPORT && j->dropped)
        {
            j->dropped = false;
            joy_sync(j);
        }
        return;
    }
    if (j->dropped)
        return;
    if (ev->type == EV_KEY && ev->code <= KEY_MAX)
        joy_apply_key(j, ev->code, ev->value != 0);
    else if (ev->type == EV_ABS)
        joy_apply_abs(j, ev->code, ev->value);
}

static void close_joy(EvdevJoy *j)
{
    if (!j || !j->used)
        return;
    close(j->fd);
    joystick_internal_disconnect(j->slot);
    memset(j, 0, sizeof *j);
}

static bool read_joy(EvdevJoy *j)
{
    struct input_event batch[EVENT_BATCH];
    for (;;)
    {
        ssize_t n = read(j->fd, batch, sizeof batch);
        if (n > 0)
        {
            for (size_t i = 0; i < (size_t)n / sizeof batch[0]; i++)
                joy_event(j, &batch[i]);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN)
            return true;
        return false;
    }
}

/* A button code that belongs to a game controller (not a keyboard key). */
static bool is_joy_button(int code)
{
    return (code >= BTN_MISC && code < BTN_MISC + 0x60) || (code >= BTN_TRIGGER_HAPPY && code <= BTN_TRIGGER_HAPPY40);
}

/* Takes ownership of fd when it returns true. */
static bool joy_probe_fd(int fd, const char *path, int gamepad_slot)
{
    unsigned long types[BITS_TO_LONGS(EV_CNT)];
    unsigned long keys[BITS_TO_LONGS(KEY_CNT)];
    unsigned long abs_bits[BITS_TO_LONGS(ABS_CNT)];
    memset(types, 0, sizeof types);
    memset(keys, 0, sizeof keys);
    memset(abs_bits, 0, sizeof abs_bits);
    if (ioctl(fd, EVIOCGBIT(0, sizeof types), types) < 0 || !test_bit(types, EV_KEY))
        return false;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0)
        return false;
    if (test_bit(types, EV_ABS))
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs_bits), abs_bits);

    /* not a mouse, touchpad, tablet or keyboard */
    if (test_bit(keys, BTN_TOUCH) || test_bit(keys, BTN_LEFT) || test_bit(keys, BTN_TOOL_PEN) ||
        test_bit(keys, BTN_TOOL_FINGER) || (test_bit(keys, KEY_A) && test_bit(keys, KEY_ENTER)))
        return false;
    bool has_button = false;
    for (int c = BTN_JOYSTICK; c < BTN_JOYSTICK + 0x30 && !has_button; c++)
        has_button = test_bit(keys, c);
    if (!has_button || !test_bit(types, EV_ABS))
        return false;

    EvdevJoy *j = NULL;
    for (int i = 0; i < JOYSTICK_MAX && !j; i++)
    {
        if (!g_joy[i].used)
            j = &g_joy[i];
    }
    if (!j)
        return false;
    memset(j, 0, sizeof *j);

    for (int code = 0; code < ABS_MT_SLOT && j->axes < JOYSTICK_MAX_AXES; code++)
    {
        struct input_absinfo info;
        if (!test_bit(abs_bits, code) || is_hat_code(code) || ioctl(fd, EVIOCGABS((unsigned)code), &info) < 0)
            continue;
        j->axis_code[j->axes] = code;
        j->axis_min[j->axes] = info.minimum;
        j->axis_max[j->axes] = info.maximum;
        j->axes++;
    }
    for (int h = 0; h < JOYSTICK_MAX_HATS; h++)
        if (test_bit(abs_bits, ABS_HAT0X + 2 * h) || test_bit(abs_bits, ABS_HAT0Y + 2 * h))
            j->hats = h + 1;
    for (int code = BTN_MISC; code <= KEY_MAX && j->buttons < JOYSTICK_MAX_BUTTONS; code++)
        if (is_joy_button(code) && test_bit(keys, code))
            j->button_code[j->buttons++] = code;
    if (j->axes == 0 && j->hats == 0)
        return false;

    char name[128] = "";
    if (ioctl(fd, EVIOCGNAME(sizeof name), name) < 0)
        name[0] = '\0';
    j->slot = joystick_internal_connect(name, j->axes, j->buttons, j->hats);
    if (j->slot < 0)
    {
        memset(j, 0, sizeof *j);
        return false;
    }
    joystick_internal_link_gamepad(j->slot, gamepad_slot);
    j->used = true;
    j->fd = fd;
    snprintf(j->path, sizeof j->path, "%s", path);
    joy_sync(j);
    return true;
}

static void probe_path(const char *path)
{
    EvdevPad *pad = find_pad(path);
    if (!pad)
    {
        /* writable if we may (force feedback needs it), read-only otherwise */
        int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0 && !probe_fd(fd, path))
            close(fd);
        pad = find_pad(path);
    }
    if (!find_joy(path))
    {
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0 && !joy_probe_fd(fd, path, pad ? pad->slot : -1))
            close(fd);
    }
}

static bool is_event_node(const char *name)
{
    return strncmp(name, "event", 5) == 0;
}

static bool scan_entry(const char *path, PathType type, void *user)
{
    (void)type;
    (void)user;
    if (is_event_node(path_filename(path)))
        probe_path(path);
    return true;
}

static void handle_inotify(void)
{
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (;;)
    {
        ssize_t n = read(g_inotify, buf, sizeof buf);
        if (n <= 0)
            return;
        for (char *p = buf; p < buf + n;)
        {
            const struct inotify_event *ev = (const struct inotify_event *)p;
            p += sizeof *ev + ev->len;
            if (ev->len == 0 || !is_event_node(ev->name))
                continue;
            char path[PAD_PATH_CAP];
            if (snprintf(path, sizeof path, INPUT_DIR "/%s", ev->name) >= (int)sizeof path)
                continue;
            if (ev->mask & IN_DELETE)
            {
                close_pad(find_pad(path));
                close_joy(find_joy(path));
            }
            else if (ev->mask & (IN_CREATE | IN_ATTRIB))
                probe_path(path);
        }
    }
}

void gamepad_backend_init(void)
{
    memset(g_dev, 0, sizeof g_dev);
    memset(g_joy, 0, sizeof g_joy);
    g_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (g_inotify >= 0 && inotify_add_watch(g_inotify, INPUT_DIR, IN_CREATE | IN_DELETE | IN_ATTRIB) < 0)
    {
        close(g_inotify);
        g_inotify = -1;
    }
    if (!fs_enumerate_directory(INPUT_DIR, false, scan_entry, NULL))
        platform_clear_error();
}

void gamepad_backend_poll(void)
{
    if (g_inotify >= 0)
        handle_inotify();
    for (int i = 0; i < GAMEPAD_MAX; i++)
    {
        if (g_dev[i].used && !read_pad(&g_dev[i]))
            close_pad(&g_dev[i]);
    }
    for (int i = 0; i < JOYSTICK_MAX; i++)
    {
        if (g_joy[i].used && !read_joy(&g_joy[i]))
            close_joy(&g_joy[i]);
    }
}

void gamepad_backend_shutdown(void)
{
    for (int i = 0; i < GAMEPAD_MAX; i++)
        close_pad(&g_dev[i]);
    for (int i = 0; i < JOYSTICK_MAX; i++)
        close_joy(&g_joy[i]);
    if (g_inotify >= 0)
        close(g_inotify);
    g_inotify = -1;
}

bool gamepad_backend_rumble(int slot, float strong, float weak, uint32_t ms)
{
    EvdevPad *d = NULL;
    for (int i = 0; i < GAMEPAD_MAX && !d; i++)
    {
        if (g_dev[i].used && g_dev[i].slot == slot)
            d = &g_dev[i];
    }
    if (!d || !d->can_rumble)
        return false;
    if (ms == 0 || (strong <= 0.0f && weak <= 0.0f))
    {
        rumble_stop(d);
        return true;
    }

    struct ff_effect e;
    memset(&e, 0, sizeof e);
    e.type = FF_RUMBLE;
    e.id = (short)d->ff_id; /* -1 asks the kernel for a new one, otherwise the effect is replaced */
    e.replay.length = (unsigned short)(ms > 0xFFFF ? 0xFFFF : ms);
    e.u.rumble.strong_magnitude = (unsigned short)(strong * 65535.0f);
    e.u.rumble.weak_magnitude = (unsigned short)(weak * 65535.0f);
    if (ioctl(d->fd, EVIOCSFF, &e) < 0)
        return false;
    d->ff_id = e.id;

    struct input_event play;
    memset(&play, 0, sizeof play);
    play.type = EV_FF;
    play.code = (unsigned short)e.id;
    play.value = 1;
    return write(d->fd, &play, sizeof play) == (ssize_t)sizeof play;
}
