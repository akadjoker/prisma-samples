
#include <sys/queue.h>
#include <pthread.h>
#include <dlfcn.h>

#include <mir_toolkit/mir_client_library.h>

typedef VkFlags VkMirWindowCreateFlagsKHR;

typedef struct VkMirWindowCreateInfoKHR
{
    VkStructureType             sType;
    const void*                 pNext;
    VkMirWindowCreateFlagsKHR   flags;
    MirConnection*              connection;
    MirWindow*                  mirWindow;
} VkMirWindowCreateInfoKHR;

typedef VkResult (APIENTRY *PFN_vkCreateMirWindowKHR)(VkInstance,const VkMirWindowCreateInfoKHR*,const VkAllocationCallbacks*,VkSurfaceKHR*);
typedef VkBool32 (APIENTRY *PFN_vkGetPhysicalDeviceMirPresentationSupportKHR)(VkPhysicalDevice,uint32_t,MirConnection*);

#include "posix_thread.h"
#include "posix_time.h"
#include "linux_joystick.h"
#include "xkb_unicode.h"
#include "egl_context.h"
#include "osmesa_context.h"

#define _glfw_dlopen(name) dlopen(name, RTLD_LAZY | RTLD_LOCAL)
#define _glfw_dlclose(handle) dlclose(handle)
#define _glfw_dlsym(handle, name) dlsym(handle, name)

#define _GLFW_EGL_NATIVE_WINDOW  ((EGLNativeWindowType) window->mir.nativeWindow)
#define _GLFW_EGL_NATIVE_DISPLAY ((EGLNativeDisplayType) _glfw.mir.display)

#define _GLFW_PLATFORM_WINDOW_STATE         _GLFWwindowMir  mir
#define _GLFW_PLATFORM_MONITOR_STATE        _GLFWmonitorMir mir
#define _GLFW_PLATFORM_LIBRARY_WINDOW_STATE _GLFWlibraryMir mir
#define _GLFW_PLATFORM_CURSOR_STATE         _GLFWcursorMir  mir

#define _GLFW_PLATFORM_CONTEXT_STATE
#define _GLFW_PLATFORM_LIBRARY_CONTEXT_STATE

typedef struct EventQueue
{
    TAILQ_HEAD(, EventNode) head;
} EventQueue;

typedef struct _GLFWwindowMir
{
    MirWindow*              window;
    int                     width;
    int                     height;
    MirEGLNativeWindowType  nativeWindow;
    _GLFWcursor*            currentCursor;

} _GLFWwindowMir;

typedef struct _GLFWmonitorMir
{
    int curMode;
    int outputId;
    int x;
    int y;

} _GLFWmonitorMir;

typedef struct _GLFWlibraryMir
{
    MirConnection*          connection;
    MirEGLNativeDisplayType display;
    EventQueue* eventQueue;

    short int keycodes[256];
    short int scancodes[GLFW_KEY_LAST + 1];

    pthread_mutex_t eventMutex;
    pthread_cond_t  eventCond;

    _GLFWwindow* disabledCursorWindow;

} _GLFWlibraryMir;

typedef struct _GLFWcursorMir
{
    MirCursorConfiguration* conf;
    MirBufferStream*        customCursor;
    char const*             cursorName; 
} _GLFWcursorMir;

extern void _glfwPollMonitorsMir(void);
extern void _glfwInitEventQueueMir(EventQueue* queue);
extern void _glfwDeleteEventQueueMir(EventQueue* queue);