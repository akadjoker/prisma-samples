#pragma once

#include "prisma/rhi/Types.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#if defined(PRISMA_TEST_VULKAN) || defined(PRISMA_HEADLESS)
#include <vulkan/vulkan.h>
#endif

#include <stdint.h>

namespace headless
{

struct Context
{
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config = nullptr;
};

struct Surface
{
    Context* context = nullptr;
    EGLSurface surface = EGL_NO_SURFACE;
    uint32_t width = 0;
    uint32_t height = 0;
};

inline bool openContext(Context* out, bool es, bool debug)
{
    typedef EGLDisplay (*GetPlatformDisplay)(EGLenum, void*, const EGLint*);
    GetPlatformDisplay getPlatformDisplay =
            reinterpret_cast<GetPlatformDisplay>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    out->display = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                                EGL_DEFAULT_DISPLAY, nullptr)
                                      : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0;
    EGLint minor = 0;
    if (out->display == EGL_NO_DISPLAY || !eglInitialize(out->display, &major, &minor))
        return false;
    if (!eglBindAPI(es ? EGL_OPENGL_ES_API : EGL_OPENGL_API)) return false;

    const EGLint configAttributes[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        es ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,
        8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
    EGLint count = 0;
    if (!eglChooseConfig(out->display, configAttributes, &out->config, 1, &count) || count == 0)
        return false;

    const EGLint debugValue = debug ? EGL_TRUE : EGL_FALSE;
    for (EGLint minorVersion = es ? 2 : 6; minorVersion >= 0 && out->context == EGL_NO_CONTEXT;
            --minorVersion)
    {
        const EGLint attributes[] = { EGL_CONTEXT_MAJOR_VERSION, es ? 3 : 4,
            EGL_CONTEXT_MINOR_VERSION, minorVersion, EGL_CONTEXT_OPENGL_DEBUG, debugValue,
            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
        const EGLint esAttributes[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION,
            minorVersion, EGL_CONTEXT_OPENGL_DEBUG, debugValue, EGL_NONE };
        out->context = eglCreateContext(out->display, out->config, EGL_NO_CONTEXT,
                es ? esAttributes : attributes);
        if (!es) break;
    }
    return out->context != EGL_NO_CONTEXT;
}

inline bool openSurface(Context* context, uint32_t width, uint32_t height, Surface* out)
{
    out->context = context;
    out->width = width;
    out->height = height;
    const EGLint attributes[] = { EGL_WIDTH, static_cast<EGLint>(width), EGL_HEIGHT,
        static_cast<EGLint>(height), EGL_NONE };
    out->surface = eglCreatePbufferSurface(context->display, context->config, attributes);
    return out->surface != EGL_NO_SURFACE;
}

inline void closeSurface(Surface* surface)
{
    if (surface->context && surface->surface != EGL_NO_SURFACE)
    {
        if (eglGetCurrentSurface(EGL_DRAW) == surface->surface)
            eglMakeCurrent(surface->context->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                    EGL_NO_CONTEXT);
        eglDestroySurface(surface->context->display, surface->surface);
    }
    surface->surface = EGL_NO_SURFACE;
}

inline void closeContext(Context* context)
{
    if (context->display == EGL_NO_DISPLAY) return;
    eglMakeCurrent(context->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context->context != EGL_NO_CONTEXT) eglDestroyContext(context->display, context->context);
    eglTerminate(context->display);
    context->display = EGL_NO_DISPLAY;
    context->context = EGL_NO_CONTEXT;
}

inline bool makeCurrent(void* user)
{
    Surface* surface = static_cast<Surface*>(user);
    return eglMakeCurrent(surface->context->display, surface->surface, surface->surface,
                   surface->context->context) == EGL_TRUE;
}

inline void swapBuffers(void* user)
{
    Surface* surface = static_cast<Surface*>(user);
    eglSwapBuffers(surface->context->display, surface->surface);
}

inline void* getProcAddress(const char* name)
{
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

inline void framebufferSize(void* user, uint32_t* width, uint32_t* height)
{
    const Surface* surface = static_cast<const Surface*>(user);
    *width = surface->width;
    *height = surface->height;
}

#if defined(PRISMA_TEST_VULKAN) || defined(PRISMA_HEADLESS)

inline const char* const* instanceExtensions(void*, uint32_t* count)
{
    static const char* const extensions[] = { "VK_KHR_surface", "VK_EXT_headless_surface" };
    *count = 2;
    return extensions;
}

inline bool createSurface(void*, void* instance, uint64_t* surface)
{
    VkInstance vulkanInstance = static_cast<VkInstance>(instance);
    PFN_vkCreateHeadlessSurfaceEXT create = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
            vkGetInstanceProcAddr(vulkanInstance, "vkCreateHeadlessSurfaceEXT"));
    if (!create) return false;
    VkHeadlessSurfaceCreateInfoEXT info = {};
    info.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    VkSurfaceKHR handle = VK_NULL_HANDLE;
    if (create(vulkanInstance, &info, nullptr, &handle) != VK_SUCCESS) return false;
    *surface = reinterpret_cast<uint64_t>(handle);
    return true;
}

#endif

inline prisma::GLPlatform glPlatform(Surface* surface)
{
    prisma::GLPlatform platform;
    platform.user = surface;
    platform.makeCurrent = makeCurrent;
    platform.swapBuffers = swapBuffers;
    platform.framebufferSize = framebufferSize;
    platform.getProcAddress = getProcAddress;
    return platform;
}

#if defined(PRISMA_TEST_VULKAN) || defined(PRISMA_HEADLESS)
inline prisma::VulkanPlatform vulkanPlatform(Surface* surface)
{
    prisma::VulkanPlatform platform;
    platform.user = surface;
    platform.instanceExtensions = instanceExtensions;
    platform.createSurface = createSurface;
    platform.framebufferSize = framebufferSize;
    return platform;
}
#endif

} // namespace headless
