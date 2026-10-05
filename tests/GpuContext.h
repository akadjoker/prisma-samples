#pragma once

#include "Headless.h"
#include "prisma/rhi/Driver.h"

#include <stdio.h>
#include <string.h>

struct GpuContext
{
    headless::Context context;
    headless::Surface surface;
    prisma::GLPlatform gl;
    prisma::VulkanPlatform vulkan;
    prisma::Driver* driver = nullptr;
    bool useVulkan = false;

    bool open(int argc, char** argv, void (*log)(const char*) = nullptr)
    {
        for (int i = 1; i < argc; ++i)
            if (strcmp(argv[i], "vulkan") == 0) useVulkan = true;

        surface.width = 64;
        surface.height = 64;
        if (!useVulkan)
        {
#ifdef PRISMA_GLES
            const bool es = true;
#else
            const bool es = false;
#endif
            if (!headless::openContext(&context, es, true) ||
                    !headless::openSurface(&context, 64, 64, &surface))
            {
                printf("headless OpenGL context could not be created\n");
                return false;
            }
        }
        gl = headless::glPlatform(&surface);
#ifdef PRISMA_TEST_VULKAN
        vulkan = headless::vulkanPlatform(&surface);
#endif

        prisma::DriverDesc desc;
        desc.type = useVulkan ? prisma::DriverType::Vulkan : prisma::DriverType::OpenGL;
        desc.gl = &gl;
        desc.vulkan = &vulkan;
        desc.log = log;
        desc.debug = true;
        prisma::DriverError error = prisma::DriverError::None;
        driver = prisma::createDriver(desc, &error);
        return driver != nullptr;
    }

    void close()
    {
        if (driver) prisma::destroyDriver(driver);
        driver = nullptr;
        headless::closeSurface(&surface);
        headless::closeContext(&context);
    }
};
