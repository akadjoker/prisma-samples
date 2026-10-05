/*
 * vulkan.c - Vulkan loader and the platform-independent half of surface
 * creation. The window-system half (extension names, the actual
 * vkCreate*SurfaceKHR call) is in the backends.
 */
#include "vulkan_internal.h"
#include "core_internal.h"
#include "backend.h"
#include "error_internal.h"

#include <string.h>

#include <stdlib.h>

static void *g_gipa;
static bool g_tried;

static void *load_loader(void)
{
    static const char *const names[] = {
#if defined(_WIN32)
        "vulkan-1.dll",
#elif defined(__APPLE__)
        "libvulkan.1.dylib", "libMoltenVK.dylib",
#else
        "libvulkan.so.1", "libvulkan.so",
#endif
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    {
        SharedLibrary *lib = library_open(names[i]);
        if (!lib)
            continue;
        void *fn = library_symbol(lib, "vkGetInstanceProcAddr");
        if (fn)
            return fn; /* the loader stays loaded for the life of the process */
        library_close(lib);
    }
    return NULL;
}

void *vulkan_get_proc_addr(void)
{
    if (!g_tried)
    {
        g_tried = true;
        g_gipa = load_loader();
        if (!g_gipa)
            error_set("the Vulkan loader was not found");
    }
    return g_gipa;
}

const char *const *vulkan_instance_extensions(uint32_t *count)
{
    uint32_t n = 0;
    const char *const *list = backend_vulkan_extensions(&n);
    if (count)
        *count = list ? n : 0;
    return list;
}

bool vulkan_supported(void)
{
    ZenPfnGetInstanceProcAddr gipa = (ZenPfnGetInstanceProcAddr)vulkan_get_proc_addr();
    if (!gipa)
        return false;

    uint32_t want_n = 0;
    const char *const *want = backend_vulkan_extensions(&want_n);
    if (!want || want_n == 0)
    {
        error_set("this platform has no Vulkan surface support");
        return false;
    }

    ZenPfnEnumerateInstanceExtensionProperties enumerate =
        (ZenPfnEnumerateInstanceExtensionProperties)gipa(NULL, "vkEnumerateInstanceExtensionProperties");
    if (!enumerate)
        return false;

    uint32_t have_n = 0;
    if (enumerate(NULL, &have_n, NULL) != ZEN_VK_SUCCESS || have_n == 0)
    {
        error_set("the Vulkan loader reports no instance extensions (no driver installed?)");
        return false;
    }
    ZenVkExtensionProperties *have = malloc((size_t)have_n * sizeof *have);
    if (!have)
        return false;
    bool ok = enumerate(NULL, &have_n, have) == ZEN_VK_SUCCESS;
    for (uint32_t i = 0; ok && i < want_n; i++)
    {
        bool found = false;
        for (uint32_t j = 0; j < have_n && !found; j++)
            found = strcmp(want[i], have[j].extensionName) == 0;
        if (!found)
        {
            error_set("the Vulkan loader lacks the instance extension %s", want[i]);
            ok = false;
        }
    }
    free(have);
    return ok;
}

bool vulkan_call_create_surface(void *instance, const char *fn, const void *create_info,
                                const void *allocator, uint64_t *out_surface)
{
    ZenPfnGetInstanceProcAddr gipa = (ZenPfnGetInstanceProcAddr)vulkan_get_proc_addr();
    if (!gipa)
        return false;
    ZenPfnCreateSurface create = (ZenPfnCreateSurface)gipa(instance, fn);
    if (!create)
        return error_set("the instance has no %s (was VK_KHR_surface and the platform extension enabled?)", fn);
    uint64_t surface = 0;
    int r = create(instance, create_info, allocator, &surface);
    if (r != ZEN_VK_SUCCESS)
        return error_set("%s failed (VkResult %d)", fn, r);
    *out_surface = surface;
    return true;
}

bool vulkan_create_surface(PlatformWindow *w, void *instance, const void *allocator, uint64_t *out_surface)
{
    if (!w || !instance || !out_surface)
        return error_set("vulkan_create_surface needs a window, an instance and an output");
    if (w->cfg.render != RENDER_VULKAN)
        return error_set("the window was not created with RENDER_VULKAN");
    return backend_vulkan_create_surface(w->b, instance, allocator, out_surface);
}
