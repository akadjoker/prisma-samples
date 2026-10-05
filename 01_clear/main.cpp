#include "common/ZenApp.h"

#include <stdlib.h>

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma clear", driverType);
    if (!window)
    {
        log_error("window: %s", platform_get_error());
        platform_shutdown();
        return 1;
    }

    prisma::Driver* driver = zenapp::createDriver(window, driverType);
    if (!driver)
    {
        window_destroy(window);
        platform_shutdown();
        return 1;
    }

    const prisma::Caps& caps = driver->caps();
    log_info("%s %u.%u: max texture %u, max color targets %u, compute %d, debug %d",
            driver->type() == prisma::DriverType::Vulkan ? "Vulkan"
            : caps.gles                                  ? "OpenGL ES"
                                                         : "OpenGL",
            caps.versionMajor, caps.versionMinor, caps.maxTextureSize, caps.maxColorTargets,
            caps.compute, caps.debugOutput);

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.10f;
    pass.clearColor[1] = 0.25f;
    pass.clearColor[2] = 0.45f;

    int frames = 0;
    while (!window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        driver->beginFrame();
        driver->beginRenderPass(pass);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return 0;
}
