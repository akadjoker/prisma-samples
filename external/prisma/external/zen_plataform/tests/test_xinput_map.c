#include "platform.h"
#include "../src/gamepad_xinput_map.h"

#include <math.h>
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

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)

int main(void)
{
    int slot = gamepad_internal_connect("Xbox Controller");
    CHECK(slot == 0);
    XInputPadState s = {0};

    for (unsigned i = 0; i < sizeof k_xinput_buttons / sizeof k_xinput_buttons[0]; i++)
    {
        s.buttons = k_xinput_buttons[i].mask;
        xinput_apply(slot, &s);
        for (int b = 0; b < GAMEPAD_BUTTON_COUNT; b++)
            CHECK(gamepad_button_down(slot, b) == (b == k_xinput_buttons[i].button));
    }
    s.buttons = XI_A | XI_LEFT_SHOULDER | XI_DPAD_LEFT;
    xinput_apply(slot, &s);
    CHECK(gamepad_button_down(slot, GAMEPAD_BUTTON_A));
    CHECK(gamepad_button_down(slot, GAMEPAD_BUTTON_LEFT_SHOULDER));
    CHECK(gamepad_button_down(slot, GAMEPAD_BUTTON_DPAD_LEFT));
    CHECK(!gamepad_button_down(slot, GAMEPAD_BUTTON_B));
    s.buttons = 0;
    xinput_apply(slot, &s);
    for (int b = 0; b < GAMEPAD_BUTTON_COUNT; b++)
        CHECK(!gamepad_button_down(slot, b));

    s.lx = 32767;
    s.rx = -32768;
    s.ly = 32767;
    s.ry = -32768;
    xinput_apply(slot, &s);
    CHECK(NEAR(gamepad_axis(slot, GAMEPAD_AXIS_LEFT_X), 1.0f));
    CHECK(gamepad_axis(slot, GAMEPAD_AXIS_RIGHT_X) == -1.0f);
    CHECK(NEAR(gamepad_axis(slot, GAMEPAD_AXIS_LEFT_Y), -1.0f));
    CHECK(gamepad_axis(slot, GAMEPAD_AXIS_RIGHT_Y) == 1.0f);

    s.lx = 16384;
    s.ly = 0;
    xinput_apply(slot, &s);
    CHECK(NEAR(gamepad_axis(slot, GAMEPAD_AXIS_LEFT_X), 0.5f));
    CHECK(gamepad_axis(slot, GAMEPAD_AXIS_LEFT_Y) == 0.0f || gamepad_axis(slot, GAMEPAD_AXIS_LEFT_Y) == -0.0f);

    s.left_trigger = 0;
    s.right_trigger = 255;
    xinput_apply(slot, &s);
    CHECK(gamepad_axis(slot, GAMEPAD_AXIS_TRIGGER_LEFT) == 0.0f);
    CHECK(gamepad_axis(slot, GAMEPAD_AXIS_TRIGGER_RIGHT) == 1.0f);
    s.left_trigger = 128;
    xinput_apply(slot, &s);
    CHECK(NEAR(gamepad_axis(slot, GAMEPAD_AXIS_TRIGGER_LEFT), 0.502f));

    gamepad_internal_disconnect(slot);
    xinput_apply(slot, &s);
    CHECK(!gamepad_connected(slot));

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
