/*
 * thread.c - threads, recursive mutexes, condition variables and the CPU count,
 * over pthreads and the Win32 API. Not available on the web.
 */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* pthread_setname_np */
#endif

#include "platform.h"
#include "error_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)

#define UNSUPPORTED() error_set("threads are not supported on the web")

PlatformThread *thread_create(ThreadFunc fn, void *user, const char *name)
{
    (void)fn, (void)user, (void)name;
    UNSUPPORTED();
    return NULL;
}
int thread_join(PlatformThread *t) { (void)t; return -1; }
void thread_detach(PlatformThread *t) { (void)t; }
uint64_t thread_current_id(void) { return 1; }
PlatformMutex *mutex_create(void)
{
    UNSUPPORTED();
    return NULL;
}
void mutex_destroy(PlatformMutex *m) { (void)m; }
void mutex_lock(PlatformMutex *m) { (void)m; }
bool mutex_try_lock(PlatformMutex *m) { (void)m; return false; }
void mutex_unlock(PlatformMutex *m) { (void)m; }
PlatformCond *cond_create(void)
{
    UNSUPPORTED();
    return NULL;
}
void cond_destroy(PlatformCond *c) { (void)c; }
void cond_signal(PlatformCond *c) { (void)c; }
void cond_broadcast(PlatformCond *c) { (void)c; }
void cond_wait(PlatformCond *c, PlatformMutex *m) { (void)c, (void)m; }
bool cond_wait_timeout(PlatformCond *c, PlatformMutex *m, uint32_t ms) { (void)c, (void)m, (void)ms; return false; }
int cpu_count(void) { return 1; }

#else

/* A thread's lifetime: whoever comes second frees the record. */
enum { T_RUNNING = 0, T_DETACHED = 1, T_FINISHED = 2 };

#if defined(_MSC_VER)
#include <intrin.h>
#define XCHG(p, v) ((int)_InterlockedExchange((volatile long *)(p), (long)(v)))
#else
#define XCHG(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#endif

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>

struct PlatformThread
{
    HANDLE handle;
    ThreadFunc fn;
    void *user;
    int result;
    volatile int state;
};
struct PlatformMutex
{
    CRITICAL_SECTION cs; /* recursive by nature */
};
struct PlatformCond
{
    CONDITION_VARIABLE cv;
};

static unsigned __stdcall trampoline(void *p)
{
    PlatformThread *t = p;
    t->result = t->fn(t->user);
    if (XCHG(&t->state, T_FINISHED) == T_DETACHED)
    {
        CloseHandle(t->handle);
        free(t);
    }
    return 0;
}

PlatformThread *thread_create(ThreadFunc fn, void *user, const char *name)
{
    (void)name;
    if (!fn)
    {
        error_set("thread_create needs a function");
        return NULL;
    }
    PlatformThread *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;
    t->fn = fn;
    t->user = user;
    t->handle = (HANDLE)_beginthreadex(NULL, 0, trampoline, t, 0, NULL);
    if (!t->handle)
    {
        free(t);
        error_set("cannot create the thread");
        return NULL;
    }
    return t;
}

int thread_join(PlatformThread *t)
{
    if (!t)
        return -1;
    WaitForSingleObject(t->handle, INFINITE);
    int r = t->result;
    CloseHandle(t->handle);
    free(t);
    return r;
}

void thread_detach(PlatformThread *t)
{
    if (t && XCHG(&t->state, T_DETACHED) == T_FINISHED)
    {
        CloseHandle(t->handle);
        free(t);
    }
}

uint64_t thread_current_id(void)
{
    return (uint64_t)GetCurrentThreadId();
}

PlatformMutex *mutex_create(void)
{
    PlatformMutex *m = malloc(sizeof *m);
    if (m)
        InitializeCriticalSection(&m->cs);
    return m;
}
void mutex_destroy(PlatformMutex *m)
{
    if (m)
    {
        DeleteCriticalSection(&m->cs);
        free(m);
    }
}
void mutex_lock(PlatformMutex *m) { EnterCriticalSection(&m->cs); }
bool mutex_try_lock(PlatformMutex *m) { return TryEnterCriticalSection(&m->cs) != 0; }
void mutex_unlock(PlatformMutex *m) { LeaveCriticalSection(&m->cs); }

