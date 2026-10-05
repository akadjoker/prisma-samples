#include "prisma/rhi/Driver.h"

namespace prisma
{

Driver* createNullDriver(const DriverDesc& desc);
#ifdef PRISMA_HAS_OPENGL
Driver* createGLDriver(const DriverDesc& desc, DriverError* error);
#endif
#ifdef PRISMA_HAS_VULKAN
Driver* createVulkanDriver(const DriverDesc& desc, DriverError* error);
#endif

bool isDriverSupported(DriverType type)
{
    switch (type)
    {
        case DriverType::Null:
            return true;
        case DriverType::OpenGL:
#ifdef PRISMA_HAS_OPENGL
            return true;
#else
            return false;
#endif
        case DriverType::Vulkan:
#ifdef PRISMA_HAS_VULKAN
            return true;
#else
            return false;
#endif
    }
    return false;
}

Driver* createDriver(const DriverDesc& desc, DriverError* error)
{
    DriverError result = DriverError::NotCompiled;
    Driver* driver = nullptr;
    switch (desc.type)
    {
        case DriverType::Null:
            driver = createNullDriver(desc);
            result = DriverError::None;
            break;
        case DriverType::OpenGL:
#ifdef PRISMA_HAS_OPENGL
            driver = createGLDriver(desc, &result);
#endif
            break;
        case DriverType::Vulkan:
#ifdef PRISMA_HAS_VULKAN
            driver = createVulkanDriver(desc, &result);
#endif
            break;
    }
    if (error) *error = result;
    return driver;
}

void destroyDriver(Driver* driver) { delete driver; }

const char* driverErrorText(DriverError error)
{
    switch (error)
    {
        case DriverError::None:
            return "no error";
        case DriverError::NotCompiled:
            return "backend not compiled into this build";
        case DriverError::MissingPlatform:
            return "platform functions missing in DriverDesc";
        case DriverError::ContextFailed:
            return "could not make the graphics context current";
        case DriverError::LoaderFailed:
            return "could not load the graphics functions";
        case DriverError::VersionTooLow:
            return "graphics API version too low";
    }
    return "unknown error";
}

} // namespace prisma
