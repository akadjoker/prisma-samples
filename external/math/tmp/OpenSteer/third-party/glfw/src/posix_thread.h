
#include <pthread.h>

#define _GLFW_PLATFORM_TLS_STATE    _GLFWtlsPOSIX   posix
#define _GLFW_PLATFORM_MUTEX_STATE  _GLFWmutexPOSIX posix

typedef struct _GLFWtlsPOSIX
{
    GLFWbool        allocated;
    pthread_key_t   key;

} _GLFWtlsPOSIX;

typedef struct _GLFWmutexPOSIX
{
    GLFWbool        allocated;
    pthread_mutex_t handle;

} _GLFWmutexPOSIX;