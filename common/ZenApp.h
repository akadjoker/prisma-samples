#pragma once

#include "platform.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/ShaderBlob.h"
#include "third_party/stb_image_write.h"

#ifdef PRISMA_HEADLESS
#include "Headless.h"
#endif

#include <ct/vector.hpp>

#include <stdlib.h>
#include <string.h>

namespace zenapp
{

#ifdef NDEBUG
const bool kDebug = false;
#else
const bool kDebug = true;
#endif

inline bool hasArgument(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], name) == 0) return true;
    return false;
}

inline const char* argumentValue(int argc, char** argv, const char* name)
{
    const size_t length = strlen(name);
    for (int i = 1; i < argc; ++i)
        if (strncmp(argv[i], name, length) == 0 && argv[i][length] == '=') return argv[i] + length + 1;
    return nullptr;
}

inline int frameLimit(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] >= '0' && argv[i][0] <= '9') return atoi(argv[i]);
    return 0;
}

inline prisma::DriverType driverType(int argc, char** argv)
{
    return hasArgument(argc, argv, "vulkan") ? prisma::DriverType::Vulkan
                                             : prisma::DriverType::OpenGL;
}

inline bool makeCurrent(void* user)
{
    window_make_current(static_cast<PlatformWindow*>(user));
    return true;
}

#ifdef __EMSCRIPTEN__
extern "C" void zenapp_wait_frame();
#endif

inline void swapBuffers(void* user)
{
    window_swap(static_cast<PlatformWindow*>(user));
#ifdef __EMSCRIPTEN__
    zenapp_wait_frame();
#endif
}

inline void framebufferSize(void* user, std::uint32_t* width, std::uint32_t* height)
{
    int w = 0;
    int h = 0;
    window_get_framebuffer_size(static_cast<PlatformWindow*>(user), &w, &h);
    *width = w > 0 ? static_cast<std::uint32_t>(w) : 0;
    *height = h > 0 ? static_cast<std::uint32_t>(h) : 0;
}

inline const char* const* instanceExtensions(void*, std::uint32_t* count)
{
    return vulkan_instance_extensions(count);
}

inline bool createSurface(void* user, void* instance, std::uint64_t* surface)
{
    return vulkan_create_surface(static_cast<PlatformWindow*>(user), instance, nullptr, surface);
}

inline void log(const char* message) { log_error("prisma: %s", message); }

inline int environmentInt(const char* name, int fallback)
{
    const char* value = getenv(name);
    return value && *value ? atoi(value) : fallback;
}

inline PlatformWindow* openWindowEx(const char* title, prisma::DriverType type, int width,
        int height, int x, int y, int monitor, bool resizable, bool vsync)
{
#ifdef PRISMA_HEADLESS
    WindowConfig fake = {};
    fake.title = title;
    fake.width = environmentInt("PRISMA_SHOT_WIDTH", 960);
    fake.height = environmentInt("PRISMA_SHOT_HEIGHT", 540);
    fake.render = RENDER_GL;
    (void) type;
    (void) width;
    (void) height;
    (void) x;
    (void) y;
    (void) monitor;
    (void) resizable;
    (void) vsync;
    return window_create(&fake);
#else
    WindowConfig config = {};
    config.title = title;
    config.width = width;
    config.height = height;
    config.x = x;
    config.y = y;
    config.monitor = monitor;
    config.resizable = resizable;
    config.vsync = vsync;

    if (type == prisma::DriverType::Vulkan)
    {
        if (!vulkan_supported()) return nullptr;
        config.render = RENDER_VULKAN;
        return window_create(&config);
    }

    config.render = RENDER_GL;
    config.gl.debug = kDebug;
#ifdef PRISMA_GLES
    config.gl.profile = GL_PROFILE_ES;
    config.gl.major = 3;
    for (int minor = 2; minor >= 0; --minor)
    {
        config.gl.minor = minor;
        PlatformWindow* window = window_create(&config);
        if (window) return window;
    }
    return nullptr;
#else
    config.gl.profile = GL_PROFILE_CORE;
    config.gl.major = 4;
    config.gl.minor = 6;
    return window_create(&config);
#endif
#endif
}

