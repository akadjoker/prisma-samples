#include "gamepad_internal.h"

void gamepad_backend_init(void)
{
}

void gamepad_backend_poll(void)
{
}

void gamepad_backend_shutdown(void)
{
}

bool gamepad_backend_rumble(int slot, float strong, float weak, uint32_t ms)
{
    (void)slot, (void)strong, (void)weak, (void)ms;
    return false;
}
