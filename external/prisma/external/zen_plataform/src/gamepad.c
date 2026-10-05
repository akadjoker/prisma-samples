#include "gamepad_internal.h"

#include <string.h>

#define GAMEPAD_NAME_CAP 128

typedef struct
{
    bool connected;
    char name[GAMEPAD_NAME_CAP];
    bool buttons[GAMEPAD_BUTTON_COUNT];
    float axes[GAMEPAD_AXIS_COUNT];
} Pad;

static Pad g_pads[GAMEPAD_MAX];
static unsigned g_pad_gen[GAMEPAD_MAX];

typedef struct
{
    bool connected;
    char name[GAMEPAD_NAME_CAP];
    int axes, buttons, hats;
    int gamepad; /* the gamepad slot of the same device, or -1 */
    float axis[JOYSTICK_MAX_AXES];
    bool button[JOYSTICK_MAX_BUTTONS];
    int hat[JOYSTICK_MAX_HATS];
} Joy;

static Joy g_joys[JOYSTICK_MAX];
static unsigned g_joy_gen[JOYSTICK_MAX];

static bool valid_slot(int index)
{
    return index >= 0 && index < GAMEPAD_MAX;
}

void gamepad_init(void)
{
    memset(g_pads, 0, sizeof g_pads);
    memset(g_joys, 0, sizeof g_joys);
    gamepad_backend_init();
}

void gamepad_poll(void)
{
    gamepad_backend_poll();
}

void gamepad_shutdown(void)
{
    gamepad_backend_shutdown();
    memset(g_pads, 0, sizeof g_pads);
    memset(g_joys, 0, sizeof g_joys);
}

int gamepad_internal_connect(const char *name)
{
    for (int i = 0; i < GAMEPAD_MAX; i++)
    {
        if (g_pads[i].connected)
            continue;
        memset(&g_pads[i], 0, sizeof g_pads[i]);
        g_pads[i].connected = true;
        g_pad_gen[i]++;
        strncpy(g_pads[i].name, name && name[0] ? name : "Gamepad", GAMEPAD_NAME_CAP - 1);
        return i;
    }
    return -1;
}

void gamepad_internal_disconnect(int slot)
{
    if (valid_slot(slot))
        memset(&g_pads[slot], 0, sizeof g_pads[slot]);
}

void gamepad_internal_set_button(int slot, int button, bool down)
{
    if (valid_slot(slot) && g_pads[slot].connected && button >= 0 && button < GAMEPAD_BUTTON_COUNT)
        g_pads[slot].buttons[button] = down;
}

void gamepad_internal_set_axis(int slot, int axis, float value)
{
    if (valid_slot(slot) && g_pads[slot].connected && axis >= 0 && axis < GAMEPAD_AXIS_COUNT)
        g_pads[slot].axes[axis] = value;
}

bool gamepad_connected(int index)
{
    return valid_slot(index) && g_pads[index].connected;
}

const char *gamepad_name(int index)
{
    return gamepad_connected(index) ? g_pads[index].name : NULL;
}

bool gamepad_button_down(int index, int button)
{
    return gamepad_connected(index) && button >= 0 && button < GAMEPAD_BUTTON_COUNT && g_pads[index].buttons[button];
}

float gamepad_axis(int index, int axis)
{
    return gamepad_connected(index) && axis >= 0 && axis < GAMEPAD_AXIS_COUNT ? g_pads[index].axes[axis] : 0.0f;
}

bool gamepad_rumble(int index, float strong, float weak, uint32_t milliseconds)
{
    if (!gamepad_connected(index))
        return false;
    strong = strong < 0.0f ? 0.0f : strong > 1.0f ? 1.0f : strong;
    weak = weak < 0.0f ? 0.0f : weak > 1.0f ? 1.0f : weak;
    return gamepad_backend_rumble(index, strong, weak, milliseconds);
}

unsigned gamepad_internal_generation(int slot)
{
    return valid_slot(slot) ? g_pad_gen[slot] : 0;
}

/* ---- joysticks ---- */

static bool valid_joy(int index)
{
    return index >= 0 && index < JOYSTICK_MAX;
}

int joystick_internal_connect(const char *name, int axes, int buttons, int hats)
{
    for (int i = 0; i < JOYSTICK_MAX; i++)
    {
        if (g_joys[i].connected)
            continue;
        memset(&g_joys[i], 0, sizeof g_joys[i]);
        Joy *j = &g_joys[i];
        j->connected = true;
        j->gamepad = -1;
        j->axes = axes < 0 ? 0 : axes > JOYSTICK_MAX_AXES ? JOYSTICK_MAX_AXES : axes;
        j->buttons = buttons < 0 ? 0 : buttons > JOYSTICK_MAX_BUTTONS ? JOYSTICK_MAX_BUTTONS : buttons;
        j->hats = hats < 0 ? 0 : hats > JOYSTICK_MAX_HATS ? JOYSTICK_MAX_HATS : hats;
        strncpy(j->name, name && name[0] ? name : "Joystick", GAMEPAD_NAME_CAP - 1);
        g_joy_gen[i]++;
        return i;
    }
    return -1;
}

void joystick_internal_disconnect(int slot)
{
    if (valid_joy(slot))
        memset(&g_joys[slot], 0, sizeof g_joys[slot]);
}

void joystick_internal_set_axis(int slot, int axis, float value)
{
    if (valid_joy(slot) && g_joys[slot].connected && axis >= 0 && axis < g_joys[slot].axes)
        g_joys[slot].axis[axis] = value;
}

void joystick_internal_set_button(int slot, int button, bool down)
{
    if (valid_joy(slot) && g_joys[slot].connected && button >= 0 && button < g_joys[slot].buttons)
        g_joys[slot].button[button] = down;
}

void joystick_internal_set_hat(int slot, int hat, int mask)
{
    if (valid_joy(slot) && g_joys[slot].connected && hat >= 0 && hat < g_joys[slot].hats)
        g_joys[slot].hat[hat] = mask;
}

void joystick_internal_link_gamepad(int slot, int gamepad_slot)
{
    if (valid_joy(slot) && g_joys[slot].connected)
        g_joys[slot].gamepad = valid_slot(gamepad_slot) ? gamepad_slot : -1;
}

unsigned joystick_internal_generation(int slot)
{
    return valid_joy(slot) ? g_joy_gen[slot] : 0;
}

bool joystick_connected(int index)
{
    return valid_joy(index) && g_joys[index].connected;
}

const char *joystick_name(int index)
{
    return joystick_connected(index) ? g_joys[index].name : NULL;
}

int joystick_axis_count(int index)
{
    return joystick_connected(index) ? g_joys[index].axes : 0;
}

int joystick_button_count(int index)
{
    return joystick_connected(index) ? g_joys[index].buttons : 0;
}

int joystick_hat_count(int index)
{
    return joystick_connected(index) ? g_joys[index].hats : 0;
}

float joystick_axis(int index, int axis)
{
    return joystick_connected(index) && axis >= 0 && axis < g_joys[index].axes ? g_joys[index].axis[axis] : 0.0f;
}

bool joystick_button(int index, int button)
{
    return joystick_connected(index) && button >= 0 && button < g_joys[index].buttons && g_joys[index].button[button];
}

int joystick_hat(int index, int hat)
{
    return joystick_connected(index) && hat >= 0 && hat < g_joys[index].hats ? g_joys[index].hat[hat] : 0;
}

int joystick_gamepad_index(int index)
{
    return joystick_connected(index) ? g_joys[index].gamepad : -1;
}