inline PlatformWindow* openWindow(const char* title, prisma::DriverType type)
{
#ifdef MONITOR_MOUSE
    const int monitor = MONITOR_MOUSE;
#else
    const int monitor = MONITOR_CURRENT;
#endif
    PlatformWindow* window = openWindowEx(title, type, 1280, 720, WINDOW_POS_CENTERED,
            WINDOW_POS_CENTERED, monitor, true, true);
    if (window) window_text_input_stop(window);
    return window;
}

inline prisma::ShaderHandle createShader(prisma::Driver* driver, const prisma::ShaderBlob& blob,
        const char* debugName = nullptr)
{
    prisma::ShaderDesc desc = prisma::shaderDesc(blob, driver->caps());
    desc.debugName = debugName;
    return driver->createShader(desc);
}

inline PlatformWindow*& captureWindow()
{
    static PlatformWindow* window = nullptr;
    return window;
}

inline void writeCapture(prisma::Driver* driver, PlatformWindow* window, const char* path)
{
    int width = 0;
    int height = 0;
    window_get_framebuffer_size(window, &width, &height);
    if (width <= 0 || height <= 0) return;
    ct::Vector<unsigned char> pixels;
    pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    prisma::Rect rect;
    rect.width = static_cast<std::uint32_t>(width);
    rect.height = static_cast<std::uint32_t>(height);
    if (!driver->readPixels(prisma::RenderTarget(), rect, pixels.data()))
    {
        log_error("capture: could not read the window");
        return;
    }
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    if (!stbi_write_png(path, width, height, 4, pixels.data(), width * 4))
        log_error("capture: could not write %s", path);
}

inline void endFrame(prisma::Driver* driver)
{
    static int frame = 0;
    static bool done = false;
    ++frame;
    const char* path = getenv("PRISMA_SHOT");
    PlatformWindow* window = captureWindow();
    if (path && *path && window && !done && frame >= environmentInt("PRISMA_SHOT_FRAME", 20))
    {
        writeCapture(driver, window, path);
        done = true;
        window_set_should_close(window, true);
    }
    driver->endFrame();
}

inline prisma::Driver* createDriver(PlatformWindow* window, prisma::DriverType type)
{
    captureWindow() = window;
    prisma::GLPlatform gl;
    prisma::VulkanPlatform vulkan;
#ifdef PRISMA_HEADLESS
    static headless::Context context;
    static headless::Surface surface;
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    window_get_framebuffer_size(window, &surfaceWidth, &surfaceHeight);
    surface.width = static_cast<std::uint32_t>(surfaceWidth);
    surface.height = static_cast<std::uint32_t>(surfaceHeight);
    if (type == prisma::DriverType::OpenGL)
    {
#ifdef PRISMA_GLES
        const bool es = true;
#else
        const bool es = false;
#endif
        if (!headless::openContext(&context, es, kDebug) ||
                !headless::openSurface(&context, surface.width, surface.height, &surface))
        {
            log_error("driver: could not create a headless OpenGL context");
            return nullptr;
        }
    }
    gl = headless::glPlatform(&surface);
    vulkan = headless::vulkanPlatform(&surface);
#else
    gl.user = window;
    gl.makeCurrent = makeCurrent;
    gl.swapBuffers = swapBuffers;
    gl.framebufferSize = framebufferSize;
    gl.getProcAddress = gl_proc_address;

    vulkan.user = window;
    vulkan.instanceExtensions = instanceExtensions;
    vulkan.createSurface = createSurface;
    vulkan.framebufferSize = framebufferSize;
#endif

    prisma::DriverDesc desc;
    desc.type = type;
    desc.gl = &gl;
    desc.vulkan = &vulkan;
    desc.log = log;
    desc.debug = kDebug;

    prisma::DriverError error = prisma::DriverError::None;
    prisma::Driver* driver = prisma::createDriver(desc, &error);
    if (!driver) log_error("driver: %s", prisma::driverErrorText(error));
    return driver;
}

} // namespace zenapp
