
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDKeys.h>

#define _GLFW_PLATFORM_JOYSTICK_STATE         _GLFWjoystickNS ns
#define _GLFW_PLATFORM_LIBRARY_JOYSTICK_STATE

#define _GLFW_PLATFORM_MAPPING_NAME "Mac OS X"

typedef struct _GLFWjoystickNS
{
    IOHIDDeviceRef      device;
    CFMutableArrayRef   axes;
    CFMutableArrayRef   buttons;
    CFMutableArrayRef   hats;
} _GLFWjoystickNS;

void _glfwInitJoysticksNS(void);
void _glfwTerminateJoysticksNS(void);