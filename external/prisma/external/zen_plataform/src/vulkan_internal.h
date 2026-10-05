/*
 * vulkan_internal.h - the few Vulkan declarations the backends need to create a
 * surface, so the library builds without the Vulkan SDK or its headers. Values
 * and layouts are the ones in vulkan_core.h and the vulkan_xlib / vulkan_win32 /
 * vulkan_android headers.
 */
#ifndef VULKAN_INTERNAL_H
#define VULKAN_INTERNAL_H

#include "platform.h"

#if defined(_WIN32) && !defined(_WIN64)
#define ZEN_VKAPI __stdcall
#else
#define ZEN_VKAPI
#endif

#define ZEN_VK_SUCCESS 0

/* VkStructureType values */
#define ZEN_VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR 1000004000
#define ZEN_VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR 1000008000
#define ZEN_VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR 1000009000

typedef struct
{
    char extensionName[256];
    uint32_t specVersion;
} ZenVkExtensionProperties;

typedef void *(ZEN_VKAPI *ZenPfnGetInstanceProcAddr)(void *instance, const char *name);
typedef int(ZEN_VKAPI *ZenPfnEnumerateInstanceExtensionProperties)(const char *layer, uint32_t *count,
                                                                  ZenVkExtensionProperties *props);
typedef int(ZEN_VKAPI *ZenPfnCreateSurface)(void *instance, const void *create_info, const void *allocator,
                                           uint64_t *surface);

/* Loader (vulkan.c). */
void *vulkan_get_proc_addr(void);

/* Resolves `fn` (a vkCreate*SurfaceKHR) on `instance` and calls it with the
   create info the backend filled in. */
bool vulkan_call_create_surface(void *instance, const char *fn, const void *create_info,
                                const void *allocator, uint64_t *out_surface);

#endif /* VULKAN_INTERNAL_H */
