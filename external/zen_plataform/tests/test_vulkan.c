/*
 * test_vulkan.c - a RENDER_VULKAN window, the native handles, and a real
 * VkSurfaceKHR on a real VkInstance. Needs a display and a Vulkan driver (Mesa's
 * lavapipe is enough); skips (77) when there is none. The few Vulkan types used
 * are declared here, so the test needs no Vulkan headers either.
 */
#include "platform.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) && !defined(_WIN64)
#define VKCALL __stdcall
#else
#define VKCALL
#endif

typedef struct
{
    int sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkAppInfo;

typedef struct
{
    int sType;
    const void *pNext;
    uint32_t flags;
    const VkAppInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
} VkInstInfo;

typedef void *(VKCALL *PfnGipa)(void *instance, const char *name);
typedef int(VKCALL *PfnCreateInstance)(const VkInstInfo *, const void *, void **);
typedef void(VKCALL *PfnDestroyInstance)(void *, const void *);
typedef void(VKCALL *PfnDestroySurface)(void *, uint64_t, const void *);

static int g_pass, g_fail;

#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (cond)                                                  \
            g_pass++;                                              \
        else                                                       \
        {                                                          \
            g_fail++;                                              \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

static int skip(const char *why)
{
    printf("skip: %s\n", why);
    platform_shutdown();
    return 77;
}

int main(void)
{
    if (!platform_init())
        return skip("no display");
    if (!vulkan_supported())
        return skip(platform_get_error());

    PfnGipa gipa = (PfnGipa)vulkan_get_proc_addr();
    CHECK(gipa != NULL);

    uint32_t n = 0;
    const char *const *ext = vulkan_instance_extensions(&n);
    CHECK(ext != NULL && n >= 2);
    CHECK(n >= 1 && strcmp(ext[0], "VK_KHR_surface") == 0);

    /* a GL window has no Vulkan surface */
    WindowConfig gl_cfg = {.title = "gl", .width = 64, .height = 64};
    PlatformWindow *glw = window_create(&gl_cfg);
    if (glw)
    {
        uint64_t s = 0;
        CHECK(!vulkan_create_surface(glw, (void *)&n, NULL, &s));
        CHECK(strstr(platform_get_error(), "RENDER_VULKAN") != NULL);
        CHECK(window_native_handle(glw, NATIVE_GL_CONTEXT) != NULL);
        window_destroy(glw);
    }

    WindowConfig cfg = {.title = "vk", .width = 200, .height = 120, .render = RENDER_VULKAN};
    PlatformWindow *w = window_create(&cfg);
    if (!w)
        return skip(platform_get_error());

    CHECK(window_native_handle(w, NATIVE_WINDOW) != NULL);
    CHECK(window_native_handle(w, NATIVE_DISPLAY) != NULL);
    CHECK(window_native_handle(w, NATIVE_GL_CONTEXT) == NULL); /* no GL context */

    int fw = 0, fh = 0;
    window_get_framebuffer_size(w, &fw, &fh);
    CHECK(fw > 0 && fh > 0);

    /* the window runs a frame loop like any other, and swap is harmless */
    window_begin_frame(w);
    window_swap(w);

    PfnCreateInstance create_instance = (PfnCreateInstance)gipa(NULL, "vkCreateInstance");
    CHECK(create_instance != NULL);
    VkInstInfo info = {.sType = 1, .enabledExtensionCount = n, .ppEnabledExtensionNames = ext};
    void *instance = NULL;
    if (!create_instance || create_instance(&info, NULL, &instance) != 0 || !instance)
    {
        window_destroy(w);
        return skip("cannot create a VkInstance (no Vulkan driver?)");
    }

    uint64_t surface = 0;
    bool ok = vulkan_create_surface(w, instance, NULL, &surface);
    if (!ok)
        printf("vulkan_create_surface: %s\n", platform_get_error());
    CHECK(ok);
    CHECK(surface != 0);

    /* a bad argument is an error, not a crash */
    CHECK(!vulkan_create_surface(w, NULL, NULL, &surface));

    PfnDestroySurface destroy_surface = (PfnDestroySurface)gipa(instance, "vkDestroySurfaceKHR");
    PfnDestroyInstance destroy_instance = (PfnDestroyInstance)gipa(instance, "vkDestroyInstance");
    CHECK(destroy_surface != NULL && destroy_instance != NULL);
    if (destroy_surface && ok)
        destroy_surface(instance, surface, NULL);
    if (destroy_instance)
        destroy_instance(instance, NULL);

    window_destroy(w);
    platform_shutdown();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
