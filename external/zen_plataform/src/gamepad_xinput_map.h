#ifndef GAMEPAD_XINPUT_MAP_H
#define GAMEPAD_XINPUT_MAP_H

#include "gamepad_internal.h"

typedef struct
{
    unsigned short buttons;
    unsigned char left_trigger, right_trigger;
    short lx, ly, rx, ry;
} XInputPadState;

enum
{
    XI_DPAD_UP = 0x0001,
    XI_DPAD_DOWN = 0x0002,
    XI_DPAD_LEFT = 0x0004,
    XI_DPAD_RIGHT = 0x0008,
    XI_START = 0x0010,
    XI_BACK = 0x0020,
    XI_LEFT_THUMB = 0x0040,
    XI_RIGHT_THUMB = 0x0080,
    XI_LEFT_SHOULDER = 0x0100,
    XI_RIGHT_SHOULDER = 0x0200,
    XI_A = 0x1000,
    XI_B = 0x2000,
    XI_X = 0x4000,
    XI_Y = 0x8000
};

static const struct
{
    unsigned short mask;
    int button;
} k_xinput_buttons[] = {
    {XI_A, GAMEPAD_BUTTON_A},
    {XI_B, GAMEPAD_BUTTON_B},
    {XI_X, GAMEPAD_BUTTON_X},
    {XI_Y, GAMEPAD_BUTTON_Y},
    {XI_BACK, GAMEPAD_BUTTON_BACK},
    {XI_START, GAMEPAD_BUTTON_START},
    {XI_LEFT_THUMB, GAMEPAD_BUTTON_LEFT_STICK},
    {XI_RIGHT_THUMB, GAMEPAD_BUTTON_RIGHT_STICK},
    {XI_LEFT_SHOULDER, GAMEPAD_BUTTON_LEFT_SHOULDER},
    {XI_RIGHT_SHOULDER, GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {XI_DPAD_UP, GAMEPAD_BUTTON_DPAD_UP},
    {XI_DPAD_DOWN, GAMEPAD_BUTTON_DPAD_DOWN},
    {XI_DPAD_LEFT, GAMEPAD_BUTTON_DPAD_LEFT},
    {XI_DPAD_RIGHT, GAMEPAD_BUTTON_DPAD_RIGHT},
};

static float xinput_stick(int value)
{
    float v = (float)value / 32767.0f;
    return v < -1.0f ? -1.0f : v > 1.0f ? 1.0f
                                        : v;
}

static void xinput_apply(int slot, const XInputPadState *s)
{
    for (unsigned i = 0; i < sizeof k_xinput_buttons / sizeof k_xinput_buttons[0]; i++)
        gamepad_internal_set_button(slot, k_xinput_buttons[i].button, (s->buttons & k_xinput_buttons[i].mask) != 0);
    gamepad_internal_set_axis(slot, GAMEPAD_AXIS_LEFT_X, xinput_stick(s->lx));
    gamepad_internal_set_axis(slot, GAMEPAD_AXIS_LEFT_Y, -xinput_stick(s->ly));
    gamepad_internal_set_axis(slot, GAMEPAD_AXIS_RIGHT_X, xinput_stick(s->rx));
    gamepad_internal_set_axis(slot, GAMEPAD_AXIS_RIGHT_Y, -xinput_stick(s->ry));
    gamepad_internal_set_axis(slot, GAMEPAD_AXIS_TRIGGER_LEFT, (float)s->left_trigger / 255.0f);
    gamepad_internal_set_axis(slot, GAMEPAD_AXIS_TRIGGER_RIGHT, (float)s->right_trigger / 255.0f);
}

#endif /* GAMEPAD_XINPUT_MAP_H */
