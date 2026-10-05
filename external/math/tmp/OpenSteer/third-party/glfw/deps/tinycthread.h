
#ifndef _TINYCTHREAD_H_
#define _TINYCTHREAD_H_

#if !defined(_TTHREAD_PLATFORM_DEFINED_)
  #if defined(_WIN32) || defined(__WIN32__) || defined(__WINDOWS__)
    #define _TTHREAD_WIN32_
  #else
    #define _TTHREAD_POSIX_
  #endif
  #define _TTHREAD_PLATFORM_DEFINED_
#endif

#if defined(_TTHREAD_POSIX_)
  #undef _FEATURES_H
  #if !defined(_GNU_SOURCE)
    #define _GNU_SOURCE
  #endif
  #if !defined(_POSIX_C_SOURCE) || ((_POSIX_C_SOURCE - 0) < 199309L)
    #undef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 199309L
  #endif
  #if !defined(_XOPEN_SOURCE) || ((_XOPEN_SOURCE - 0) < 500)
    #undef _XOPEN_SOURCE
    #define _XOPEN_SOURCE 500
  #endif
#endif

#include <time.h>

#if defined(_TTHREAD_POSIX_)
  #include <sys/time.h>
  #include <pthread.h>
#elif defined(_TTHREAD_WIN32_)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #define __UNDEF_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #ifdef __UNDEF_LEAN_AND_MEAN
    #undef WIN32_LEAN_AND_MEAN
    #undef __UNDEF_LEAN_AND_MEAN
  #endif
#endif

#ifndef TIME_UTC
  #ifdef CLOCK_REALTIME
    #define TIME_UTC CLOCK_REALTIME
  #else
    #define TIME_UTC 0
  #endif
#endif

#if defined(_TTHREAD_WIN32_) || defined(__APPLE_CC__)
#define _TTHREAD_EMULATE_CLOCK_GETTIME_

#if defined(_TTHREAD_WIN32_)
struct _ttherad_timespec {
  time_t tv_sec;
  long   tv_nsec;
};
#define timespec _ttherad_timespec
#endif

typedef int _tthread_clockid_t;
#define clockid_t _tthread_clockid_t

int _tthread_clock_gettime(clockid_t clk_id, struct timespec *ts);
#define clock_gettime _tthread_clock_gettime
#ifndef CLOCK_REALTIME
  #define CLOCK_REALTIME 0
#endif
#endif

#define TINYCTHREAD_VERSION_MAJOR 1

#define TINYCTHREAD_VERSION_MINOR 1

#define TINYCTHREAD_VERSION (TINYCTHREAD_VERSION_MAJOR * 100 + TINYCTHREAD_VERSION_MINOR)

#if !(defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201102L)) && !defined(_Thread_local)
 #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
  #define _Thread_local __thread
 #else
  #define _Thread_local __declspec(thread)
 #endif
#endif

#define TSS_DTOR_ITERATIONS 0

#define thrd_error    0 
#define thrd_success  1 
#define thrd_timeout  2 
#define thrd_busy     3 
#define thrd_nomem    4 

#define mtx_plain     1
#define mtx_timed     2
#define mtx_try       4
#define mtx_recursive 8

#if defined(_TTHREAD_WIN32_)
typedef struct {
  CRITICAL_SECTION mHandle;   
  int mAlreadyLocked;         
  int mRecursive;             
} mtx_t;
#else
typedef pthread_mutex_t mtx_t;
#endif

int mtx_init(mtx_t *mtx, int type);

void mtx_destroy(mtx_t *mtx);

int mtx_lock(mtx_t *mtx);

int mtx_timedlock(mtx_t *mtx, const struct timespec *ts);

int mtx_trylock(mtx_t *mtx);

int mtx_unlock(mtx_t *mtx);

#if defined(_TTHREAD_WIN32_)
typedef struct {
  HANDLE mEvents[2];                  
  unsigned int mWaitersCount;         
  CRITICAL_SECTION mWaitersCountLock; 
} cnd_t;
#else
typedef pthread_cond_t cnd_t;
#endif

int cnd_init(cnd_t *cond);

void cnd_destroy(cnd_t *cond);

int cnd_signal(cnd_t *cond);

int cnd_broadcast(cnd_t *cond);

int cnd_wait(cnd_t *cond, mtx_t *mtx);

int cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);

#if defined(_TTHREAD_WIN32_)
typedef HANDLE thrd_t;
#else
typedef pthread_t thrd_t;
#endif

typedef int (*thrd_start_t)(void *arg);

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);

thrd_t thrd_current(void);

int thrd_detach(thrd_t thr);

int thrd_equal(thrd_t thr0, thrd_t thr1);

void thrd_exit(int res);

int thrd_join(thrd_t thr, int *res);

int thrd_sleep(const struct timespec *time_point, struct timespec *remaining);

void thrd_yield(void);

#if defined(_TTHREAD_WIN32_)
typedef DWORD tss_t;
#else
typedef pthread_key_t tss_t;
#endif

typedef void (*tss_dtor_t)(void *val);

int tss_create(tss_t *key, tss_dtor_t dtor);

void tss_delete(tss_t key);

void *tss_get(tss_t key);

int tss_set(tss_t key, void *val);

#endif 