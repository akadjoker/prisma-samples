#ifndef PLATFORM_EXPORT_H
#define PLATFORM_EXPORT_H
#if defined(_WIN32) && !defined(PLATFORM_STATIC)
#ifdef platform_EXPORTS /* CMake defines it when compiling the SHARED lib */
#define PLATFORM_API __declspec(dllexport)
#else
#define PLATFORM_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define PLATFORM_API __attribute__((visibility("default")))
#else
#define PLATFORM_API
#endif
#endif
