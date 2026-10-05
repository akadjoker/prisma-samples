
#include <linux/input.h>
#include <linux/limits.h>
#include <regex.h>

#define _GLFW_PLATFORM_JOYSTICK_STATE         _GLFWjoystickLinux linjs
#define _GLFW_PLATFORM_LIBRARY_JOYSTICK_STATE _GLFWlibraryLinux  linjs

#define _GLFW_PLATFORM_MAPPING_NAME "Linux"

typedef struct _GLFWjoystickLinux
{
    int                     fd;
    char                    path[PATH_MAX];
    int                     keyMap[KEY_CNT - BTN_MISC];
    int                     absMap[ABS_CNT];
    struct input_absinfo    absInfo[ABS_CNT];
    int                     hats[4][2];
} _GLFWjoystickLinux;

typedef struct _GLFWlibraryLinux
{
    int                     inotify;
    int                     watch;
    regex_t                 regex;
    GLFWbool                dropped;
} _GLFWlibraryLinux;

GLFWbool _glfwInitJoysticksLinux(void);
void _glfwTerminateJoysticksLinux(void);
void _glfwDetectJoystickConnectionLinux(void);