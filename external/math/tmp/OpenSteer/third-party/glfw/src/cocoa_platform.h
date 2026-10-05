
#include <stdint.h>
#include <dlfcn.h>

#if defined(__OBJC__)
#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#else
#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>
typedef void* id;
#endif

typedef VkFlags VkMacOSSurfaceCreateFlagsMVK;

typedef struct VkMacOSSurfaceCreateInfoMVK
{
    VkStructureType                 sType;
    const void*                     pNext;
    VkMacOSSurfaceCreateFlagsMVK    flags;
    const void*                     pView;
} VkMacOSSurfaceCreateInfoMVK;

typedef VkResult (APIENTRY *PFN_vkCreateMacOSSurfaceMVK)(VkInstance,const VkMacOSSurfaceCreateInfoMVK*,const VkAllocationCallbacks*,VkSurfaceKHR*);

#include "posix_thread.h"
#include "cocoa_joystick.h"
#include "nsgl_context.h"
#include "egl_context.h"
#include "osmesa_context.h"

#define _glfw_dlopen(name) dlopen(name, RTLD_LAZY | RTLD_LOCAL)
#define _glfw_dlclose(handle) dlclose(handle)
#define _glfw_dlsym(handle, name) dlsym(handle, name)

#define _GLFW_EGL_NATIVE_WINDOW  ((EGLNativeWindowType) window->ns.view)
#define _GLFW_EGL_NATIVE_DISPLAY EGL_DEFAULT_DISPLAY

#define _GLFW_PLATFORM_WINDOW_STATE         _GLFWwindowNS  ns
#define _GLFW_PLATFORM_LIBRARY_WINDOW_STATE _GLFWlibraryNS ns
#define _GLFW_PLATFORM_LIBRARY_TIMER_STATE  _GLFWtimerNS   ns
#define _GLFW_PLATFORM_MONITOR_STATE        _GLFWmonitorNS ns
#define _GLFW_PLATFORM_CURSOR_STATE         _GLFWcursorNS  ns

#define kTISPropertyUnicodeKeyLayoutData _glfw.ns.tis.kPropertyUnicodeKeyLayoutData
typedef TISInputSourceRef (*PFN_TISCopyCurrentKeyboardLayoutInputSource)(void);
#define TISCopyCurrentKeyboardLayoutInputSource _glfw.ns.tis.CopyCurrentKeyboardLayoutInputSource
typedef void* (*PFN_TISGetInputSourceProperty)(TISInputSourceRef,CFStringRef);
#define TISGetInputSourceProperty _glfw.ns.tis.GetInputSourceProperty
typedef UInt8 (*PFN_LMGetKbdType)(void);
#define LMGetKbdType _glfw.ns.tis.GetKbdType

typedef struct _GLFWwindowNS
{
    id              object;
    id              delegate;
    id              view;
    id              layer;

    GLFWbool        maximized;

    int             width, height;
    int             fbWidth, fbHeight;
    float           xscale, yscale;

    double          cursorWarpDeltaX, cursorWarpDeltaY;

} _GLFWwindowNS;

typedef struct _GLFWlibraryNS
{
    CGEventSourceRef    eventSource;
    id                  delegate;
    id                  autoreleasePool;
    GLFWbool            cursorHidden;
    TISInputSourceRef   inputSource;
    IOHIDManagerRef     hidManager;
    id                  unicodeData;
    id                  listener;

    char                keyName[64];
    short int           keycodes[256];
    short int           scancodes[GLFW_KEY_LAST + 1];
    char*               clipboardString;
    CGPoint             cascadePoint;

    double              restoreCursorPosX, restoreCursorPosY;

    _GLFWwindow*        disabledCursorWindow;

    struct {
        CFBundleRef     bundle;
        PFN_TISCopyCurrentKeyboardLayoutInputSource CopyCurrentKeyboardLayoutInputSource;
        PFN_TISGetInputSourceProperty GetInputSourceProperty;
        PFN_LMGetKbdType GetKbdType;
        CFStringRef     kPropertyUnicodeKeyLayoutData;
    } tis;

} _GLFWlibraryNS;

typedef struct _GLFWmonitorNS
{
    CGDirectDisplayID   displayID;
    CGDisplayModeRef    previousMode;
    uint32_t            unitNumber;
    id                  screen;

} _GLFWmonitorNS;

typedef struct _GLFWcursorNS
{
    id              object;

} _GLFWcursorNS;

typedef struct _GLFWtimerNS
{
    uint64_t        frequency;

} _GLFWtimerNS;

void _glfwInitTimerNS(void);

void _glfwPollMonitorsNS(void);
void _glfwSetVideoModeNS(_GLFWmonitor* monitor, const GLFWvidmode* desired);
void _glfwRestoreVideoModeNS(_GLFWmonitor* monitor);