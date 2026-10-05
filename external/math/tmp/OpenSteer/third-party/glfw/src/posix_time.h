
#define _GLFW_PLATFORM_LIBRARY_TIMER_STATE _GLFWtimerPOSIX posix

#include <stdint.h>

typedef struct _GLFWtimerPOSIX
{
    GLFWbool    monotonic;
    uint64_t    frequency;

} _GLFWtimerPOSIX;

void _glfwInitTimerPOSIX(void);