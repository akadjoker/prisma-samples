#ifndef GAMEPAD_INTERNAL_H
#define GAMEPAD_INTERNAL_H

#include "platform.h"

void gamepad_init(void);
void gamepad_poll(void);
void gamepad_shutdown(void);

int gamepad_internal_connect(const char *name);
void gamepad_internal_disconnect(int slot);
void gamepad_internal_set_button(int slot, int button, bool down);
void gamepad_internal_set_axis(int slot, int axis, float value);

/* Joysticks (raw devices), filled by the backend like the gamepads. */
int joystick_internal_connect(const char *name, int axes, int buttons, int hats);
void joystick_internal_disconnect(int slot);
void joystick_internal_set_axis(int slot, int axis, float value);
void joystick_internal_set_button(int slot, int button, bool down);
void joystick_internal_set_hat(int slot, int hat, int mask);
void joystick_internal_link_gamepad(int slot, int gamepad_slot);

/* How many times a slot has been connected: a change means a different device, even
   when the slot was freed and taken again between two looks. */
unsigned gamepad_internal_generation(int slot);
unsigned joystick_internal_generation(int slot);

/* Vibration (rumble) for the slot; false if the device cannot. */
bool gamepad_backend_rumble(int slot, float strong, float weak, uint32_t ms);

void gamepad_backend_init(void);
void gamepad_backend_poll(void);
void gamepad_backend_shutdown(void);

#endif /* GAMEPAD_INTERNAL_H */
