
#define _GLFW_PLATFORM_JOYSTICK_STATE         _GLFWjoystickWin32 win32
#define _GLFW_PLATFORM_LIBRARY_JOYSTICK_STATE int dummy

#define _GLFW_PLATFORM_MAPPING_NAME "Windows"

typedef struct _GLFWjoyobjectWin32
{
    int                     offset;
    int                     type;
} _GLFWjoyobjectWin32;

typedef struct _GLFWjoystickWin32
{
    _GLFWjoyobjectWin32*    objects;
    int                     objectCount;
    IDirectInputDevice8W*   device;
    DWORD                   index;
    GUID                    guid;
} _GLFWjoystickWin32;

void _glfwInitJoysticksWin32(void);
void _glfwTerminateJoysticksWin32(void);
void _glfwDetectJoystickConnectionWin32(void);
void _glfwDetectJoystickDisconnectionWin32(void);