PlatformCond *cond_create(void)
{
    PlatformCond *c = malloc(sizeof *c);
    if (c)
        InitializeConditionVariable(&c->cv);
    return c;
}
void cond_destroy(PlatformCond *c) { free(c); }
void cond_signal(PlatformCond *c) { WakeConditionVariable(&c->cv); }
void cond_broadcast(PlatformCond *c) { WakeAllConditionVariable(&c->cv); }
void cond_wait(PlatformCond *c, PlatformMutex *m) { SleepConditionVariableCS(&c->cv, &m->cs, INFINITE); }
bool cond_wait_timeout(PlatformCond *c, PlatformMutex *m, uint32_t ms)
{
    return SleepConditionVariableCS(&c->cv, &m->cs, ms) != 0;
}

int cpu_count(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
}

#else /* POSIX */

#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

struct PlatformThread
{
    pthread_t th;
    ThreadFunc fn;
    void *user;
    int result;
    char name[16]; /* the Linux limit, with the NUL */
    volatile int state;
};
struct PlatformMutex
{
    pthread_mutex_t m;
};
struct PlatformCond
{
    pthread_cond_t c;
};

static void *trampoline(void *p)
{
    PlatformThread *t = p;
#if defined(__linux__) || defined(__ANDROID__)
    if (t->name[0])
        pthread_setname_np(pthread_self(), t->name);
#endif
    t->result = t->fn(t->user);
    if (XCHG(&t->state, T_FINISHED) == T_DETACHED)
        free(t);
    return NULL;
}

PlatformThread *thread_create(ThreadFunc fn, void *user, const char *name)
{
    if (!fn)
    {
        error_set("thread_create needs a function");
        return NULL;
    }
    PlatformThread *t = calloc(1, sizeof *t);
    if (!t)
        return NULL;
    t->fn = fn;
    t->user = user;
    if (name)
        snprintf(t->name, sizeof t->name, "%s", name);
    int rc = pthread_create(&t->th, NULL, trampoline, t);
    if (rc != 0)
    {
        free(t);
        error_set("cannot create the thread (error %d)", rc);
        return NULL;
    }
    return t;
}

int thread_join(PlatformThread *t)
{
    if (!t)
        return -1;
    pthread_join(t->th, NULL);
    int r = t->result;
    free(t);
    return r;
}

void thread_detach(PlatformThread *t)
{
    if (!t)
        return;
    pthread_detach(t->th);
    if (XCHG(&t->state, T_DETACHED) == T_FINISHED)
        free(t);
}

uint64_t thread_current_id(void)
{
    return (uint64_t)(uintptr_t)pthread_self();
}

PlatformMutex *mutex_create(void)
{
    PlatformMutex *m = malloc(sizeof *m);
    if (!m)
        return NULL;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    int rc = pthread_mutex_init(&m->m, &a);
    pthread_mutexattr_destroy(&a);
    if (rc != 0)
    {
        free(m);
        return NULL;
    }
    return m;
}
void mutex_destroy(PlatformMutex *m)
{
    if (m)
    {
        pthread_mutex_destroy(&m->m);
        free(m);
    }
}
void mutex_lock(PlatformMutex *m) { pthread_mutex_lock(&m->m); }
bool mutex_try_lock(PlatformMutex *m) { return pthread_mutex_trylock(&m->m) == 0; }
void mutex_unlock(PlatformMutex *m) { pthread_mutex_unlock(&m->m); }

PlatformCond *cond_create(void)
{
    PlatformCond *c = malloc(sizeof *c);
    if (!c)
        return NULL;
    pthread_condattr_t a;
    pthread_condattr_init(&a);
#if !defined(__APPLE__) && defined(CLOCK_MONOTONIC)
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC); /* timeouts must not follow the wall clock */
#endif
    int rc = pthread_cond_init(&c->c, &a);
    pthread_condattr_destroy(&a);
    if (rc != 0)
    {
        free(c);
        return NULL;
    }
    return c;
}
void cond_destroy(PlatformCond *c)
{
    if (c)
    {
        pthread_cond_destroy(&c->c);
        free(c);
    }
}
void cond_signal(PlatformCond *c) { pthread_cond_signal(&c->c); }
void cond_broadcast(PlatformCond *c) { pthread_cond_broadcast(&c->c); }
void cond_wait(PlatformCond *c, PlatformMutex *m) { pthread_cond_wait(&c->c, &m->m); }

bool cond_wait_timeout(PlatformCond *c, PlatformMutex *m, uint32_t ms)
{
    struct timespec ts;
#if !defined(__APPLE__) && defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L)
    {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&c->c, &m->m, &ts) == 0;
}

int cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

#endif /* POSIX / Win32 */

#endif /* web */
